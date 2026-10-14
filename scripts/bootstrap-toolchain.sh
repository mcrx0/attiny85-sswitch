#!/bin/sh
# Fetch an AVR toolchain without root.
#
# Installs into ./.toolchain/opt and touches nothing outside this repo.
# Deliberately not `apt install`: this needs to work in a container, on a
# locked-down machine, and on a system where installing packages is not
# something you want to do for a 5 kB firmware.
#
# If you already have avr-gcc, avr-objcopy and avr-size on PATH, use those
# instead and skip this entirely.

set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
root="$here/.toolchain"
prefix="$root/opt"
debdir="$root/debs"

if [ -x "$prefix/usr/bin/avr-gcc" ]; then
    echo "toolchain already present at $prefix/usr/bin"
    echo
    echo "PATH=\"$prefix/usr/bin:\$PATH\" make firmware"
    exit 0
fi

for tool in apt-get dpkg curl; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: $tool is required but not installed" >&2
        exit 1
    }
done

case "$(uname -s)" in
    Linux) ;;
    *)
        echo "error: this script assumes Linux/apt-get. On other systems," >&2
        echo "       install gcc-avr, binutils-avr and avr-libc and skip this." >&2
        exit 1
        ;;
esac

mkdir -p "$debdir" "$prefix"

echo "==> downloading"
cd "$debdir"
apt-get download gcc-avr binutils-avr avr-libc

echo "==> unpacking into $prefix (no system changes)"
for deb in ./*.deb; do
    dpkg -x "$deb" "$prefix"
done

if [ ! -x "$prefix/usr/bin/avr-gcc" ]; then
    echo "error: avr-gcc missing after unpack" >&2
    exit 1
fi

echo "==> smoke test"
cd "$root"
printf 'int main(void){for(;;);}\n' > smoke.c
"$prefix/usr/bin/avr-gcc" -mmcu=attiny85 -Os -o smoke.elf smoke.c
rm -f smoke.c smoke.elf

echo
echo "done. build with:"
echo
echo "  PATH=\"$prefix/usr/bin:\$PATH\" make firmware"
echo
echo "or, from a shell that keeps PATH set:"
echo
echo "  export PATH=\"$prefix/usr/bin:\$PATH\""
