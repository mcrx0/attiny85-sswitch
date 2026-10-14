#!/usr/bin/env python3
"""
End-to-end tests for tools/sswitch, run against tools/protocol_sim.py.

These verify the HOST side: argument handling, English translation, port
handling, exit codes. They cannot verify the firmware -- that needs a
Digispark. See tests/README.md for what to check on hardware.

    python3 -m unittest discover -s tests -v
"""

import io
import os
import pty
import subprocess
import sys
import threading
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SSWITCH = os.path.join(ROOT, "tools", "sswitch")

sys.path.insert(0, os.path.join(ROOT, "tools"))

import protocol_sim  # noqa: E402

# tools/sswitch has no .py extension, so load it by path.
import importlib.util  # noqa: E402

_spec = importlib.util.spec_from_loader(
    "sswitch_mod", importlib.machinery.SourceFileLoader("sswitch_mod", SSWITCH)
)
sswitch_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sswitch_mod)


def run(port, *args, expect_ok=None):
    """Invoke the CLI, return (exit_code, stdout)."""
    proc = subprocess.run(
        [sys.executable, SSWITCH, "--port", port] + list(args),
        capture_output=True, text=True, timeout=20,
    )
    if expect_ok is not None and proc.returncode != expect_ok:
        raise AssertionError(
            f"{args} -> exit {proc.returncode}, expected {expect_ok}\n"
            f"stdout: {proc.stdout!r}\nstderr: {proc.stderr!r}"
        )
    return proc.returncode, proc.stdout.strip()


class Sim:
    """A running simulator on a pty, plus the device object it drives."""

    def __init__(self, active_low=(False, False, False)):
        self.master, self.slave = pty.openpty()
        self.port = os.ttyname(self.slave)
        self.device = protocol_sim.Device(active_low)
        self.stop = threading.Event()
        self.thread = threading.Thread(
            target=protocol_sim.serve,
            args=(self.master, self.device, self.stop),
            daemon=True,
        )
        self.thread.start()

    def close(self):
        self.stop.set()
        os.close(self.master)
        os.close(self.slave)


