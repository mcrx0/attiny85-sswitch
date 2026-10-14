/*
 * Host-build stand-in for V-USB's usbdrv.h.
 *
 * The protocol tests compile channels.c, proto.c and reply.c as ordinary
 * host code so the logic can be exercised without a Digispark. reply.c
 * touches V-USB only to queue a finished line for transmission, so all that
 * is stubbed here is the queue and the interrupt-buffer flag.
 *
 * This is NOT a mock of the firmware: channels.c, proto.c and reply.c are
 * the exact sources that get compiled into the hex. Only the USB transport
 * underneath them is absent.
 */
#ifndef SSWITCH_TEST_USBDRV_H
#define SSWITCH_TEST_USBDRV_H

#include <stdint.h>

typedef uint8_t uchar;

/* Set by the test to observe what would have gone on the wire. */
extern unsigned char usbStubSent[64];
extern unsigned char usbStubLen;
extern unsigned char usbStubTxBusy;

#define usbTxLen1 usbStubTxBusy

#define PSTR(s_) (s_)

void usbSetInterrupt(unsigned char *data, unsigned char len);

#endif /* SSWITCH_TEST_USBDRV_H */
