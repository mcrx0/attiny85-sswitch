#!/usr/bin/env python3
"""
Walk the USB descriptors straight out of the compiled ELF and check them
against the CDC-ACM requirements.

Descriptors are hand-written byte arrays, which is exactly the kind of thing
that is right in every individual byte and wrong in total length, endpoint
number, or a length field that does not match its descriptor. Those are the
bugs that produce a device which never appears as /dev/ttyACM* and give no
hint why.

    python3 tools/check_descriptors.py firmware/build/attiny85-sswitch.elf

Reads the real bytes from the ELF, so it checks what is actually flashed, not
what is intended.
"""

import re
import sys

# --- ELF byte extraction ---------------------------------------------------

SHT_PROGBITS = 1


def sections(path):
    """Return {name: (addr, bytes)} for allocated sections."""
    out = {}
    with open(path, "rb") as f:
        data = f.read()

    if data[:4] != b"\x7fELF":
        raise SystemExit(f"{path} is not an ELF file")

    is32 = data[4] == 1
    little = data[5] == 1
    end = "<" if little else ">"

    if is32:
        e_shoff = int.from_bytes(data[0x20:0x24], "little" if little else "big")
        e_shentsize = int.from_bytes(data[0x2E:0x30], "little" if little else "big")
        e_shnum = int.from_bytes(data[0x30:0x32], "little" if little else "big")
        e_shstrndx = int.from_bytes(data[0x32:0x34], "little" if little else "big")
        sh_fmt = end + "IIIIIIIIII"
    else:
        raise SystemExit("64-bit ELF is not expected from avr-gcc")

    def sh(i):
        off = e_shoff + i * e_shentsize
        fields = __import__("struct").unpack_from(sh_fmt, data, off)
        return fields  # name, type, flags, addr, offset, size, link, info, ...

    shstr = sh(e_shstrndx)
    strtab_off, strtab_size = shstr[4], shstr[5]
    strtab = data[strtab_off:strtab_off + strtab_size]

    def name_at(n):
        end_n = strtab.index(b"\0", n)
        return strtab[n:end_n].decode()

    for i in range(e_shnum):
        fields = sh(i)
        out[name_at(fields[0])] = (fields[3],
                                   data[fields[4]:fields[4] + fields[5]])

    return out


def _plausible_config(cfg):
    """
    Does this byte range look like a CDC-ACM configuration descriptor?

    Necessary because a 2-byte pattern like 09 02 occurs all over a code
    image. Without this, the scanner happily latches onto whatever comes
    first and the checks then pass or fail about the wrong bytes.
    """
    # Layout, per USB 2.0 §9.1.2:
    #   [0] bLength            [5] bConfigurationValue
    #   [1] bDescriptorType    [6] iConfiguration
    #   [2..3] wTotalLength    [7] bmAttributes
    #   [4] bNumInterfaces    [8] bMaxPower
    #
    # Mind bNumInterfaces at [4]. It is easy to write a header without it,
    # shift every later field by one, and get a descriptor whose individual
    # bytes look plausible but which no host will accept.
    if len(cfg) < 9 or cfg[0] != 9 or cfg[1] != 2:
        return False

    total = int.from_bytes(cfg[2:4], "little")
    if total != len(cfg) or not (40 <= total <= 100):
        return False
    if not (1 <= cfg[4] <= 4):     # bNumInterfaces
        return False
    if cfg[5] != 1:                # bConfigurationValue
        return False
    if cfg[6] != 0:                # iConfiguration
        return False
    # Note the parentheses: `cfg[7] & 0x80 != 0x80` parses as
    # `cfg[7] & (0x80 != 0x80)`, which is always falsy and silently rejects
    # every real descriptor.
    if (cfg[7] & 0x80) != 0x80:   # bmAttributes must be bus powered
        return False
    if not (1 <= cfg[8] <= 50):  # bMaxPower, 2 mA units, <= 100 mA
        return False

    # Walk it. A real descriptor frames exactly, ends on the last byte, and
    # carries the interfaces and endpoints a CDC device is supposed to have.
    i, ifaces, ends = 0, 0, 0
    while i < len(cfg):
        if i + 2 > len(cfg):
            return False
        ln, dt = cfg[i], cfg[i + 1]
        if ln < 2 or i + ln > len(cfg):
            return False
        if dt == 4:
            ifaces += 1
        if dt == 5:
            ends += 1
        i += ln

    if i != len(cfg):
        return False
    if not (1 <= ifaces <= 4) or not (1 <= ends <= 4):
        return False

    # At least one interface must actually declare endpoints, and there must
    # be at least one interrupt endpoint for bulk or interrupt data.
    with_eps = sum(
        cfg[k + 4] for k in range(len(cfg) - 1)
        if cfg[k + 1] == 4 and k + 5 <= len(cfg)
    )
    if with_eps < 1:
        return False
    if ends < 1:
        return False

    return True