class TestSswitch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sim = Sim()
        cls.port = cls.sim.port

    @classmethod
    def tearDownClass(cls):
        cls.sim.close()

    def setUp(self):
        run(self.port, "all-off")

    # ---------------------------------------------------------- switching --
    def test_on_off_roundtrip(self):
        _, out = run(self.port, "on", "0", expect_ok=0)
        self.assertEqual(out, "OK P0 ON HIGH 255")
        _, out = run(self.port, "off", "0", expect_ok=0)
        self.assertEqual(out, "OK P0 OFF LOW 0")

    def test_off_is_idempotent(self):
        """A retried command must not flip the state back. No toggles."""
        run(self.port, "on", "0")
        for _ in range(3):
            _, out = run(self.port, "on", "0")
            self.assertEqual(out, "OK P0 ON HIGH 255")

    def test_dim(self):
        _, out = run(self.port, "dim", "0", "120", expect_ok=0)
        self.assertEqual(out, "OK P0 ON PWM 120")

    def test_p2_has_no_pwm(self):
        _, out = run(self.port, "dim", "2", "120", expect_ok=1)
        self.assertEqual(out, "ERR P2 NO PWM")

    def test_p2_still_does_full_on(self):
        _, out = run(self.port, "on", "2", expect_ok=0)
        self.assertEqual(out, "OK P2 ON HIGH 255")

    # ------------------------------------------------------------ polarity --
    def test_high_is_a_voltage_not_a_decision(self):
        """On an active-LOW channel, HIGH means the switch is OFF."""
        sim = Sim(active_low=(True, False, False))
        try:
            _, out = run(sim.port, "on", "0", expect_ok=0)
            self.assertEqual(out, "OK P0 ON LOW 255")
            _, out = run(sim.port, "high", "0", expect_ok=0)
            self.assertEqual(out, "OK P0 OFF HIGH 0")
            _, out = run(sim.port, "low", "0", expect_ok=0)
            self.assertEqual(out, "OK P0 ON LOW 255")
        finally:
            sim.close()

    # ------------------------------------------------------------ rejection --
    def test_rejects_usb_pins_with_a_reason(self):
        for pin, why in ((3, "USB D-"), (4, "USB D+"), (5, "RESET")):
            _, out = run(self.port, "on", str(pin), expect_ok=1)
            self.assertEqual(out, f"ERR P{pin} {why}")

    def test_rejects_nonsense_pin(self):
        # The firmware upper-cases the whole line before parsing, so the
        # echoed token comes back upper-cased too.
        _, out = run(self.port, "on", "banana", expect_ok=1)
        self.assertEqual(out, "ERR PIN BANANA")

    def test_unknown_command(self):
        _, out = run(self.port, "raw", "FROBNICATE", expect_ok=1)
        self.assertEqual(out, 'ERR UNKNOWN "FROBNICATE"')

    def test_duty_out_of_range(self):
        _, out = run(self.port, "dim", "0", "300", expect_ok=1)
        self.assertEqual(out, "ERR DUTY MUST BE 1-255")

    # --------------------------------------------------------------- reads --
    def test_pins_human_and_raw(self):
        run(self.port, "on", "0")
        run(self.port, "dim", "1", "10")
        _, out = run(self.port, "pins", expect_ok=0)
        self.assertEqual(out, "P0 ON HIGH 255 | P1 ON PWM 10 | P2 OFF LOW 0")
        _, out = run(self.port, "pins", "--raw", expect_ok=0)
        self.assertEqual(out, "P0,ON,HIGH,255;P1,ON,PWM,10;P2,OFF,LOW,0")

    def test_read(self):
        _, out = run(self.port, "read", "0", expect_ok=0)
        self.assertEqual(out, "OK P0 LOW")

    def test_in_out(self):
        _, out = run(self.port, "in", "0", expect_ok=0)
        self.assertEqual(out, "OK P0 IN - 0")
        _, out = run(self.port, "out", "0", expect_ok=0)
        self.assertEqual(out, "OK P0 OFF LOW 0")

    def test_all_off(self):
        run(self.port, "on", "0")
        run(self.port, "on", "1")
        _, out = run(self.port, "all-off", expect_ok=0)
        self.assertEqual(out, "OK ALL OFF")
        _, out = run(self.port, "pins", expect_ok=0)
        self.assertEqual(out, "P0 OFF LOW 0 | P1 OFF LOW 0 | P2 OFF LOW 0")

    def test_status(self):
        _, out = run(self.port, "status", expect_ok=0)
        self.assertEqual(out, "ATTINY85-SSWITCH 1.0")


class TestPortErrors(unittest.TestCase):
    """
    The three ways opening a port fails, and what the user is told.

    A raw pyserial exception repeats the errno text twice and names no fix,
    which is a bad thing to read when you cannot work out whether you need
    to plug something in, close something, or change group membership.
    """

    def check(self, text, must_contain):
        msg = sswitch_mod.explain_port_error("/dev/ttyACM0", Exception(text))
        for fragment in must_contain:
            self.assertIn(fragment, msg, f"{text!r} -> {msg!r}")

    def test_not_plugged_in(self):
        self.check("[Errno 2] No such file or directory: '/dev/ttyACM0'",
                   ["does not exist", "plugged in"])

    def test_no_permission(self):
        self.check("could not open port /dev/ttyACM0: [Errno 13] "
                   "Permission denied", ["permission denied", "dialout"])

    def test_port_busy(self):
        self.check("could not open port /dev/ttyACM0: [Errno 11] "
                   "Resource temporarily unavailable",
                   ["in use", "minicom"])

    def test_errno_is_not_repeated_twice(self):
        msg = sswitch_mod.explain_port_error(
            "/dev/ttyACM0", Exception("No such file or directory"))
        self.assertNotIn("Errno", msg)

    def test_unrecognised_error_still_passes_through(self):
        msg = sswitch_mod.explain_port_error("/dev/ttyACM0", Exception("wat"))
        self.assertIn("wat", msg)


