# Toolchain

The firmware needs an AVR cross-compiler. Nothing else — no Arduino IDE, no
Digistump board package, no platformio.

## Bootstrap

```bash
scripts/bootstrap-toolchain.sh
```

This unpacks `gcc-avr`, `binutils-avr` and `avr-libc` from Debian/Ubuntu
packages into `./.toolchain/opt/` and puts nothing anywhere else. **No root, no
system changes** — `.toolchain/` is gitignored and you can delete it whenever
you like. That approach works fine in a container or on a locked-down machine
where `apt install` is not an option.

If you already have the tools, use them:

```bash
export PATH=/usr/bin:$PATH
make firmware
```

The `firmware/Makefile` calls `avr-gcc`, `avr-objcopy` and `avr-size` by name
and ignores `make`'s own `CC`, so any of these work:

```bash
make firmware                              # uses whatever is on PATH
PATH="$PWD/.toolchain/opt/usr/bin:$PATH" make firmware
```

## Docker

```bash
make docker-check      # build the image, then build and test in it
```

Same toolchain, same output bytes, no bootstrap and nothing installed on the
host. `docker/Dockerfile` pins `ubuntu:24.04` by digest and the three AVR
packages by version, specifically so the container compiles the same firmware
the budget above describes. Full guide, including CI, troubleshooting and why
the obvious third-party AVR images do not work: **[docker/README.md](../docker/README.md)**.

## What you need

| Tool | Used for | Provided by |
|---|---|---|
| `avr-gcc` | compiling | `gcc-avr` |
| `avr-objcopy` | `.hex` and `.bin` | `binutils-avr` |
| `avr-size` | the memory report | `binutils-avr` |
| `avr-libc` | headers + `crt1.o` | `avr-libc` |
| `cc` | host build of the protocol tests | `gcc` |
| `python3` | host tool + tests | system |
| `pyserial` | host tool | `pip install pyserial` |

`cc` is easy to miss: `make test-protocol` compiles `channels.c`, `proto.c` and
`reply.c` a second time as host code, so a cross-compiler alone is not enough to
run the tests.

Version used for development: avr-gcc 7.3.0 (Atmel), binutils 2.26. The Docker
image pins exactly these.

## V-USB

Vendored at `firmware/vendor/v-usb/` and **unmodified**, so
`git clone && make firmware` needs no network. Only `usbdrv/` is kept; see
`firmware/vendor/v-usb/VENDORED.md` for what was removed and why, and
`firmware/vendor/v-usb.UPSTREAM` for the commit.

**`firmware/vendor/v-usb/.git` must not exist.** If it does, git records the
directory as a submodule gitlink instead of the driver files — the commit
succeeds, the files are absent from history, and the next clone fails with a
missing `usbdrv.h`. `make test-vendor` fails loudly on that state, and the
re-vendor recipe is at the bottom of `.gitignore`.

Upstream ships **no CDC-ACM example** — obdev's documentation explicitly
discourages low-speed virtual COM ports — so the configuration in
`firmware/usbconfig.h` is written for this project. Use upstream's
`usbdrv/usbconfig-prototype.h` as the reference for any `USB_CFG_*` macro you
change; every macro there has a comment explaining the values.

## Building

```bash
make firmware              # or: make -C firmware
```

Output:

```
firmware/build/attiny85-sswitch.hex
firmware/build/attiny85-sswitch.bin
```

`make size` prints the memory report. Budget check:

```
Program:  5032 bytes (61.4% Full)
Data:      308 bytes (60.2% Full)
```

The app ceiling is not the full 8192 bytes of flash. micronucleus's `t85_default`
configuration sets `BOOTLOADER_ADDRESS = 0x1A00`, leaving **6656 bytes** for
the application. At 5032 there is about 1.6 kB of headroom.

SRAM is the tighter constraint: 512 bytes total, 308 used. That is why string
constants go to flash — on AVR a plain `"..."` in `.rodata` is copied to SRAM
at startup, and the format strings alone come to ~300 bytes. Every string in
the firmware goes through `reply_lit()` / `PSTR`. The first cut of this code did
not, and it did not link.

## Compiler flags that are not optional

```make
-funsigned-char -funsigned-bitfields
```

`char` is signed on AVR, which is not the compiler default for that target.
V-USB's `uchar` arithmetic depends on it. Drop these and the firmware compiles
cleanly and then misbehaves at runtime.

`-ffunction-sections -fdata-sections -Wl,--gc-sections` keeps unused code out
of a flash budget that is not generous.

## Fuses

The Digispark's factory fuses are already correct for this firmware. **Do not
change them.** For the record:

| Fuse | Value | Why |
|---|---|---|
| `lfuse` | `0xE1` | internal 64 MHz PLL ÷ 4, `CKDIV8` clear — this is the 16.5 MHz V-USB needs |
| `hfuse` | `0xDD` | `RSTDISBL` clear (PB5 stays RESET), `DWEN`, `SPIEN`, BOD 2.7 V |
| `efuse` | `0xFE` | `SELFPRGEN` — micronucleus needs to write flash |

Two things to note. `RSTDISBL` is clear, so PB5 is the reset pin and not a
channel. And BOD is enabled, which is what makes a brownout produce a clean
reset into the all-off state instead of a partial one.

## Flashing

Needs a Digispark with the stock micronucleus bootloader, which is what factory
boards ship with.

```bash
scripts/flash.sh
# or
make flash
```

Two ways in, depending on what you have:

**micronucleus** — talks to the bootloader directly over USB:

```bash
avr-objcopy -O ihex -R .eeprom firmware/build/attiny85-sswitch.elf main.hex
micronucleus --run main.hex
```

**avrdude + uisp** — for ISP programmers:

```bash
avrdude -p usb -c usisp -P /dev/ttyACM0 -U lfuse:w:0xe1:m \
        -U hfuse:w:0xdd:m -U efuse:w:0xfe:m \
        -U flash:w:main.hex:i
```

After flashing, unplug and replug. The firmware drops off the USB bus for
~255 ms on startup so the host re-reads the descriptors instead of trusting a
cached copy.

## OSCCAL

The ATtiny85's internal RC is ±10% out of the box; V-USB needs ±1%. micronucleus
calibrates at boot and stores the result in flash, so the application inherits a
calibrated oscillator and this firmware does no calibration of its own.

If you ever build this for a board *without* micronucleus, you must calibrate
yourself — see `libs-device/osccal.c` in the V-USB distribution. The symptom of
getting it wrong is a device that enumerates intermittently and never quite
reliably.

## Tests

```bash
make test
```

`make test-protocol` compiles the firmware sources a second time as host code
and checks protocol behaviour. `make test-cli` runs `tools/sswitch` against
`tools/protocol_sim.py`. Neither needs hardware. Neither can tell you the device
enumerates or moves a pin — see `tests/README.md`.
