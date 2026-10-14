/*
 * Host-build stand-in for <avr/io.h>.
 *
 * board.h includes it for PORTB/DDRB/PINB and the Timer/Counter0 bit names.
 * The protocol tests declare those as plain variables and constants, so the
 * hardware access stays visible in the test and there is no register
 * emulation to misplace the bit numbers in.
 */

#ifndef SSWITCH_TEST_AVR_IO_H
#define SSWITCH_TEST_AVR_IO_H

#include <stdint.h>

extern uint8_t PORTB;
extern uint8_t DDRB;
extern uint8_t PINB;
extern uint8_t TCCR0A;
extern uint8_t TCCR0B;
extern uint8_t OCR0A;
extern uint8_t OCR0B;

/* TCCR0A */
#define COM0A1 7
#define COM0A0 6
#define COM0B1 5
#define COM0B0 4
#define WGM01  1
#define WGM00  0

/* TCCR0B */
#define FOC0A 7
#define FOC0B 6
#define CS02  2
#define CS01  1
#define CS00  0

#endif /* SSWITCH_TEST_AVR_IO_H */
