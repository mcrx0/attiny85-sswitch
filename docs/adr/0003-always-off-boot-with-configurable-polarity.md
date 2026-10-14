# 0003 — Always-OFF at boot, with polarity as configuration

Date: 2026-09-30

## Status

Accepted

## Consequences reviewed: **this is the one that will get "simplified" back into a bug**

Three separate decisions, each of which looks like dead weight on its own and
each of which exists because of a specific failure.

### 1. `PORTB` is written before `DDRB`

The ATtiny85 datasheet notes that *"the Port B pins are tri-stated when a reset
condition becomes active, even if the clock is not running"*. So between reset
and the first instruction of `main()`, the pin is floating.

The natural-looking code is wrong:

```c
DDRB  |= (1 << bit);   /* pin becomes an output, at its reset value */
PORTB  = off_level;    /* "fix" it now */
```

For a few cycles the pin drives the **reset** value, which is LOW. On an
active-LOW module, LOW is energised. So this ordering means a real, audible
relay click and a real contact closure **at every power-up**, before the host has
said a single word.

Correct order, in `apply()` in `firmware/src/channels.c`:

```c
PORTB = off_level;     /* while still an input: engages/disengages the pull-up */
DDRB  |= (1 << bit);   /* now drive off_level */
```

This is a one-line difference from the reference design and it is the whole
reason that design is unsafe on a mains load.

### 2. Every channel boots OFF, unconditionally

`channels_init()` sets every channel to duty 0 before `usbInit()` is called, so
the pins are safe before USB is even alive — and stay safe if USB never comes up.

The reference project boots at LOW = ON. That is defensible for its use case
(an HTC Vive EDL interrupt where ON is the resting state) and indefensible for
anything that switches power to something.

**This is a decision about your load, not about the firmware.** If the thing
you switch must be ON when the controller is dead, you want the opposite
polarity, and you should think hard about which failure you would rather have.
For anything on mains, off-when-dead is the only defensible default.

Note that this is the *default*, not an enforced invariant: `duty` is plain RAM
and nothing restores it. Power cycle → OFF. There is no EEPROM persistence, on
purpose — a relay that silently comes back on after a power blip is a worse
failure than one that comes back off.

### 3. Polarity is per-channel configuration, and the protocol does not leak it

Relay modules are not uniform, and the difference is not academic:

| Module | Trigger |
|---|---|
| KY-019, CW-019, HW-482 (bare SS8050) | **active-HIGH** |
| Blue PC817 opto modules, jumper on "Lo" | **active-LOW** |

Assume the wrong one and `OFF` energises the coil.

So the protocol separates two things that are usually conflated:

- `ON`/`OFF` are **decisions** — energise or de-energise.
- `HIGH`/`LOW` are **voltages** — what is on the pad.

Polarity is one constant per channel in `firmware/src/config.h`, and it affects
only three things: the boot level, the mapping from `HIGH`/`LOW` to a duty, and
what `PINS` reports. It is never something the caller has to remember.

`append_state()` reports the level alongside the state, so `sswitch on 0` on an
active-LOW channel answers `OK P0 ON LOW 255` — the polarity is visible in the
reply rather than buried in a config file three files away.

### 4. Watchdog

V-USB does all its bit-banging inside a single interrupt. A wedged `usbPoll()`
loop is unrecoverable without a power cycle, which is exactly when you least
want to reach for the cable.

8-second watchdog, `wdt_reset()` in the main loop. A reset lands in
`channels_init()`, so the worst case is the safe state. ~30 bytes.

### 5. DTR/RTS deliberately ignored

Tempting: treat host-disconnect as "turn everything off". Rejected. Every
Python `open()`/`close()` cycle toggles DTR, so a script that reopens the port
between commands would flap the relays it just set — turning a working script
into a machine that toggles its own outputs. The watchdog covers the real
failure (a hung device) without the false positives.

## Consequences

- Every relay click at power-up is a bug, not a feature.
- `PINS` after boot shows `OFF LOW 0` on an active-HIGH board, and
  `OFF HIGH 0` on an active-LOW one. Both are correct; both are OFF.
- Flashing the firmware always leaves the relays off.
- A `10 kΩ` pull-up in the wiring is not optional: it is what holds OFF during
  the reset window before `main()` runs. See `docs/wiring.md`.

## References

- Atmel-2586 §1.1.3 — *"The Port B pins are tri-stated when a reset condition
  becomes active, even if the clock is not running"*
- Atmel-2586 Table 21-x — `VOL`/`VOH` specified only at 10 mA; 40 mA absolute
  per-pin maximum
- <https://arduinomodules.info/ky-019-5v-relay-module/> — KY-019 is active-HIGH
- <https://handsontec.com/dataspecs/module/Relay%20Module/1Ch-relay%20Opto.pdf>
  — *"The digital inputs from Arduino are Active LOW"*
- `dmikushin/attiny_relay2` — the design this took its USB layer from, and whose
  boot sequence this deliberately does not copy