def _plausible_device(dev):
    if len(dev) != 18 or dev[0] != 18 or dev[1] != 1:
        return False
    if dev[4] != 0x02 or dev[7] != 64 or dev[17] != 1:
        return False
    # Must contain a descriptor-sized run of the same class codes.
    return dev[2:4] in (b"\x10\x01", b"\x00\x02", b"\x10\x00")


def find_descriptors(secs):
    """
    Pull deviceDescriptor and configurationDescriptor out of the image.

    Located by scanning for well-known prefixes rather than by symbol, so
    this needs neither nm nor a symbol table. Every candidate is validated
    before being accepted -- see the _plausible_* helpers for why a bare
    byte-pattern match is not enough.
    """
    dev_prefix = bytes([18, 1, 0x10, 0x01])
    cfg_prefix = bytes([9, 2])

    found = {}
    for name, (_addr, body) in secs.items():
        if name not in (".text", ".data", ".rodata"):
            continue
        for i in range(len(body) - 4):
            if "device" not in found and body[i:i + 4] == dev_prefix:
                cand = body[i:i + 18]
                if _plausible_device(cand):
                    found["device"] = cand
            if "configuration" not in found and body[i:i + 2] == cfg_prefix:
                total = int.from_bytes(body[i + 2:i + 4], "little")
                if 40 <= total <= 100 and len(body) >= i + total:
                    cand = body[i:i + total]
                    if _plausible_config(cand):
                        found["configuration"] = cand
    return found


# --- descriptor walking ----------------------------------------------------

failures = []
checks = 0


def check(what, got, want):
    global checks
    checks += 1
    if got != want:
        failures.append(f"{what}: got {got!r}, want {want!r}")
    return got == want


