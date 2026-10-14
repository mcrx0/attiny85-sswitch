# 0001 — V-USB CDC-ACM rather than vendor-specific or HID

Date: 2026-09-30

## Status

Accepted

## Context

The goal is a device that a Linux host can drive with a plain shell redirect:

```bash
echo "ON 0" > /dev/ttyACM0
```

That requires the device to enumerate as a CDC-ACM class device, because that
is what makes the kernel bind `cdc_acm` and hand back a `/dev/ttyACM*` node.
Nothing about "a USB device on a Digispark" implies that. The ATtiny85 has **no
USB peripheral at all** — no `ATtiny85` variant ships with one, and USB exists
only on `AT90USB*` and `ATmega32U4`. Everything is bit-banged by V-USB at
low speed, 1.5 Mbit/s, in software.

Three options:

1. **CDC-ACM** — `/dev/ttyACM0`, works with `stty`, `cat`, `echo`, pyserial,
   screen, and anything that speaks serial. ~1.4 kB of V-USB plus a hand-written
   CDC layer.
2. **Vendor-specific** (`16c0:05dc`) — needs libusb or pyusb on the host. No
   `/dev/tty*`, so the shell redirect is gone and every user needs a library.
   Needs a udev rule on Linux and a filter on Windows.
3. **HID** (`16c0:05df`) — a HID class device, not a port. It works, but the
   host side is a HID library and the device is a keyboard from the OS's point
   of view.

## Decision

CDC-ACM.

The deciding factor is that option 1 makes the *simplest possible client* the
correct client. That matters more than the flash it costs. A user who wants to
switch a relay should not have to install a Python package, and the difference
between "flash size we could have saved" and "nobody can use this without a
dependency" is not close.

HID was rejected specifically because it would still need a host library while
being harder to inspect, and vendor-specific for the same reason as HID, with
the extra burden of udev rules.

## Consequences

**obdev discourage this.** Their own documentation says of low-speed virtual
COM ports: *"We strongly discourage this method because it does things forbidden
by the USB specification."* The specification requires CDC data endpoints to be
bulk, and forbids bulk endpoints at low speed. We satisfy both by declaring the
data endpoints as **interrupt** (`bmAttributes = 0x03`). Linux's `cdc-acm`
submits URBs to whatever the descriptor names and does not care what they are
called. The failure mode of getting this wrong is not a subtly wrong port — it
is a device that never produces `/dev/ttyACM*` at all.

That also means we **do not advertise `USB_CDC_CAP_BRK`** (0x04) in the ACM
functional descriptor. We do not implement `SEND_BREAK`, and advertising a
capability we do not have means a host gets no error when it tries to use it.

**Windows and macOS are unverified.** obdev's own CDC solution needed a
Windows driver patch for precisely this reason. We declare Linux support and say
nothing else. This is the single largest risk in the design, and it is why the
host tool is Linux-first.

**V-USB's transport quirks had to be worked around in two places**, both
documented at the call site:

- `USB_CFG_IMPLEMENT_FN_WRITEOUT` must be 1. V-USB's `usbFunctionRead()` and
  `usbFunctionWrite()` are for *control* transfers only; the only path in from
  the host on the data endpoint is `usbFunctionWriteOut()`. Leave the flag at 0
  and every command is received, ACKed by the USB layer, and discarded without
  reaching the application. It fails silently.
- Device → host has to use `usbSetInterrupt()`, because `usbPoll()`'s own
  transmit path is driven by `usbMsgLen`, which is `static` inside `usbdrv.c`
  and unreachable from application code. Replies therefore go out one 8-byte
  packet at a time.

**Port discovery is not free.** With several switches plugged in, matching on
VID/PID alone is ambiguous — `16c0:05e1` is shared by every obdev-style
CDC-ACM device, and obdev's rules require distinguishing by manufacturer and
product string. `tools/sswitch` matches on VID/PID and accepts `--port`; with
more than one device, use `--port`.

**Licence.** V-USB is GPL-2.0-or-GPL-3.0-or-commercial, so this project is
GPL-2.0-or-later. See [0003](0003-always-off-boot-with-configurable-polarity.md)
for the sharing-terms obligation on the VID/PID.

## Alternatives reconsidered

If Windows support turns out to be needed, the options are a CDC descriptor that
`usbser.sys` accepts (unverified), or switching to option 2 and adding pyusb.
Switching to option 2 means giving up the shell redirect, which was the entire
point, so the decision would need re-making rather than patching.

## References

- <https://www.obdev.at/products/vusb/index.html> — bulk endpoints forbidden at
  low speed; supported configurations; the discourage-CDC note
- <https://github.com/obdev/V-USB> — the driver, vendored at
  `firmware/vendor/v-usb/`
- Linux `drivers/usb/class/cdc-acm.c` — endpoint-type agnostic, and
  `USB_CDC_CAP_BRK` gating `tty.set_break()`
