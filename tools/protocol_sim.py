#!/usr/bin/env python3
"""
A stand-in for the attiny85-sswitch firmware.

This is a TEST DOUBLE, not an emulator and not a substitute for hardware.
It re-implements the line protocol from docs/protocol.md so the host tool and
its tests have something to talk to. It cannot tell you whether the firmware
compiles, enumerates, or moves a pin -- only a Digispark can do that.

It does pin down the two behaviours most likely to be got wrong:

  * channels P0 and P1 have PWM, P2 does not
  * HIGH and LOW are voltages, so they mean different switch states
    depending on the channel's polarity

The firmware's own protocol logic is tested separately, against the real
sources, in tests/protocol_test.c. What that cannot reach is the host side,
which is what this is for: it lets tools/sswitch be exercised end to end.

    python3 tools/protocol_sim.py            # serve on a fresh pty
"""

import os
import pty
import sys
import threading
import time

VERSION = "ATTINY85-SSWITCH 1.0"

# The template each Device copies. A process may host several Devices with
# different polarities at once, so this must never be mutated directly.
TEMPLATE = [
    {"pin": 0, "active_low": False, "pwm": True},
    {"pin": 1, "active_low": False, "pwm": True},
    {"pin": 2, "active_low": False, "pwm": False},
]

REJECT = {
    3: "USB D-",
    4: "USB D+",
    5: "RESET",
}

HELP = ("ON[ <pin>[ <duty>]] OFF <pin> HIGH <pin> LOW <pin> IN <pin> "
        "OUT <pin> READ <pin> PINS[ --raw] ALL OFF STATUS")


def static_level(c):
    """1 = HIGH, 0 = LOW, 2 = not a steady level (PWM or input)."""
    if c["mode"] == "IN":
        return 2
    if c["duty"] == 0:
        return 1 if c["active_low"] else 0
    if c["duty"] == 255:
        return 0 if c["active_low"] else 1
    return 2


def level_token(lvl):
    return {0: "LOW", 1: "HIGH"}.get(lvl, "PWM")


class Device:
    """One simulated switch. Owns its state; shares nothing."""

    def __init__(self, active_low=(False, False, False)):
        self.ch = [dict(c) for c in TEMPLATE]
        for c, al in zip(self.ch, active_low):
            c["active_low"] = al

    # ------------------------------------------------------------ helpers --
    def by_pin(self, n):
        return next(c for c in self.ch if c["pin"] == n)

    def reject(self, tok):
        if not tok.isdigit():
            return f"ERR PIN {tok}"
        n = int(tok)
        if n < len(self.ch):
            return None
        return f"ERR P{n} {REJECT.get(n, 'NO SUCH PIN')}"

    def state_str(self, c):
        if c["mode"] == "IN":
            return "IN - 0"
        lvl = level_token(static_level(c))
        return f"{'ON' if c['duty'] else 'OFF'} {lvl} {c['duty']}"

    def pins_line(self, raw):
        """
        Human:  P0 ON HIGH 255 | P1 ON PWM 10 | P2 OFF LOW 0
        Raw:    P0,ON,HIGH,255;P1,ON,PWM,10;P2,OFF,LOW,0

        Same field order in both; only the delimiters differ, so a script
        can parse either.
        """
        sep = ";" if raw else " | "
        out = []
        for c in self.ch:
            fields = self.state_str(c)
            out.append(f"P{c['pin']}" + ("," if raw else " ")
                       + (fields.replace(" ", ",") if raw else fields))
        return sep.join(out)

    # ----------------------------------------------------------- commands --
    def dispatch(self, line):
        t = line.strip().upper().split()
        if not t:
            return None

        if t[0] in ("HELP", "?"):
            return HELP
        if t == ["STATUS"]:
            return VERSION
        if t == ["PINS"]:
            return self.pins_line(False)
        if t == ["PINS", "--RAW"]:
            return self.pins_line(True)
        if t == ["ALL", "OFF"]:
            for c in self.ch:
                c["mode"], c["duty"] = "OUT", 0
            return "OK ALL OFF"

        verb = t[0]

        if verb == "ON":
            if len(t) < 2:
                return "ERR NEED A PIN"
            bad = self.reject(t[1])
            if bad:
                return bad
            c = self.by_pin(int(t[1]))
            if len(t) == 2:
                duty = 255
            elif len(t) == 3 and t[2].isdigit() and 1 <= int(t[2]) <= 255:
                duty = int(t[2])
            else:
                return "ERR DUTY MUST BE 1-255"
            if duty < 255 and not c["pwm"]:
                return f"ERR P{c['pin']} NO PWM"
            c["mode"], c["duty"] = "OUT", duty
            return f"OK P{c['pin']} {self.state_str(c)}"

        if verb in ("HIGH", "LOW"):
            if len(t) != 2:
                return f"ERR USAGE: {verb} <pin>"
            bad = self.reject(t[1])
            if bad:
                return bad
            c = self.by_pin(int(t[1]))
            c["mode"] = "OUT"
            # HIGH/LOW is a voltage. Which switch state that means is the
            # module's business, not the caller's.
            c["duty"] = 255 if ((verb == "HIGH") != c["active_low"]) else 0
            return f"OK P{c['pin']} {self.state_str(c)}"

        if verb in ("OFF", "IN", "OUT", "READ"):
            if len(t) != 2:
                return f"ERR USAGE: {verb} <pin>"
            bad = self.reject(t[1])
            if bad:
                return bad
            c = self.by_pin(int(t[1]))
            if verb == "OFF":
                c["mode"], c["duty"] = "OUT", 0
            elif verb == "IN":
                c["mode"] = "IN"
            elif verb == "OUT":
                c["mode"], c["duty"] = "OUT", 0
            else:
                return f"OK P{c['pin']} {level_token(static_level(c))}"
            return f"OK P{c['pin']} {self.state_str(c)}"

        return f'ERR UNKNOWN "{t[0]}"'


def serve(fd, device=None, stop=None):
    """
    Read protocol lines from fd, write one reply line each.

    A pty master whose slave has been closed raises OSError(EIO) rather than
    returning EOF, and the CLI opens and closes the port on every
    invocation. Treat that as "nobody is listening right now" and keep
    going, or the server dies on the first client disconnect and every
    subsequent test times out.
    """
    if device is None:
        device = Device()
    if stop is None:
        stop = threading.Event()

    buf = ""
    while not stop.is_set():
        try:
            chunk = os.read(fd, 256)
        except OSError:
            time.sleep(0.01)
            continue
        if not chunk:
            time.sleep(0.01)
            continue

        buf += chunk.decode("ascii", "replace")
        while "\n" in buf:
            line, buf = buf.split("\n", 1)
            reply = device.dispatch(line)
            if reply is not None:
                try:
                    os.write(fd, (reply + "\n").encode("ascii"))
                except OSError:
                    pass


def main():
    fd, _slave = pty.openpty()
    print(f"protocol simulator on {os.ttyname(fd)}  (Ctrl-C to stop)")
    print("channels: P0 P1 (PWM) P2 (on/off), all active-HIGH")
    try:
        serve(fd)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
