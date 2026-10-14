/*
 * channels.c - driving the three switchable pins.
 *
 * Two rules shape this file.
 *
 * 1. Boot must be OFF, and OFF must be a real static level rather than a
 *    duty cycle of zero, so that READ can tell the truth and so that an
 *    idle channel is electrically quiet.
 *
 * 2. PWM uses only the hardware compare outputs. PB0 is OC0A and PB1 is
 *    OC0B; PB2 has no compare output at all. Getting PWM onto PB2 would
 *    mean a timer interrupt, and V-USB's bit-banging lives in a
 *    timing-critical pin-change ISR -- a competing interrupt there costs
 *    enumeration reliability, so we simply do not offer it.
 */
#include "channels.h"
#include "config.h"

#include <avr/io.h>
#include <avr/pgmspace.h>

#define CH_MASK_P0 0x01
#define CH_MASK_P1 0x02
#define CH_MASK_P2 0x04

#define OC_NONE 0xFF

/* Compare output each channel can borrow from Timer/Counter0. */
static const uint8_t ch_oc[CH_COUNT] PROGMEM = { 0, 1, OC_NONE };

static uint8_t oc_of(uint8_t i)
{
    return (uint8_t)pgm_read_byte(&ch_oc[i]);
}

/* TCCR0A compare-output bit positions. */
#define COM0A_SHIFT 6 /* COM0A1:0 */
#define COM0B_SHIFT 4 /* COM0B1:0 */
#define COM0A_MASK  ((uint8_t)(3u << COM0A_SHIFT))
#define COM0B_MASK  ((uint8_t)(3u << COM0B_SHIFT))

static channel_t ch[CH_COUNT] = {
    { CH_MASK_P0, CH0_ACTIVE_LOW, CH_OUT, 0 },
    { CH_MASK_P1, CH1_ACTIVE_LOW, CH_OUT, 0 },
    { CH_MASK_P2, CH2_ACTIVE_LOW, CH_OUT, 0 },
};

/* ---------------------------------------------------------------- PWM ---- */

static void pwm_timer_init(void)
{
    /*
     * Phase-correct PWM (WGM01:00 = 01), TOP = 0xFF, so the period is
     * 2 * 256 counts and both compare registers are free to be duties.
     * Both outputs start disconnected, leaving P0 and P1 as plain pins.
     */
    TCCR0A = 0;
    OCR0A = 0;
    OCR0B = 0;
#if PWM_CLOCK_DIV == 1
    TCCR0B = (uint8_t)(1 << CS00);         /* 32.2 kHz -- motors */
#elif PWM_CLOCK_DIV == 8
    TCCR0B = (uint8_t)(1 << CS01);         /* 4.0 kHz */
#elif PWM_CLOCK_DIV == 64
    TCCR0B = (uint8_t)((1 << CS02) | (1 << CS01)); /* 503 Hz */
#else
    TCCR0B = (uint8_t)((1 << CS02) | (1 << CS01)); /* 1007 Hz, default */
#endif
}

static void pwm_stop(uint8_t i)
{
    switch (oc_of(i)) {
    case 0:
        TCCR0A &= (uint8_t)~COM0A_MASK;
        OCR0A = 0;
        break;
    case 1:
        TCCR0A &= (uint8_t)~COM0B_MASK;
        OCR0B = 0;
        break;
    default:
        break;
    }
}

