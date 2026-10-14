# 0002 — The host owns the command language, not the firmware

Date: 2026-09-30

## Status

Accepted

## Context

The device is meant to be driven from a serial prompt. "SWITCH ON pin 0" reads
well typed by a human at a terminal. That suggests parsing English in the
firmware.

Against it:

- The firmware has **512 bytes of SRAM** and about 5 kB of usable flash. Space
  is not abundant.
- The command language is the part most likely to change. Someone will want
  `toggle`, or aliases, or a `set all off`. Every one of those is a reflash of
  hardware somebody has to physically reach.
- Parsing `switch on pin 0` and `on 0` in firmware costs tokens, branches and
  string tables. ~300 bytes of SRAM is the difference between linking and not
  here, because on AVR every string constant is copied to RAM at startup unless
  it is explicitly put in flash.

For it:

- It is a nicer experience to type.
- Someone will write a script against whatever the device speaks.

## Decision

**The firmware speaks terse tokens only. The host tool speaks English.**

```
firmware   ON 0        OFF 0       HIGH 0      LOW 0
           IN 0        OUT 0       READ 0      PINS [--raw]
           ALL OFF     HELP        STATUS

tools/sswitch   switch on pin 0  ->  ON 0
                on 0             ->  ON 0
                dim 1 120        ->  ON 1 120
```

Both paths are complete and neither is a fallback. `echo "ON 0" > /dev/ttyACM0`
is a fully supported client that happens to have no dependencies.

The device replies in the same terse form, so a script that uses `sswitch` and
a script that writes to the port directly parse the same replies.

## Consequences

**The shell redirect works.** The primary use case has no dependencies at all,
which was the point of [0001](0001-usb-cdc-acm-over-vendor-specific-or-hid.md).

**Changing the language is a host-side change.** Aliases, new verbs, nicer
output formats — all in `tools/sswitch`, no reflash.

**English is not available from `echo`.** `echo "switch on pin 0" > /dev/ttyACM0`
gets `ERR UNKNOWN "SWITCH"`. That is the cost, and it is paid in the place where
it costs least: the conveniences live in the layer that is easy to change.

**Unknown protocol commands stay reachable** through `sswitch raw "..."`, so the
firmware can grow a verb without the tool having to learn about it first.

**The device never echoes.** A terminal opened on a raw port has echo off, so
`sswitch repl` echoes locally. Otherwise your keystrokes would be invisible.

## Alternatives reconsidered

Parsing English in firmware would have cost roughly 300 bytes of SRAM and made
the language host-side forever, with a reflash per change. On a device with
512 bytes of RAM and a bootloader eating 1.5 kB of flash, spending the scarce
resource on the part that changes most is the wrong trade.

A compromise — a few aliases in firmware, English in the tool — was rejected
because two places to implement the same language is one more than is needed.
