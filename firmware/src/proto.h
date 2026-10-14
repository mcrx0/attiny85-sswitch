/*
 * proto.h - the line protocol.
 *
 * One line in, exactly one line out. Case-insensitive, spaces only, no
 * quoting rules, no toggles.
 *
 *   ON <p>              duty 255
 *   ON <p> <1-255>      PWM duty
 *   OFF <p>             duty 0
 *   HIGH <p>            raw level, PWM mode off
 *   LOW <p>             raw level, PWM mode off
 *   IN <p>              pin becomes an input
 *   OUT <p>             pin becomes an output again, OFF
 *   READ <p>            actual level back
 *   PINS [--raw]        every channel
 *   ALL OFF             panic
 *   HELP | STATUS
 *
 * Why there is no toggle: a toggle cannot be retried. A script that times
 * out and re-sends would flip the load back to where it started. Every
 * command here assigns a value instead.
 *
 * The English form ("switch on pin 0") lives in tools/sswitch, not here.
 * That is the trade for a firmware a few hundred bytes smaller and a
 * device that a raw shell redirect can drive.
 */
#ifndef SSWITCH_PROTO_H
#define SSWITCH_PROTO_H

#include <stdint.h>

/* Feed one received byte. Returns non-zero once a complete line is ready
 * for dispatch. */
uint8_t proto_feed(uint8_t byte);

uint8_t proto_pending(void);

/* Parse and answer the pending line. */
void proto_dispatch(void);

#endif /* SSWITCH_PROTO_H */
