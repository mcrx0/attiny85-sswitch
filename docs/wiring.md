# Wiring

The firmware only drives a pin. Everything else is your problem, and this page
exists because most of it is the kind of problem that works on the bench and
fails in the field.

## The relay module, not the relay

Do not connect a bare relay coil to a GPIO. An SRD-05VDC-SL-C draws 71–90 mA.
The ATtiny85 datasheet specifies `VOL`/`VOH` only at 10 mA and puts the absolute
per-pin maximum at 40 mA. That is 2–3× over the absolute maximum.

Use a module with its own transistor. Both of these are fine to drive directly
from a GPIO — the pin only sources 1–2 mA of base or opto bias:

| Module | Topology | Trigger |
|---|---|---|
| KY-019 / CW-019 / HW-482 | bare SS8050 NPN, no opto | **active-HIGH** |
| Blue 1-channel opto modules | PC817 + driver, jumper selects Lo/Hi | **active-LOW** when the jumper is on "Lo" |

This is the single most common mistake with relay boards, and it is
backwards-enough to be dangerous: if you assume active-HIGH and your module is
active-LOW, `OFF` energises the coil. Set the polarity to match:

```c
/* firmware/src/config.h */
#define CH0_ACTIVE_LOW 0   /* KY-019: energises when the pin goes HIGH */
```

`ON`/`OFF` then always mean energise/de-energise regardless. `HIGH`/`LOW`
always mean a voltage regardless. `PINS` reports the level, so you can see
which way your board went.

## The 10 kΩ pull-up — do not skip this

Between the channel pin and VCC, 10 kΩ:

```
     VCC (5V)
      │
     10kΩ
      │
      ├────────── relay module control input
      │
      ├────────── Digispark P0/P1/P2
```

From the ATtiny85 datasheet: *"The Port B pins are tri-stated when a reset
condition becomes active, even if the clock is not running."* Between power-up
and the first instruction of `main()`, your pin is floating. Without a pull-up
the relay state during that window is genuinely undefined — and for an
active-LOW module, floating can mean energised.

With the pull-up, the module reads HIGH (de-energised) for the whole reset
window. On the firmware side, `apply()` in `channels.c` then writes `PORTB`
before `DDRB` so the pin drives its OFF level from the moment it becomes an
output, with no pass-through of the reset value.

## Power

The Digispark takes 5 V from USB. Relay coils go on the same rail.

| | |
|---|---|
| One SRD-05VDC relay | ~90 mA — fine, USB 2.0 allows 500 mA |
| Four-channel opto module | ~350 mA of coil, plus opto bias — close to the limit |

The USB descriptor declares 100 mA (`USB_CFG_MAX_BUS_POWER 100`, whose value
is in milliamperes; V-USB halves it because `bMaxPower` counts 2 mA units).
Three coils at ~90 mA is ~270 mA, which is well over what is declared. A
well-behaved host port will hold you to the declared figure, so expect a
brown-out with all three relays energised — and the attiny85 will reset and put
them all back OFF. If that is the behaviour you want, good. If not, use a
separate supply for the coils and share only ground — and then the pull-up
matters more, because the module is no longer held up by the Digispark's rail.

The coil current is the whole USB budget story. The microcontroller itself
draws single-digit milliamperes.

## The current path

The ATtiny85 is a switch controller, not a switch. It carries no coil current
and never has:

```
   Digispark P0 ──[1kΩ]──┬── relay module control input
                          │
                        10kΩ        ← the pull-up, above
                          │
                         VCC
```

Inside the module, and only in a module that has one:

```
   VCC ───────┬──────────── relay coil ─────┬────── drain
              │                              │
              └──────────|<|───────────────  ← flyback diode
                             gate
                               │
   P0 ──[1kΩ]──[gate resistor]┴── source ── GND
              100 Ω gate
```

So the GPIO drives a gate or an opto LED, at roughly 1–2 mA. The MOSFET is the
element that interrupts the coil current; the relay is the actuator that moves
the contact. The firmware writes a bit and is otherwise out of the picture.

The 1 kΩ gate resistor is not optional. A MOSFET gate is a capacitor, and the
ATtiny85's output driver has finite rise time; without the resistor the gate
edge rings on the module's wiring and can latch the MOSFET half-on. 100 nΩ at
the gate, close to the transistor, damps it.

## Flyback

The diode goes **across the coil**, cathode to +5 V, anode to the low side. It
does not go across the relay contacts — see [Mains](#mains).

- **KY-019 has a 1N4007 fitted.** Nothing to add.
- **Opto modules have one** (1N4148 or 1N4007). Nothing to add.
- **A bare relay needs one you add**. At 71–90 mA coil current both 1N4148 and
  1N4007 are adequate; 1N4007 if you want margin.

When the switch opens, the coil's current has nowhere to go and the voltage
across it reverses and climbs until something gives: the diode conducts and
provides a recirculation path. Without it you get a spike of `V = L·di/dt`
into the hundreds of volts, which does three things at once:

1. **Arcs across the MOSFET.** It was turned off, so the drain is at whatever
   voltage the spike drives it to. A MOSFET does not avalanche gracefully; it
   fails short, and it fails at the moment you were trying to switch something
   off.
2. **Back-powers the microcontroller.** The spike couples back through the
   FET's drain-source capacitance and up the gate resistor into the pin, where
   the ATtiny85's clamp diodes can inject current into the dead VCC rail. This
   is how a switching transient becomes a brown-out reboot.
3. **Radiates.** The edge is nanoseconds wide and couples straight into the USB
   pair, which is a differential antenna. Symptom is enumeration flapping
   while a relay switches.

The practical note: the diode slows coil release, because current now decays
through the diode instead of collapsing instantly. Time constant is roughly
`L / (R_coil + R_diode)`, a few milliseconds for a typical small relay, on top
of the contact bounce already present. Irrelevant for a relay switched on human
timescales; relevant if you are timing contact transitions. A Zener in series
with the diode restores most of the speed at the cost of clamping to
`VCC + Vz + Vf` instead of `VCC + Vf` — worth doing only if you have measured
the slow release to be a problem.

## Which pin to use

P1 has the Digispark's onboard LED wired to it, which loads the pin. It works,
but prefer **P0 or P2** for relays and use P1 for something that does not care.

P2 has no PWM. If you need dimming, use P0 or P1.

## Mains

If any of this switches mains: the relay contacts, the wiring, and the
enclosure all matter far more than the firmware. The firmware's contribution
to your safety case is only this: the relay is OFF from reset, and the
watchdog means a wedged USB stack cannot leave it stuck ON.

## First power-up

1. **Do not plug the relay in yet.** Power the Digispark alone.
2. `dmesg | tail` — you want `cdc_acm` and a new `/dev/ttyACM*`.
3. `tools/sswitch status` should answer `ATTINY85-SSWITCH 1.0`.
4. `tools/sswitch pins` should show all three channels `OFF LOW 0`. If it does
   not, stop: your polarity is wrong for your module, or you have the wrong
   module.
5. **Now** wire the relay. `tools/sswitch on 0`. Listen for the click.

On the first real reset with a mains-connected relay, keep your hand near the
power switch.
