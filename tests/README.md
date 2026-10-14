# Tests

Three layers. `make test` runs all of them; none of them need a Digispark.

| Layer | What it is | Needs hardware? |
|---|---|---|
| `test-protocol` | firmware sources compiled as host code, protocol behaviour checked | no |
| `test-descriptors` | USB descriptors walked out of the compiled ELF | no |
| `test-vendor` | the vendored V-USB is really vendored | no |
| `test-cli` | `tools/sswitch` driven against a protocol test double | no |

**None of them can tell you the device enumerates, or moves a pin.** That needs
hardware. See "Hardware checks" below.

## test-protocol — the real firmware, on the host

`tests/protocol_test.c` `#include`s `firmware/src/channels.c`, `proto.c` and
`reply.c` directly, so **the code under test is the code that gets flashed**.
Only the hardware underneath is substituted: `tests/hoststubs/avr/io.h` turns
`PORTB`, `DDRB`, `TCCR0A` and friends into plain variables, and
`tests/hoststubs/usbdrv.h` replaces the V-USB transmit path.

This is not a mock. It is the same source, compiled for the host.

It covers boot state and the `PORTB`-before-`DDRB` ordering, the polarity
mapping both ways, PWM duty → OCR arithmetic including the 128-boundary where
the compare mode flips, the reserved-pin rejections, idempotence, `PINS` in
both formats, and the error paths.

The stubs mirror the hardware definitions — the Timer/Counter0 bit names live in
`hoststubs/avr/io.h`, taken from the datasheet. If they disagreed with the real
`<avr/io.h>` the tests would be checking the wrong bits, so they are worth
keeping in step if you touch them.

## test-descriptors — descriptors out of the ELF

`tools/check_descriptors.py` reads the compiled `.elf`, finds the device and
configuration descriptors, and checks them against the CDC-ACM requirements:
total length, endpoint numbering, the functional descriptors, and the endpoint
attribute type.

Descriptors are hand-written byte arrays, which is the kind of thing that is
correct in every individual byte and wrong in the total.

**This test has already caught one real bug.** The configuration header was
missing its `bNumInterfaces` byte, which shifted every following field by one.
Every other byte was individually plausible and the array was 67 bytes long as
written — but `bNumInterfaces` read as `bConfigurationValue`, the interface
count read as 0, and the device would never have enumerated. There was no way
to see that by reading the source; it only showed up by walking the bytes of
the built image.

Run it by hand:

```bash
python3 tools/check_descriptors.py firmware/build/attiny85-sswitch.elf
```

## test-vendor — the vendored driver is really vendored

`scripts/check-vendor.sh` checks that `firmware/vendor/v-usb/` holds the driver
files, has no nested `.git`, is not recorded as a submodule gitlink, and has its
upstream commit recorded.

It exists because of one specific failure mode. Re-cloning V-USB into that path
and forgetting to remove `.git` does not produce a loud error — `git add`
converts the whole directory into a gitlink (mode 160000), the commit succeeds,
and the driver files are simply not in history. Clone that commit and
`vendor/v-usb/` is empty, so the build fails with `usbdrv.h: No such file or
directory` and no hint about the cause.

Verified by reproducing it: a scratch repo where `git add` recorded mode 160000,
and this check reported the gitlink, the missing driver files, and the missing
upstream commit.

## test-cli — the host tool, against a test double

`tools/protocol_sim.py` implements the line protocol on a pty, and
`tests/test_sswitch.py` runs the real `tools/sswitch` as a subprocess against
it. This exercises argument handling, English translation, exit codes, and
port handling end to end.

**`protocol_sim.py` is a test double, not an emulator.** It re-implements the
protocol from `docs/protocol.md`. It cannot tell you the firmware compiles,
enumerates, or drives a pin. Its value is that `test_sswitch.py` does not have
to re-state the protocol expectations in a way that can drift from it.

## Hardware checks — the part that only hardware does

Everything below needs a Digispark and, for the last one, a relay.

### 1. Enumeration

```bash
dmesg | tail -20
```

You want a `cdc_acm` line and a new `/dev/ttyACM*`. If nothing appears, the
device is not enumerating at all — and `test-descriptors` is the first place
to look, because a malformed descriptor produces exactly this with no error.

```bash
lsusb | grep -i 16c0:05e1
```

### 2. Identity

```bash
tools/sswitch status        # ATTINY85-SSWITCH 1.0
lsusb -v -d 16c0:05e1 | grep -i manufacturer
```

The manufacturer string is currently the `example.invalid` placeholder. See
`docs/adr/0003`.

### 3. Boot is safe

Power-cycle **with the relay connected** and listen. There must be no click.

This is the single most important hardware check, and it is the one the
`PORTB`-before-`DDRB` ordering exists for. If you hear a click at power-up,
either the ordering is wrong or the polarity is set wrong for your module.

```bash
tools/sswitch pins          # all three OFF immediately after boot
```

### 4. Pins actually move

```bash
tools/sswitch on 0          # OK P0 ON HIGH 255
```

Confirm with a meter on the pin: about 5 V. Then `off 0`: about 0 V.

For PWM, a meter reads roughly `duty/255` of the rail:

```bash
tools/sswitch dim 0 128     # about 2.5 V on an active-high channel
```

### 5. Reset behaviour

With a relay connected and *on*, press the Digispark's reset button. The relay
must turn off. This is the watchdog path and the reset path landing in the same
all-off state.

### 6. The watchdog

Unplug the host data path mid-command, or better: this one needs a debugger.
The honest version is that a wedged `usbPoll()` is not reachable from software,
so verify by inspection that `wdt_reset()` is in the main loop and that
`wdt_enable(WDTO_8S)` is called before it. Both are in `firmware/src/main.c`.

## Adding a test

Protocol behaviour goes in `tests/protocol_test.c` — it compiles the real
sources, so a test there is a test of what you flash. Host-tool behaviour goes
in `tests/test_sswitch.py`, and if you add a protocol feature, add it to
`tools/protocol_sim.py` in the same commit so the two do not drift.