/* Fraction of the period the pin spends HIGH, 0..255 where 255 is 100%. */
static void pwm_run(uint8_t i, uint8_t hi)
{
    uint8_t com, ocr;

    /*
     * Phase-correct PWM tops out at 50% in either compare mode, so each
     * mode covers one half of the range:
     *
     *   COM = 10  clear on match, set at BOTTOM -> HIGH = OCR / 512
     *   COM = 11  set on match, clear at BOTTOM -> HIGH = (512 - OCR) / 512
     *
     * With OCR = 2 * min(hi, 255 - hi) the two cover 0..50% and 50..100%
     * and never collide on OCR = 255, which is the degenerate midpoint.
     */
    if (hi < 128) {
        com = 2;
        ocr = (uint8_t)(hi << 1);
    } else {
        com = 3;
        ocr = (uint8_t)((255 - hi) << 1);
    }

    switch (oc_of(i)) {
    case 0:
        OCR0A = ocr;
        TCCR0A = (uint8_t)(((TCCR0A & (uint8_t)~COM0A_MASK)
                            | (uint8_t)(com << COM0A_SHIFT) | (1 << WGM01)));
        break;
    case 1:
        OCR0B = ocr;
        TCCR0A = (uint8_t)(((TCCR0A & (uint8_t)~COM0B_MASK)
                            | (uint8_t)(com << COM0B_SHIFT) | (1 << WGM01)));
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------- apply ----- */

uint8_t channel_static_level(const channel_t *c)
{
    if (c->mode == CH_IN) return 2; /* not driven at all */
    if (c->duty == 0) return c->active_low ? 1 : 0;
    if (c->duty == 255) return c->active_low ? 0 : 1;
    return 2; /* PWM running: no steady level to report */
}

static void apply(uint8_t i)
{
    channel_t *c = &ch[i];
    uint8_t hi;

    if (c->mode == CH_IN) {
        pwm_stop(i);
        /*
         * The internal pull-up only biases this pin's own input. A relay
         * module's control input draws no current, so this does not drive
         * the module in either direction -- and READ reports the real
         * external level.
         */
        PORTB |= c->bit;
        DDRB &= (uint8_t)~c->bit;
        return;
    }

    if (c->duty == 0 || c->duty == 255) {
        uint8_t phys = channel_static_level(c);
        pwm_stop(i);
        /*
         * PORTB before DDRB, always. Reversing that order parks the pin at
         * its reset value for a few cycles -- and for an active-LOW module
         * the reset value is LOW, which is energised. On a mains relay
         * that is an audible click and a real contact closure at every
         * power-up.
         */
        if (phys) PORTB |= c->bit;
        else      PORTB &= (uint8_t)~c->bit;
        DDRB |= c->bit;
        return;
    }

    hi = c->duty;
    if (c->active_low) hi = (uint8_t)(255 - c->duty);

    pwm_stop(i);
    PORTB &= (uint8_t)~c->bit; /* idle level while the compare output is off */
    DDRB |= c->bit;
    pwm_run(i, hi);
}

/* ------------------------------------------------------------ public ----- */

void channels_init(void)
{
    uint8_t i;
    pwm_timer_init();
    for (i = 0; i < CH_COUNT; i++) {
        ch[i].mode = CH_OUT;
        ch[i].duty = 0;
        apply(i);
    }
}

uint8_t channels_count(void)
{
    return CH_COUNT;
}

channel_t *channel_get(uint8_t index)
{
    return (index < CH_COUNT) ? &ch[index] : 0;
}

uint8_t channel_pwm_capable(uint8_t index)
{
    return (index < CH_COUNT) && (oc_of(index) != OC_NONE);
}

/*
 * Reasons live in flash, not SRAM. On AVR a plain string literal is copied
 * to RAM at startup, and at 512 bytes total that is not a rounding error.
 * The caller reads these with reply_strp().
 */
const char *channel_reject_reason(uint8_t pin)
{
    if (pin < CH_COUNT) return 0;
    switch (pin) {
    case PIN_USB_DMINUS: return PSTR("USB D-");
    case PIN_USB_DPLUS:  return PSTR("USB D+");
    case PIN_RESET:      return PSTR("RESET");
    default:             return PSTR("NO SUCH PIN");
    }
}

void channel_set_duty(uint8_t index, uint8_t duty)
{
    if (index >= CH_COUNT) return;
    ch[index].mode = CH_OUT;
    ch[index].duty = duty;
    apply(index);
}

void channel_set_input(uint8_t index)
{
    if (index >= CH_COUNT) return;
    ch[index].mode = CH_IN;
    apply(index);
}

void channel_set_output(uint8_t index)
{
    channel_set_duty(index, 0);
}

void channels_all_off(void)
{
    uint8_t i;
    for (i = 0; i < CH_COUNT; i++) ch[i].duty = 0;
    for (i = 0; i < CH_COUNT; i++) {
        ch[i].mode = CH_OUT;
        apply(i);
    }
}

uint8_t channel_read_level(uint8_t index)
{
    channel_t *c;
    uint8_t lvl;

    if (index >= CH_COUNT) return 2;
    c = &ch[index];

    if (c->mode == CH_IN) return (PINB & c->bit) ? 1 : 0;

    lvl = channel_static_level(c);
    if (lvl != 2) return lvl;
    return 2; /* PWM */
}