def _open(port, timeout=0.2):
    """Open a port the way the CLI does."""
    return sswitch_mod.open_port(port, timeout)


class TestDeviceSilence(unittest.TestCase):
    """A device that stops answering must not hang or loop forever."""

    def silent_port(self):
        """
        A port that accepts writes and never answers.

        This is what an unplugged or crashed device looks like from the host
        side, and it is a different failure from "the port would not open":
        the write succeeds, the read times out. Using a pty whose master end
        is still open gives exactly that -- the write is absorbed, and the
        read blocks until the timeout.
        """
        import serial as _s
        master, slave = pty.openpty()
        s = _s.Serial(os.ttyname(slave), timeout=0.05, write_timeout=0.05)
        return s, master, slave

    def capture(self, fn):
        """Run fn, returning (exit_code, stdout, stderr)."""
        out, err = io.StringIO(), io.StringIO()
        real_out, real_err = sys.stdout, sys.stderr
        sys.stdout, sys.stderr = out, err
        try:
            code = fn()
        finally:
            sys.stdout, sys.stderr = real_out, real_err
        return code, out.getvalue(), err.getvalue()

    def test_run_one_reports_silence(self):
        s, master, slave = self.silent_port()
        try:
            code, out, err = self.capture(
                lambda: sswitch_mod.run_one(s, "ON 0", 0.2))
        finally:
            s.close()
            os.close(master)
            os.close(slave)
        self.assertEqual(code, 1)   # failure, not a crash and not a hang
        # Diagnostics belong on stderr so stdout stays capturable.
        self.assertEqual(out, "")
        self.assertIn("no reply", err)

    def test_successful_reply_goes_to_stdout(self):
        sim = Sim()
        try:
            code, out, err = self.capture(
                lambda: sswitch_mod.run_one(_open(sim.port), "STATUS"))
        finally:
            sim.close()
        self.assertEqual(code, 0)
        self.assertIn("ATTINY85-SSWITCH", out)
        self.assertEqual(err, "")

    def test_repl_gives_up_rather_than_looping(self):
        s, master, slave = self.silent_port()
        import builtins
        real_input = builtins.input
        calls = []

        def fake_input():
            # Prompts go to stderr now, so input() takes no argument.
            calls.append(1)
            if len(calls) > 5:
                raise AssertionError("repl did not give up on a silent device")
            return "on 0"          # never sends "exit"

        builtins.input = fake_input
        try:
            code, out, err = self.capture(lambda: sswitch_mod.repl(s, 0.2))
        finally:
            builtins.input = real_input
            s.close()
            os.close(master)
            os.close(slave)
        self.assertEqual(code, 2)
        self.assertEqual(len(calls), 1,
                         "repl re-prompted after the device went silent")
        self.assertEqual(out, "")
        self.assertIn("no reply", err)


class TestTranslate(unittest.TestCase):
    """The English layer. Pure function, no device needed."""

    def check(self, english, expected):
        self.assertEqual(sswitch_mod.translate(english), expected)

    def test_switch_on_pin_n(self):
        self.check("switch on pin 0", "ON 0")

    def test_without_the_switch_prefix(self):
        self.check("on 0", "ON 0")

    def test_case_insensitive(self):
        self.check("SWITCH OFF PIN 1", "OFF 1")

    def test_all_off(self):
        self.check("all off", "ALL OFF")

    def test_dim_is_on_with_a_duty(self):
        self.check("dim 1 120", "ON 1 120")

    def test_pin_keyword_is_optional(self):
        self.check("read pin 2", "READ 2")
        self.check("read 2", "READ 2")

    def test_blanks(self):
        self.assertIsNone(sswitch_mod.translate(""))
        self.assertIsNone(sswitch_mod.translate("   "))
        self.assertIsNone(sswitch_mod.translate("switch"))

    def test_unknown_passes_through(self):
        self.check("frobnicate", "FROBNICATE")

    def test_toggle_is_absent_by_design(self):
        self.assertNotEqual(sswitch_mod.translate("toggle 0"), "ON 0")


if __name__ == "__main__":
    unittest.main()
