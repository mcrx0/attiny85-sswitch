#!/bin/sh
# Verify the vendored V-USB is vendored.
#
# A nested .git inside firmware/vendor/v-usb/ does not produce a loud
# failure -- it produces a silent one, which is worse. `git add` converts the
# whole directory into a gitlink (mode 160000), the commit succeeds, and the
# driver files are simply absent from history. A clone of that commit has an
# empty vendor/v-usb/ and a build that fails with "no such file or directory"
# pointing at usbdrv.h.
#
# This is cheap to check and worth checking. Run it in CI.

set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
vendor="$here/firmware/vendor/v-usb"
upstream_file="$here/firmware/vendor/v-usb.UPSTREAM"

fail=0

if [ -d "$vendor/.git" ]; then
    cat >&2 <<'EOF'
FATAL: firmware/vendor/v-usb/.git exists.

  git will treat the directory as a submodule and record a gitlink instead of
  the driver files, so the next clone gets an empty vendor/v-usb/ and a build
  that fails for no visible reason.

  Fix:
      rm -rf firmware/vendor/v-usb/.git

  Then check that the driver is still present:
      ls firmware/vendor/v-usb/usbdrv/usbdrv.c
EOF
    fail=1
fi

if [ ! -d "$vendor" ]; then
    echo "FATAL: $vendor is missing." >&2
    echo "  V-USB is vendored on purpose so a clone builds with no network." >&2
    fail=1
fi

# A gitlink would show up here as an untracked-looking staged entry.
if git -C "$here" ls-files -s --error-unmatch firmware/vendor/v-usb 2>/dev/null \
   | grep -q '^160000'; then
    echo "FATAL: firmware/vendor/v-usb is recorded as a gitlink (mode 160000)." >&2
    echo "  The driver files are not in history. See the note above." >&2
    fail=1
fi

for required in usbdrv/usbdrv.c usbdrv/usbdrv.h usbdrv/usbdrvasm.S \
                usbdrv/usbdrvasm165.inc usbdrv/asmcommon.inc \
                usbdrv/License.txt; do
    if [ ! -f "$vendor/$required" ]; then
        echo "FATAL: $vendor/$required is missing." >&2
        fail=1
    fi
done

if [ ! -f "$upstream_file" ]; then
    echo "FATAL: firmware/vendor/v-usb.UPSTREAM is missing." >&2
    echo "  The vendored commit has to be recorded, or the copy cannot be audited." >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi

echo "vendor: OK (usbdrv/ present, no nested .git, commit $(cat "$upstream_file"))"