def walk_config(cfg):
    """Yield (descriptor_type, bytes) and validate the framing."""
    i = 0
    out = []
    while i < len(cfg):
        if i + 2 > len(cfg):
            failures.append(f"truncated descriptor header at offset {i}")
            break
        length, dtype = cfg[i], cfg[i + 1]
        if length < 2:
            failures.append(f"descriptor at {i} has bLength {length}, must be >= 2")
            break
        if i + length > len(cfg):
            failures.append(
                f"descriptor at {i} claims {length} bytes but only "
                f"{len(cfg) - i} remain")
            break
        out.append((dtype, cfg[i:i + length]))
        i += length
    check("configuration descriptor frames exactly", i, len(cfg))
    return out


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else \
        "firmware/build/attiny85-sswitch.elf"

    secs = sections(path)
    desc = find_descriptors(secs)

    if "device" not in desc:
        raise SystemExit("could not find the device descriptor in the ELF")
    if "configuration" not in desc:
        raise SystemExit("could not find the configuration descriptor in the ELF")

    dev = desc["device"]
    cfg = desc["configuration"]

    print("device descriptor")
    check("bLength", dev[0], 18)
    check("bDescriptorType", dev[1], 1)
    check("bcdUSB", int.from_bytes(dev[2:4], "little"), 0x0110)
    check("bDeviceClass (CDC)", dev[4], 0x02)
    check("bDeviceProtocol (AT)", dev[6], 0x01)
    check("bMaxPacketSize0", dev[7], 64)
    vid = int.from_bytes(dev[8:10], "little")
    pid = int.from_bytes(dev[10:12], "little")
    check("idVendor", vid, 0x16C0)
    check("idProduct", pid, 0x05E1)
    check("bNumConfigurations", dev[17], 1)

    print("configuration descriptor")
    total = int.from_bytes(cfg[2:4], "little")
    check("wTotalLength matches the array", total, len(cfg))
    check("bNumInterfaces", cfg[4], 2)
    check("bConfigurationValue", cfg[5], 1)
    check("iConfiguration", cfg[6], 0)
    check("bmAttributes bus-powered", cfg[7] & 0x80, 0x80)
    # bMaxPower is in 2 mA units and USB 2.0 caps a bus-powered device at
    # 100 mA, i.e. 50. Overstating it is how a brownout sneaks up on you.
    check("bMaxPower is 100 mA (2 mA units)", cfg[8], 50)

    descs = walk_config(cfg)
    print(f"  {len(descs)} descriptors, {len(cfg)} bytes total")

    # Interfaces
    ifaces = [d for t, d in descs if t == 4]
    check("two interface descriptors", len(ifaces), 2)

    comm, data = ifaces
    check("interface 0 bInterfaceNumber", comm[2], 0)
    check("interface 0 is CDC ACM", (comm[5], comm[6]), (0x02, 0x02))
    check("interface 0 endpoint count", comm[4], 1)
    check("interface 1 bInterfaceNumber", data[2], 1)
    check("interface 1 is CDC data", data[5], 0x0A)
    check("interface 1 endpoint count", data[4], 2)

    # CDC functional descriptors
    funcs = {d[2]: d for t, d in descs if t == 0x24}
    check("CDC header present", 0x00 in funcs, True)
    check("CDC call management present", 0x01 in funcs, True)
    check("CDC ACM present", 0x02 in funcs, True)
    check("CDC union present", 0x06 in funcs, True)

    check("ACM bmCapabilities is LINE only",
          funcs[0x02][3], 0x02)
    # The point of ADR 0003 / usbconfig.h: advertising BRK without implementing
    # SEND_BREAK means a host gets no error when it tries.
    check("ACM does not advertise BRK (unimplemented)",
          funcs[0x02][3] & 0x04, 0)

    union = funcs[0x06]
    check("union control interface", union[3], 0)
    check("union subordinate interface", union[4], 1)

    # Endpoints
    eps = [d for t, d in descs if t == 5]
    print("endpoints")
    seen = {}
    for e in eps:
        addr = e[2]
        num = addr & 0x0F
        direction = "IN" if addr & 0x80 else "OUT"
        attrs = e[3]
        size = int.from_bytes(e[4:6], "little")
        seen[(num, direction)] = (attrs, size)
        print(f"  EP{num} {direction}: attributes=0x{attrs:02x} "
              f"maxpacket={size}")

    # Every endpoint address must be distinct, and EP0 must never appear in
    # the configuration descriptor.
    check("no duplicate endpoint addresses", len(seen), len(eps))
    for num, direction in seen:
        check(f"EP{num} {direction} is not EP0", num != 0, True)
        check(f"EP{num} {direction} is in range", 1 <= num <= 15, True)

    check("EP1 IN declared", (1, "IN") in seen, True)
    check("EP1 OUT declared", (1, "OUT") in seen, True)
    check("EP3 IN notification declared", (3, "IN") in seen, True)

    # The load-bearing bit from ADR 0001: bulk is forbidden at low speed, so
    # the CDC data endpoints must be interrupt or cdc-acm never binds.
    for key in ((1, "IN"), (1, "OUT")):
        if key in seen:
            attrs, size = seen[key]
            check(f"EP{key[0]} {key[1]} attributes are INTERRUPT not BULK",
                  attrs, 0x03)
            check(f"EP{key[0]} {key[1]} max packet size is 8", size, 8)

    print()
    if failures:
        print(f"{checks} checks, {len(failures)} FAILURES")
        for f in failures:
            print(f"  FAIL {f}")
        return 1
    print(f"{checks} checks, 0 failures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
