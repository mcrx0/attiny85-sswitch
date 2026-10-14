/*
 * reply.h - building the one line that comes back for every command.
 *
 * Deliberately not snprintf. avr-libc's vsnprintf is around 1.7 kB, and
 * this device has about 5 kB to spend in total. Two verbs and no format
 * string parsing costs about 100 bytes and removes a whole class of
 * buffer surprises.
 *
 * The device never speaks unsolicited. A reply exists only because a
 * command was received, which is what lets a host script read the port
 * without ever having to filter a banner out of the stream.
 */
#ifndef SSWITCH_REPLY_H
#define SSWITCH_REPLY_H

#include <stdint.h>

/*
 * Longest line we emit. The HELP line is the worst case at 108 characters.
 *
 * This buffer lives in SRAM, so it is one of the few things competing for
 * the ATtiny85's 512 bytes. Everything that does not change -- the format
 * strings in particular -- belongs in flash, which is why reply_strp() and
 * PSTR exist. The first cut of this firmware put ~560 bytes of string
 * literals in RAM and did not link.
 */
#define REPLY_MAX 112

void reply_begin(void);

/* Append a RAM string. Used for text assembled at runtime. */
void reply_str(const char *s);

/* Append a string from flash. */
void reply_strp(const char *s);

void reply_u(uint16_t v);
void reply_ch(char c);

/* Call once the line is complete. */
void reply_commit(void);

/* Non-zero while a committed reply still has bytes the host has not taken. */
uint8_t reply_busy(void);

/*
 * Push the next 8-byte packet at the host, once the endpoint is ready.
 *
 * V-USB gives no callback for unsolicited data on an interrupt IN endpoint;
 * usbPoll()'s own transmit path drives a file-static length counter that
 * application code cannot reach. usbSetInterrupt() is the supported route,
 * and it both overwrites unconditionally and handles one packet at a time,
 * so replies are queued here and released a packet at a time from the main
 * loop. Call it after usbPoll().
 */
void reply_pump(void);

/*
 * Append a string literal, which lives in flash.
 *
 * On AVR a plain "..." in .rodata is copied to SRAM at startup, which is a
 * surprisingly expensive way to spend 512 bytes. PSTR keeps it in flash and
 * reply_strp() reads it from there.
 */
#define reply_lit(literal) reply_strp(PSTR(literal))

/*
 * Pick between two literals.
 *
 * Not a ternary inside PSTR(): a conditional expression of two char
 * pointers is not const, and the cast to __c[] rejects it.
 */
#define reply_either(cond, a, b) \
    do { if (cond) reply_lit(a); else reply_lit(b); } while (0)

/*
 * An error line, in one call.
 *
 * A macro rather than a function so that PSTR can wrap each argument: on
 * AVR a plain literal in .rodata is copied to SRAM at startup, and the
 * error strings alone come to roughly 300 bytes. At 512 bytes of SRAM
 * total that is the difference between linking and not.
 */
#define err(a, b)                    \
    do {                             \
        reply_begin();               \
        reply_strp(PSTR(a));         \
        reply_strp(PSTR(b));         \
        reply_commit();              \
    } while (0)

#endif /* SSWITCH_REPLY_H */
