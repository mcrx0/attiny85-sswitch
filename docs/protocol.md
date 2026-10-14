# The protocol

The contract between a host and an attiny85-sswitch. If you write something
that speaks this, it works.

## Framing

- One command per line, terminated by `\n` (`\r\n` is accepted).
- Exactly one reply line per command, also `\n`-terminated.
- The device **never speaks unsolicited** and **never echoes**. No banner, no
  greeting, nothing. If bytes arrive that you did not ask for, something is
  wrong — which is a useful property, not a limitation.
- Baud rate is meaningless. CDC has no UART behind it and the device ignores
  `SET_LINE_CODING`. Open the port at anything.

Case-insensitive. Tokens separated by spaces or tabs, any number.

## Vocabulary

| Term | Means | Never means |
|---|---|---|
| switch state | `ON` / `OFF` — is the load energised | a voltage |
| pin level | `HIGH` / `LOW` — the voltage on the pad | a decision |
| duty | 0–255 — how much of the period it is ON | a percentage (0–255, not 0–100) |
| polarity | compile-time: does this module energise on LOW or HIGH | anything the host sets |

`ON` is not `HIGH`. On an active-LOW module `ON` means the pin is LOW. The
protocol does not care which your board is; it is one constant in
`firmware/src/config.h`.

## Commands

| Command | Reply | Notes |
|---|---|---|
| `ON <p>` | `OK P<p> ON HIGH 255` | full on |
| `ON <p> <duty>` | `OK P<p> ON PWM 120` | duty 1–255 |
| `OFF <p>` | `OK P<p> OFF LOW 0` | |
| `HIGH <p>` | `OK P<p> ON HIGH 255` | a *voltage*; see below |
| `LOW <p>` | `OK P<p> OFF LOW 0` | a *voltage*; see below |
| `IN <p>` | `OK P<p> IN - 0` | pin becomes an input, pull-up on |
| `OUT <p>` | `OK P<p> OFF LOW 0` | always re-enters OFF |
| `READ <p>` | `OK P<p> HIGH` | actual level, not the last command |
| `PINS` | `P0 ON HIGH 255 \| P1 OFF LOW 0 \| P2 IN - 0` | |
| `PINS --raw` | `P0,ON,HIGH,255;P1,OFF,LOW,0;P2,IN,-,0` | |
| `ALL OFF` | `OK ALL OFF` | |
| `HELP` / `?` | one line listing the above | |
| `STATUS` | `ATTINY85-SSWITCH 1.0` | |

### `HIGH` and `LOW` are voltages

These set the pin, not the decision. On a channel configured active-HIGH,
`HIGH 0` energises the load; on one configured active-LOW it de-energises it.
Both are static levels — PWM is switched off — so `READ` reports the truth
immediately.

### No toggle

There is no `TOGGLE`, and that is deliberate. A toggle cannot be retried: a
script that times out and re-sends flips the relay back to where it started.
Every command here assigns a value, so re-sending is always safe.

### `PINS` formats

Same field order in both, only the delimiters differ:

```
P0 ON HIGH 255 | P1 ON PWM 10 | P2 OFF LOW 0      human
P0,ON,HIGH,255;P1,ON,PWM,10;P2,OFF,LOW,0        machine
```

Fields are `pin, state, level, mode, duty`. `level` is `HIGH`, `LOW`, or `PWM`
(not a steady level). In `IN` mode, state is `IN` and level is `-`.

## Errors

Every failure is `ERR <reason>`, and **an `ERR` changes nothing** — a rejected
command never leaves a channel half-changed.

| Reply | Meaning |
|---|---|
| `ERR P3 USB D-` | the pin exists, and here is why it is not available |
| `ERR P4 USB D+` | ditto |
| `ERR P5 RESET` | ditto |
| `ERR P9 NO SUCH PIN` | no such pin at all |
| `ERR PIN abc` | not a number; echoed back verbatim |
| `ERR NEED A PIN` | `ON` with no pin |
| `ERR DUTY MUST BE 1-255` | out of range, or `ON 0 0` |
| `ERR P2 NO PWM` | P2 has no compare output; see below |
| `ERR USAGE: ON <pin> [duty]` | wrong argument count |
| `ERR UNKNOWN "FROBNICATE"` | unknown verb, echoed |
| `ERR LINE TOO LONG` | over 47 characters |
| `ERR TOO MANY ARGUMENTS` | over 4 tokens |

Note the difference between `ERR P4 USB D+` and `ERR P9 NO SUCH PIN`: the first
means the pin is real and unavailable, the second means you made a typo. Both
are useless if collapsed into one message.

An `ERR` is a *rejected command*, not a broken link. A whitespace-only line
replies `ERR UNKNOWN ""` rather than nothing, because a host waiting one reply
per line cannot tell silence from a hang.

## Why P2 has no PWM

The ATtiny85's PWM compare outputs are fixed in silicon: OC0A is PB0, OC0B is
PB1, and the other two are on PB3 and PB4 — the USB pins. PB2 has none.

Getting PWM onto PB2 would mean a timer interrupt, and V-USB's bit-banging
lives in a timing-critical pin-change interrupt. A competing interrupt there
costs enumeration reliability. So `ON 2 120` returns `ERR P2 NO PWM` and P2 is
on/off only.

## Language on the wire is terse on purpose

The firmware implements these tokens and nothing more. English
(`switch on pin 0`) is the client tool's job, in `tools/sswitch`. That keeps a
few hundred bytes out of the firmware and means a raw `echo` is a complete
client:

```bash
echo "ON 0" > /dev/ttyACM0
```

## Scripting

`tools/sswitch` exits 0 unless the device said `ERR` or said nothing, and 2 if
it could not talk to the device at all:

```bash
sswitch off 0 || echo "failed to switch off"
```

For direct use:

```bash
exec 3<>/dev/ttyACM0
stty -F /dev/ttyACM0 raw -echo
printf 'ON 0\n' >&3 && head -1 <&3     # OK P0 ON HIGH 255
```
