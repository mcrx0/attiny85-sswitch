/*
 * board.h - the Digispark pin map.
 *
 * The ATtiny85 has a 6-bit port B and no PB6/PB7. On a Digispark three of
 * those six bits are not available to us:
 *
 *   PB0 -> P0   free
 *   PB1 -> P1   free, but the onboard LED is wired to it
 *   PB2 -> P2   free
 *   PB3 -> P3   USB D-   (V-USB bit-bangs it)
 *   PB4 -> P4   USB D+   (V-USB bit-bangs it, and it is the pin-change
 *                       interrupt source, so it is never idle)
 *   PB5 -> P5   RESET    (a real GPIO only with RSTDISBL, which kills ISP)
 *
 * So the device has exactly three channels. See docs/pinout.md.
 */
#ifndef SSWITCH_BOARD_H
#define SSWITCH_BOARD_H

#include <avr/io.h>

#define CH_COUNT 3

/* PORTB bit masks for each channel. Channel index == bit index. */
#define CH_BIT(n) ((uint8_t)(1u << (n)))

/* The three pins that exist but are not ours, in bit order, for rejection
 * messages. Keeping the reason next to the pin is the whole point: a bare
 * "ERR BAD PIN" leaves the user guessing. */
#define PIN_USB_DMINUS 3
#define PIN_USB_DPLUS  4
#define PIN_RESET      5

#endif /* SSWITCH_BOARD_H */
