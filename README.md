# attiny85-sswitch

A USB serial switch for a Digispark. Plug it into a Linux box, it appears as
`/dev/ttyACM0`, and you switch a relay with a line of text.

```bash
stty -F /dev/ttyACM0 raw -echo
echo "ON 0" > /dev/ttyACM0          # no Python, no libraries, no Arduino
```

Or with the bundled tool, which also speaks English:

```bash
tools/sswitch on 0
tools/sswitch repl
switch> switch on pin 0
```

## What it is

Bare-metal C on an ATtiny85. The chip has no USB peripheral at all, so USB 1.1
low-speed is bit-banged by [V-USB](https://www.obdev.at/products/vusb/index.html)
and shaped into a CDC-ACM serial device. There is no Arduino, no Digistump
board package, and no runtime — the whole firmware is about 5 kB of flash and
300 bytes of RAM, and it enumerates as a serial port.

## Channels

The Digispark exposes six pins. Three of them are not yours:

| Pin | Port | Status |
|---|---|---|
| P0 | PB0 | ✅ channel, PWM (OC0A) |
| P1 | PB1 | ✅ channel, PWM (OC0B) — **onboard LED attached** |
| P2 | PB2 | ✅ channel, **no PWM** |
| P3 | PB3 | ❌ USB D− |
| P4 | PB4 | ❌ USB D+ |
| P5 | PB5 | ❌ RESET |

So there are three channels. Asking for a fourth gets you a reason rather than
silence:

```
$ tools/sswitch on 4
ERR P4 USB D+
```

P1 has the onboard LED wired to it, which loads the pin. It works, but use P0
or P2 for relays. See [docs/pinout.md](docs/pinout.md).

## Commands

One line in, exactly one line out. The device never speaks unsolicited and
never echoes.

| Command | Effect |
|---|---|
| `ON <p>` | duty 255 — full on |
| `ON <p> <1-255>` | PWM duty |
| `OFF <p>` | duty 0 |
| `HIGH <p>` / `LOW <p>` | raw level, PWM mode off |
| `IN <p>` / `OUT <p>` | pin direction; `OUT` always re-enters off |
| `READ <p>` | actual level back |
| `PINS [--raw]` | every channel |
| `ALL OFF` | everything off |
| `HELP`, `STATUS`, `?` | |

`ON`/`OFF` are decisions; `HIGH`/`LOW` are voltages. Which level energises
your relay is a property of your relay module, not of the protocol. Full
contract in [docs/protocol.md](docs/protocol.md).

**There is no toggle, anywhere.** Every command assigns a value. A command that
toggles cannot be retried, and a script that times out and re-sends would flip
the relay back to where it started.

## Safety

- **Boot is always OFF.** All three channels are de-energised before USB is
  even initialised. Write `PORTB` before `DDRB` so the pin never passes
  through the wrong level — on an active-LOW module, the reset value of the pin
  is LOW, which is *energised*. The reference project this design grew from
  gets that backwards, and on a mains relay it means an audible click and a
  real contact closure at every power-up.
- **8-second watchdog.** V-USB does its bit-banging inside a single interrupt,
  so a wedged loop is unrecoverable without yanking the cable. A reset lands in
  the same all-off path.
- **Add the 10 kΩ pull-up** from the signal line to VCC in
  [docs/wiring.md](docs/wiring.md). That is what makes OFF real while the pin
  is tri-stated during reset.

## Layout

```
firmware/     bare-metal C, V-USB vendored
  src/        main.c channels.c proto.c reply.c, config.h board.h
tools/        sswitch (CLI + REPL), protocol_sim.py (test double),
              check_descriptors.py (walks the descriptors out of the ELF)
tests/        protocol_test.c (host build of the real sources), test_sswitch.py,
              hoststubs/
docs/         protocol.md wiring.md pinout.md toolchain.md, adr/
scripts/      bootstrap-toolchain.sh flash.sh
CONTEXT.md    vocabulary
```

## Docs

| | |
|---|---|
| [CONTEXT.md](CONTEXT.md) | vocabulary — including the words this project refuses to use |
| [docs/protocol.md](docs/protocol.md) | the contract, and how to script it |
| [docs/THIRD-PARTY-LICENSES.md](docs/THIRD-PARTY-LICENSES.md) | every dependency, its licence, and what it obliges you to |
| [docs/wiring.md](docs/wiring.md) | relay modules, the pull-up, polarity traps, power |
| [docs/pinout.md](docs/pinout.md) | the three channels, and why there are only three |
| [docs/toolchain.md](docs/toolchain.md) | build, fuses, flashing, toolchain |
| [tests/README.md](tests/README.md) | what the tests cover, and what only hardware can check |

Design decisions, with the reasoning and what was rejected:
[0001 USB stack](docs/adr/0001-usb-cdc-acm-over-vendor-specific-or-hid.md) ·
[0002 language in the host](docs/adr/0002-host-owns-the-command-language.md) ·
[0003 always-OFF boot](docs/adr/0003-always-off-boot-with-configurable-polarity.md)

## Building

```bash
scripts/bootstrap-toolchain.sh   # no root: unpacks into ./.toolchain
make firmware                    # -> firmware/build/attiny85-sswitch.hex
make test                        # host-side protocol + CLI tests
```

Flashing needs a Digispark with the stock micronucleus bootloader, which is
what the factory boards ship with:

```bash
scripts/flash.sh                 # or: make flash
```

## Tests

`make test` runs the protocol tests and the CLI tests. The protocol tests
compile `channels.c`, `proto.c` and `reply.c` a second time as host code
against `tests/hoststubs/`, so the logic under test is the logic that gets
flashed — only the USB transport underneath it is absent.

The CLI tests run `tools/sswitch` against `tools/protocol_sim.py`, a test
double on a pty. **Neither can tell you the device enumerates or moves a pin.**
That needs hardware; see [tests/README.md](tests/README.md).

## Licence

GPL-2.0-or-later — see [LICENSE](LICENSE). It follows from V-USB, which is
GPL-2.0/GPL-3.0-or-commercial, so a permissive licence is not available
without replacing V-USB.

Every dependency, its licence, and which obligations each one carries:
**[docs/THIRD-PARTY-LICENSES.md](docs/THIRD-PARTY-LICENSES.md)**. It records
two things that are not yet satisfied and that only you can fix:

- obdev asks for a circuit diagram as a minimum publication standard, and this
  repo has wiring as text only ([docs/wiring.md](docs/wiring.md))
- the shared VID/PID requires a manufacturer string containing a domain or
  e-mail address you own, and `USB_CFG_VENDOR_NAME` is still a placeholder

`firmware/vendor/v-usb/` is vendored **unmodified** with its own licence
intact at [`usbdrv/License.txt`](firmware/vendor/v-usb/usbdrv/License.txt);
see [`VENDORED.md`](firmware/vendor/v-usb/VENDORED.md) for what was removed and
why.

Two things to change before you publish anything:

1. **`USB_CFG_VENDOR_NAME`** in `firmware/usbconfig.h` is set to
   `example.invalid`. obdev's rules for the shared VID/PID 16c0:05e1 require
   the manufacturer string to contain a domain or e-mail address **you** own
   — `firmware/vendor/v-usb/usbdrv/USB-IDs-for-free.txt`, rule (2).
2. **The copyright line** in `LICENSE` says "the attiny85-sswitch authors".
   Put your name or organisation in if that matters to you.
