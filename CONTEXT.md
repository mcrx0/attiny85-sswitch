# attiny85-sswitch — glossary

The words this project uses, and — more usefully — the words it deliberately
does not.

This is a vocabulary, not a specification. For behaviour see
[docs/protocol.md](docs/protocol.md); for wiring see
[docs/wiring.md](docs/wiring.md).

## The device

**Switch** — the thing as a whole: a Digispark running this firmware. One
switch, three channels.

**Channel** — one controllable pin. Exactly three exist: **P0**, **P1**,
**P2**. P1 carries the Digispark's onboard LED; P2 cannot do PWM.

**Port** — the host's handle on the switch: a `/dev/ttyACM*` node. Not to be
confused with the ATtiny85's hardware port B.

## The two things that are constantly confused

**Switch state** — `ON` or `OFF`. The *semantic* answer to "is the load
energised?"

**Pin level** — `HIGH` or `LOW`. The *physical* answer to "what voltage is on
the pad?"

These are different questions and they do not have the same answer. A KY-019
relay module energises when the pin goes HIGH. A blue optocoupler module
energises when the pin goes LOW. In both cases `ON` means the same thing.

The protocol keeps them apart: `ON`/`OFF` are decisions, `HIGH`/`LOW` are
voltages. Nothing in the command language requires the user to know which kind
of module they have. See
[ADR 0003](docs/adr/0003-always-off-boot-with-configurable-polarity.md).

**Polarity** — the mapping between the two above, fixed at compile time per
channel. Set by the relay module, not by the user, and never by a command.

**Duty** — 0–255, how much of the PWM period a channel is on. Not a percentage.
Full on is 255, off is 0.

## Talking to it

**Command** — one line sent to the switch. Terse tokens; the firmware knows
nothing about English.

**Reply** — exactly one line back per command. Never unsolicited, never an echo.
A switch that speaks when unasked cannot be scripted reliably.

**Set-state** — a command that assigns a value. Every command here is
set-state; there is no toggle anywhere. A toggle cannot be retried, and a
script that times out and re-sends would flip the relay back where it started.

**The language lives on the host.** `switch on pin 0` is `tools/sswitch`'s
doing. The firmware only ever sees `ON 0`. See
[ADR 0002](docs/adr/0002-host-owns-the-command-language.md).

## Words this project refuses to use

**Signal** — dropped. It was carrying three unrelated meanings: the command
being sent, the voltage on the pin, and the USB connection. Those are
**command**, **pin level**, and **port**.

**Toggle** — not offered. See *set-state*.

**Smart switch** — dropped as vacuous. It described an aspiration rather than a
capability.

## Safety vocabulary

**OFF** — the de-energised state. Guaranteed from the moment power rises,
through reset, and on watchdog expiry. Not "whatever the last command said".

**Boot state** — every channel OFF. Not a default that can be configured; there
is no EEPROM persistence, so a power cycle always returns to OFF. A relay that
silently comes back on after a power blip is a worse failure than one that
comes back off.

**Reserved pin** — a pin that exists on the chip but is not available: P3 and
P4 carry USB, P5 is RESET. Asking for one is an error that names the reason,
not a silent no-op, because "the pin exists but I cannot have it" and "that
was a typo" are different problems.
