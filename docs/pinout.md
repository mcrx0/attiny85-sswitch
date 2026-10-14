# Pinout

The ATtiny85 has a **6-bit** port B — PB0 to PB5. There are no PB6/PB7. If you
find a pinout claiming otherwise, it is describing a different chip.

## Digispark header

| Pin | Port | Hardware function | Channel? | PWM |
|---|---|---|---|---|
| P0 | PB0 | USI DI, ADC0, OC0A | ✅ | ✅ OC0A |
| P1 | PB1 | USI DO, **onboard LED**, OC0B, OC1A | ✅ | ✅ OC0B |
| P2 | PB2 | USI SCK, ADC1, **INT0** | ✅ | ❌ |
| P3 | PB3 | **USB D−**, XTAL1, OC1B | ❌ | — |
| P4 | PB4 | **USB D+**, XTAL2, OC1D | ❌ | — |
| P5 | PB5 | **RESET** | ❌ | — |

**Three channels.** P3 and P4 carry USB and are bit-banged continuously; P5 is
the reset pin and would need `RSTDISBL`, which kills ISP.

The device says so rather than failing quietly:

```
$ sswitch on 3
ERR P3 USB D-
$ sswitch on 5
ERR P5 RESET
```

## Why P2 cannot do PWM

Compare outputs are fixed in silicon:

| Timer | Compare output | Port |
|---|---|---|
| Timer0 | OC0A | PB0 |
| Timer0 | OC0B | PB1 |
| Timer1 | OC1A | PB1 |
| Timer1 | OC1B / OC1D | PB4 / PB3 — **the USB pins** |

PB2 has none. Software PWM on P2 would need a timer interrupt, and V-USB's
bit-banging runs in a timing-critical pin-change interrupt where D+ lives. A
competing interrupt there costs enumeration reliability, which is not worth
1.6 kB of saved flash. `ON 2 120` returns `ERR P2 NO PWM`.

## Notes on individual pins

**P0** — the clean choice. PWM capable, nothing attached, not an interrupt
pin.

**P1** — the onboard LED is wired from PB1 through a resistor to ground. That
loads the pin when driving it high. The LED needs a few mA and so does a relay
module's input, so it works, but you are sharing current budget. Prefer P0/P2
for relays. Use P1 if you want the blink as confirmation.

**P2** — free and PWM-less. Also owns **INT0**, the highest-priority external
interrupt on the AVR. V-USB wants that priority for itself but is forced onto
pin-change interrupts anyway (D+ is on PB4, not INT0), so P2 as a plain output
is fine. Do not put anything on P2 that needs interrupts.

**P0 and AREF** — PB0 doubles as AIN0/AREF. Do not drive it high while the ADC
is configured against it. The firmware uses no ADC.

## Interrupts

P0–P4 are all `PCINT`-capable but share the single `PCINT0_vect` vector, which
V-USB's bit-bang assembly occupies. Adding your own pin-change interrupt means
reimplementing that vector and chaining. Do not.

## Boot state

All channels are driven to their de-energised level before USB initialises.
See [wiring.md](wiring.md) for the pull-up that makes this true during the reset
window, before the first instruction runs.

## Changing the polarity

One place, per channel:

```c
/* firmware/src/config.h */
#define CH0_ACTIVE_LOW 0   /* 0 = KY-019 (HIGH energises), 1 = opto module */
#define CH1_ACTIVE_LOW 0
#define CH2_ACTIVE_LOW 0
```

Then `make firmware`. `ON`/`OFF` mean energise/de-energise either way.
