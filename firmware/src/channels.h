/*
 * channels.h - switchable pins, their polarity, and their PWM.
 *
 * The vocabulary this file implements, which the rest of the firmware and
 * the documentation both depend on:
 *
 *   switch state   ON or OFF. Semantic. "Is the load energised?"
 *   pin level      HIGH or LOW. Physical. "What voltage is on the pad?"
 *   duty           0..255. How much of the period the channel is ON.
 *   polarity       the compile-time mapping between the two above.
 *
 * These are deliberately separate. ON is never a voltage, and HIGH is never
 * a decision. An active-LOW relay module drives them opposite to each other
 * and the command language does not notice.
 */
#ifndef SSWITCH_CHANNELS_H
#define SSWITCH_CHANNELS_H

#include <stdint.h>
#include "board.h"

#define CH_IN  0
#define CH_OUT 1

typedef struct {
    uint8_t bit;        /* PORTB bit mask                        */
    uint8_t active_low; /* 1 = the module energises on LOW        */
    uint8_t mode;       /* CH_IN or CH_OUT                        */
    uint8_t duty;       /* 0..255                                 */
} channel_t;

/* Drive every channel to its de-energised level. Call before usbInit(). */
void channels_init(void);

uint8_t channels_count(void);

/* NULL when index is not a channel. */
channel_t *channel_get(uint8_t index);

/* 1 when the pin has a hardware compare output, so PWM is possible without
 * a timer interrupt. PB0 is OC0A and PB1 is OC0B; PB2 has none. */
uint8_t channel_pwm_capable(uint8_t index);

/* Why a pin number is not a channel, or NULL if it is one. */
const char *channel_reject_reason(uint8_t pin);

void channel_set_duty(uint8_t index, uint8_t duty);
void channel_set_input(uint8_t index);
void channel_set_output(uint8_t index); /* always re-enters as OFF */
void channels_all_off(void);

/* 0 = LOW, 1 = HIGH, 2 = not a steady level (PWM running). */
uint8_t channel_read_level(uint8_t index);

/* Physical level the pin idles at for the current duty and polarity.
 * 1 = HIGH, 0 = LOW. Meaningless when the mode is CH_IN. */
uint8_t channel_static_level(const channel_t *c);

#endif /* SSWITCH_CHANNELS_H */
