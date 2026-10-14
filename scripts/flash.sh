#!/bin/sh
# Flash the firmware to a Digispark.
#
# Needs a Digispark with the stock micronucleus bootloader, which is what the
# factory boards ship with. No ISP programmer required.
#
# Unplug the board before running this, then plug it back in when it says so.

set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
elf="$here/firmware/build/attiny85-sswitch.elf"
hex="$here/firmware/build/attiny85-sswitch.hex"

# Use the repo-local toolchain if it is there.
if [ -x "$here/.toolchain/opt/usr/bin/avr-gcc" ]; then
    PATH="$here/.toolchain/opt/usr/bin:$PATH"
    export PATH
fi

if [ ! -f "$elf" ]; then
    echo "$elf not found. Run: make firmware" >&2
    exit 1
fi

command -v avr-size >/dev/null 2>&1 && avr-size --format=avr --mcu=attiny85 "$elf"

if command -v micronucleus >/dev/null 2>&1; then
    echo
    echo "==> flashing with micronucleus"
    echo "    unplug the Digispark now, plug it back in when prompted"
    # micronucleus wants ihex; make sure we have it.
    [ -f "$hex" ] || avr-objcopy -O ihex -R .eeprom "$elf" "$hex"
    micronucleus --run "$hex"
    exit 0
fi

if command -v avrdude >/dev/null 2>&1; then
    echo
    echo "==> micronucleus not found; trying avrdude"
    echo "    this path needs an ISP programmer, not just USB"
    avrdude -p usb -c usisp \
            -U lfuse:w:0xe1:m -U hfuse:w:0xdd:m -U efuse:w:0xfe:m \
            -U flash:w:"$hex":i
    exit 0
fi

cat >&2 <<EOF

error: neither micronucleus nor avrdude is on PATH.

  Digispark flashing over USB needs micronucleus:
      pip install --user micronucleus   (or your package manager)

  See docs/toolchain.md for the full toolchain.
EOF
exit 1
