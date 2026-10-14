# Third-party licences

attiny85-sswitch itself is GPL-2.0-or-later; see [LICENSE](../LICENSE).

This file records what comes from where, because the obligations differ
sharply depending on whether a dependency is **redistributed** in this repo or
merely **used to build it**. Getting that distinction wrong is how a project
ends up either shipping a GPL-3 library's full text without saying so, or
claiming obligations it does not have.

## Summary

| Dependency | Licence | Role | Redistributed here? |
|---|---|---|---|
| [V-USB](#v-usb) | GPL-2.0-or-GPL-3.0, or commercial | USB driver | **yes** — `firmware/vendor/v-usb/` |
| avr-libc | BSD-3-Clause | C library, headers | no — links `strcmp.o` |
| libgcc (`gcc-avr`) | GPL-3.0+ with the GCC Runtime Library Exception | runtime helpers | no — links `_mulhi3.o`, `_udivmodhi4.o` |
| avr-gcc | GPL-3.0+ | compiler | no |
| binutils-avr | GPL-3.0+ | `avr-objcopy`, `avr-size` | no |
| Python 3 | PSF-2.0 | runs the host tool and tests | no |
| pyserial | BSD-3-Clause | host tool serial I/O | no |

---

## Redistributed

### V-USB

**Upstream:** <https://github.com/obdev/V-USB> — Objective Development GmbH
**Vendored commit:** [`firmware/vendor/v-usb.UPSTREAM`](../firmware/vendor/v-usb.UPSTREAM)
**Full licence text:** [`firmware/vendor/v-usb/usbdrv/License.txt`](../firmware/vendor/v-usb/usbdrv/License.txt)
**Commercial alternative:** Objective Development sells a V-USB commercial
licence. If you need one, that is their business, not ours.

This is the dependency that decides our licence. V-USB is GPL-2.0-or-GPL-3.0-or-commercial,
so a project that links it is GPL — there is no way to publish this firmware
under a permissive licence without replacing V-USB or buying the commercial
licence.

`usbdrv/` is vendored **unmodified**, with its licence and documentation intact.
See [`VENDORED.md`](../firmware/vendor/v-usb/VENDORED.md) for what was removed
and how to re-vendor. `make test-vendor` fails if the directory ever regains a
nested `.git`, which would silently turn it into an empty submodule.

#### Attribution, per obdev's terms

Beyond the GPL itself, `usbdrv/License.txt` asks that anyone distributing V-USB:

- **(1)** publish the project on a website and send obdev the URL via
  <http://www.obdev.at/vusb/feedback.html>
- **(2)** include at least a circuit diagram, the full host-side source, an
  ASCII `Readme.txt` describing the project and its layout, and a reference to
  <http://www.obdev.at/vusb/>

Only **(2)**'s reference is fully satisfied: this README and `docs/` describe
the project and where everything lives, and the reference appears there.

**(2)**'s *circuit diagram* is **not** included — there is no KiCad or PDF
schematic in this repo. Wiring is documented as text in
[`docs/wiring.md`](../docs/wiring.md), which is sufficient to build the thing
but is not what obdev asked for. If you want to be strictly compliant, add a
schematic.

**(1)**'s feedback form is polite rather than mandatory, but it is one request
and it is the courteous thing to do before publishing.

**(3)** asks that improvements to the driver itself be offered to obdev for
their commercial licence. We have made none: `usbdrv/` is untouched, and our
own changes live in `firmware/usbconfig.h` and `firmware/src/`. If you modify
the driver itself, you are on your own with obdev.

#### Shared USB VID/PID

We use **VID `0x16c0`, PID `0x05e1`** — obdev's shared pair reserved for
CDC-ACM class devices. Those are conditional on rules in
[`USB-IDs-for-free.txt`](../firmware/vendor/v-usb/usbdrv/USB-IDs-for-free.txt),
and two of them are currently **not** met:

1. The manufacturer string must contain a domain name or e-mail address that
   **you** own. `USB_CFG_VENDOR_NAME` in `firmware/usbconfig.h` is still the
   `example.invalid` placeholder. **This must be fixed before publishing.**
2. Devices sharing an ID must be distinguished by textual manufacturer and
   product string, not by VID/PID alone. `tools/sswitch` matches on VID/PID
   and offers `--port` to disambiguate, which is the honest behaviour for now
   but does not satisfy the rule on its own.

Using someone else's manufacturer string is the easy way to end up with two
identical-looking devices on one bus, and it is not what the ID was shared for.

---

## Used to build, not redistributed

These are installed by `scripts/bootstrap-toolchain.sh` (from Debian packages,
into `.toolchain/`, which is gitignored) or by your package manager. None of
them is committed to this repository.

### avr-libc — BSD-3-Clause

Microchip/Atmel's standard C library for AVR. Permissive, and
`strcmp.o` from it **is linked into the firmware binary**. The headers in
`<avr/…>` come from here. Licence: BSD-3-Clause.

### libgcc (from gcc-avr) — GPL-3.0+ with the GCC Runtime Library Exception

`_mulhi3.o` and `_udivmodhi4.o` are linked into the firmware binary. These are
GCC runtime helpers, not part of the GPL'd compiler proper.

This is worth stating precisely because it is the one place a GPL-3 component
touches a GPL-2 project. It is compatible **because of the GCC Runtime Library
Exception**, which grants the permission to link these routines into a
non-GPL-3 program without the result becoming GPL-3. Without that exception
the licence analysis would be different. The exception is upstream GCC's; note
that Debian's `gcc-avr` copyright file records the licence set as GPL-3+ and
does not restate the exception, so if you are auditing this formally, read
`COPYING.RUNTIME` in the GCC source tree.

### avr-gcc and binutils-avr — GPL-3.0+

The compiler and the binutils. Neither ends up in the firmware binary — they
produce it. Their GPL does not reach the output.

### Python 3 — PSF-2.0

Runs `tools/sswitch` and the test suite.

### pyserial — BSD-3-Clause

Serial I/O for `tools/sswitch` and `tools/protocol_sim.py`. Not vendored;
`pip install pyserial` or your distribution's `python3-serial`. Permissive,
and no attribution obligation beyond keeping the notice in a redistribution.

---

## How to verify these claims

Everything above was read off the actual artefacts rather than recalled:

```sh
# Licences of the build toolchain, as Debian records them
grep -E '^License:' .toolchain/opt/usr/share/doc/{avr-libc,binutils-avr,gcc-avr}/copyright \
  | sort | uniq -c | sort -rn

# What is actually linked into the firmware -- every archive member pulled in
avr-gcc -mmcu=attiny85 -Wl,--gc-sections -Wl,-Map=build/link.map -o /dev/null \
  firmware/build/src/*.o firmware/build/vendor/v-usb/usbdrv/*.o
grep -oE '[a-z0-9_]+\.a\([^)]+\)' build/link.map | sort -u

# The vendored licence, first party
head -25 firmware/vendor/v-usb/usbdrv/License.txt
```

The link map lists seven archive members:

```
libc.a(strcmp.o)
libgcc.a(_clear_bss.o)  libgcc.a(_copy_data.o)  libgcc.a(_exit.o)
libgcc.a(_mulhi3.o)     libgcc.a(_udivmodhi4.o)
```

`_clear_bss`, `_copy_data` and `_exit` are startup and teardown, always
present. The other two are there because avr-gcc emits `__mulhi3` and
`__udivmodhi4` calls for 16-bit multiply and divide — `reply_u()` divides by
ten, for one.

If you change a dependency, re-run these rather than trusting the table above.