/*
 * config.h - compile-time configuration for attiny85-sswitch.
 *
 * Everything a user of this firmware is expected to change lives here.
 * Nothing in this file is a command; it is hardware truth.
 */
#ifndef SSWITCH_CONFIG_H
#define SSWITCH_CONFIG_H

/*
 * The Digispark runs its internal RC oscillator through the PLL.
 *
 * V-USB only supports a fixed set of clock rates, and 16.5 MHz is the one
 * that tolerates an untrimmed RC (+/-1%). 16 MHz -- which the Digispark
 * documentation calls "16 MHz via internal PLL" -- is NOT supported, so
 * do not "simplify" this to 16000000.
 */
#define F_CPU 16500000UL

/*
 * Per-channel polarity.
 *
 *   0 = ACTIVE HIGH : the module energises when the pin goes HIGH.
 *                     KY-019 / CW-019 / bare SS8050 boards.
 *   1 = ACTIVE LOW  : the module energises when the pin goes LOW.
 *                     Blue PC817 optocoupler boards with the jumper on "Lo".
 *
 * The protocol is written so this choice never leaks into it:
 *
 *   ON / OFF   always mean energised / de-energised, whatever this says.
 *   HIGH / LOW always mean a voltage, whatever this says.
 *
 * The firmware only needs to know so it can boot into a safe state and
 * report the level truthfully. Defaults are ACTIVE HIGH because that is
 * what the most common bare relay board does.
 */
#define CH0_ACTIVE_LOW 0
#define CH1_ACTIVE_LOW 0
#define CH2_ACTIVE_LOW 0

/*
 * PWM carrier frequency.
 *
 * Timer/Counter0 in phase-correct PWM mode, so the period is 2 * 256
 * counts and f = F_CPU / (PWM_CLOCK_DIV * 512):
 *
 *      1 -> 32.2 kHz   above hearing; use this for motors
 *      8 ->  4.0 kHz
 *     32 ->  1007 Hz   default: inaudible for relay coils, flicker-free
 *                        for LEDs
 *     64 ->   503 Hz
 *
 * Set this to the divider, not the resulting frequency.
 */
#define PWM_CLOCK_DIV 32

#endif /* SSWITCH_CONFIG_H */
