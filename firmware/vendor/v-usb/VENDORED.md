# Vendored V-USB

Source: https://github.com/obdev/V-USB
Commit: see ../v-usb.UPSTREAM
Licence: GPL-2.0 or GPL-3.0, or a commercial licence from Objective Development.
         Full text in usbdrv/License.txt.

## Do not let a nested .git appear here

`firmware/vendor/v-usb/.git` must not exist. If it does, `git add` records the
directory as a submodule **gitlink** (mode 160000) instead of recording the
driver files. The commit succeeds. It looks fine. The files are not in history,
and a clone of that commit has an empty `vendor/v-usb/`, so the build fails
with `usbdrv.h: No such file or directory` and nothing pointing at the cause.

This is not hypothetical — it is the default outcome of re-cloning V-USB into
this path without removing `.git` afterwards.

```sh
make test-vendor     # fails loudly on this state
```

If you trip it:

```sh
rm -rf firmware/vendor/v-usb/.git
```

Then confirm the driver is still there (`ls firmware/vendor/v-usb/usbdrv/`) and
re-run `make test-vendor`.

The full re-vendor recipe is at the bottom of the repository `.gitignore`.

## Why it is vendored rather than fetched at build time

Two reasons, both learned the hard way:

1. **Reproducibility.** `git clone && make firmware` works with no network. A
   build step that clones from the internet is a build step that fails
   eventually, and the reference design this took its USB layer from has a
   Makefile that cannot build from a clean checkout for exactly that reason —
   its README claims it clones V-USB automatically and its Makefile does not.

2. **A known-good ATtiny85 copy.** Upstream's examples do not cover CDC-ACM,
   so the driver is configured here rather than copied from an example.

## What was removed

Only `usbdrv/` is kept — the driver itself, its assembly for 12/12.8/15/16/16.5/
18/20 MHz, `asmcommon.inc`, and the licences. Removed: `examples/`,
`circuits/`, `libs-device/`, `libs-host/`, `tests/`, `v-usb.xcodeproj/`,
`mkdist.sh`, and the top-level `Makefile` and `README.md`.

Nothing in the removed directories is compiled or referenced by
`firmware/Makefile`. `usbdrv/` itself is **unmodified** — diff against upstream
at the recorded commit to confirm.

`usbdrv/usbconfig-prototype.h` is kept even though it is not used directly,
because it is the reference documentation for every `USB_CFG_*` macro in
`firmware/usbconfig.h`.

## The config is ours, not theirs

`firmware/usbconfig.h` is an ATtiny85 CDC-ACM configuration written for this
project. Upstream ships no CDC example; obdev's documentation explicitly
discourages low-speed virtual COM ports. See `docs/adr/0001`.
