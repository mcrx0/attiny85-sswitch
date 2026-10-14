/*
 * main.c - USB plumbing for a CDC-ACM serial switch on an ATtiny85.
 *
 * The ATtiny85 has no USB peripheral at all. Everything here exists to make
 * a bit-banged USB 1.1 low-speed device look like a serial port to Linux.
 */
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include <string.h>

#include "usbdrv.h"
#include "config.h"
#include "channels.h"
#include "proto.h"
#include "reply.h"

/* CDC class requests, PSTN1200 table 5. */
#define CDC_SET_LINE_CODING        0x20
#define CDC_GET_LINE_CODING        0x21
#define CDC_SET_CONTROL_LINE_STATE 0x22
#define CDC_SEND_BREAK             0x23

#define USBDESCR_DEVICE    1
#define USBDESCR_CONFIG    2

/*
 * Device descriptor.
 *
 * bcdUSB is 1.10, not 2.00, because that is what V-USB actually implements.
 * Claiming 2.0 buys nothing and can confuse host-side drivers.
 *
 * bMaxPacketSize0 is 64 even though this is a low-speed device with an
 * 8-byte interrupt endpoint; 64 is the control endpoint maximum and hosts
 * expect it there.
 */
static const uchar deviceDescriptor[] PROGMEM = {
    18,        /* bLength            */
    1,         /* bDescriptorType    */
    0x10, 0x01,/* bcdUSB 1.10        */
    0x02,      /* bDeviceClass CDC   */
    0x00,      /* bDeviceSubclass    */
    0x01,      /* bDeviceProtocol AT */
    64,        /* bMaxPacketSize0    */
    0xC0, 0x16,/* idVendor  0x16c0   */
    0xE1, 0x05,/* idProduct 0x05e1   */
    0x01, 0x00,/* bcdDevice 1.00     */
    1,         /* iManufacturer      */
    2,         /* iProduct           */
    3,         /* iSerialNumber      */
    1          /* bNumConfigurations */
};

/*
 * Configuration descriptor: two interfaces, three endpoints, 67 bytes.
 *
 * The part that actually matters:
 *
 *   EP1 IN and EP1 OUT carry bmAttributes 0x03, INTERRUPT.
 *
 * The USB specification forbids BULK endpoints on low-speed devices --
 * obdev say so in their own product page -- but PSTN1200 requires the CDC
 * data class to use bulk. That contradiction is why obdev discourage
 * low-speed CDC outright. Declaring the endpoints interrupt is the way
 * through: Linux's cdc-acm driver submits URBs to whatever endpoints the
 * descriptor names and does not care what they are called. The practical
 * consequence of getting this wrong is not a slightly wrong port, it is a
 * device that never produces /dev/ttyACM* at all.
 *
 * EP3 IN is the CDC notification endpoint. The descriptor announces it but
 * we never feed it, which is why the device never speaks unsolicited: there
 * is no path for it to do so.
 *
 * bmCapabilities on the ACM functional descriptor is 0x02, USB_CDC_CAP_LINE.
 * We deliberately do NOT set USB_CDC_CAP_BRK (0x04): this firmware does not
 * implement SEND_BREAK, and advertising a capability we do not have means a
 * host gets no error when it tries to use it.
 */
static const uchar configurationDescriptor[] PROGMEM = {
    /*
     * Configuration header. Note the two-byte wTotalLength, then
     * bNumInterfaces = 2: interface 0 is the CDC communications interface
     * and interface 1 is the data interface. It is tempting to fold the "2"
     * into the wTotalLength comment above and then forget the byte; that
     * shifts every following field by one and produces a device that never
     * enumerates.
     */
    9, 2, 67, 0, /* wTotalLength = 67 */
    2,           /* bNumInterfaces = 2 */
    1,           /* bConfigurationValue */
    0,           /* iConfiguration      */
    0x80,        /* bmAttributes: bus powered, no remote wakeup */
    50,          /* bMaxPower: 2 mA units, so 100 mA -- the USB 2.0 ceiling
                  * for a bus-powered device. Not a wish: a single SRD-05VDC
                  * relay coil is ~90 mA, so a multi-channel module really
                  * will exceed this and really should be on its own supply.
                  * See docs/wiring.md. */

    /* Interface 0: Communications / ACM / AT */
    9, 4, 0, 0, 1, 0x02, 0x02, 0x01, 0,

    /* CDC header functional */
    5, 0x24, 0x00, 0x10, 0x01,

    /* CDC call management: device handles interface 1 */
    5, 0x24, 0x01, 0x03, 0x01,

    /* CDC ACM: line coding only */
    4, 0x24, 0x02, 0x02,

    /* CDC union: control interface 0, subordinate interface 1 */
    5, 0x24, 0x06, 0, 1,

    /* EP3 IN, interrupt, 8 bytes, 255 ms -- notification, never used */
    7, 5, 0x83, 0x03, 8, 0x00, 0xFF,

    /* Interface 1: Data */
    9, 4, 1, 0, 2, 0x0A, 0x00, 0x00, 0,

    /* EP1 IN, interrupt, 8 bytes, 10 ms */
    7, 5, 0x81, 0x03, 8, 0x00, 0x0A,

    /* EP1 OUT, interrupt, 8 bytes, 10 ms -- commands arrive here */
    7, 5, 0x01, 0x03, 8, 0x00, 0x0A
};

/*
 * We ignore SET_LINE_CODING rather than storing it. There is no UART behind
 * this interface, so a baud rate has no meaning; keeping the host's value
 * and echoing it back would only imply we care. GET_LINE_CODING still has
 * to answer, and 9600 8N1 is the conventional thing to say.
 */
static const uchar lineCoding[7] PROGMEM = {
    0x80, 0x25, /* 9600 baud */
    0x00,       /* 1 stop bit      */
    0x00,       /* no parity       */
    0x08        /* 8 data bits     */
};

/* ----------------------------------------------------------- callbacks -- */

USB_PUBLIC usbMsgLen_t usbFunctionDescriptor(struct usbRequest *rq)
{
    if (rq->wValue.bytes[1] == USBDESCR_DEVICE) {
        usbMsgPtr = (usbMsgPtr_t)deviceDescriptor;
        return sizeof deviceDescriptor;
    }
    if (rq->wValue.bytes[1] == USBDESCR_CONFIG) {
        usbMsgPtr = (usbMsgPtr_t)configurationDescriptor;
        return sizeof configurationDescriptor;
    }
    return 0;
}

USB_PUBLIC uchar usbFunctionSetup(uchar data[8])
{
    usbRequest_t *rq = (usbRequest_t *)(void *)data;

    if ((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {
        switch (rq->bRequest) {
        case CDC_SET_LINE_CODING:
            return 0;
        case CDC_GET_LINE_CODING: {
            uint8_t i;
            for (i = 0; i < sizeof lineCoding; i++) {
                data[i] = (uchar)pgm_read_byte(&lineCoding[i]);
            }
            return sizeof lineCoding;
        }
        case CDC_SET_CONTROL_LINE_STATE:
            /*
             * DTR/RTS are ignored on purpose. Wiring "host disconnects ->
             * all relays off" to DTR would be tempting, but every python
             * open()/close() cycle toggles it, so a script that reopens the
             * port between commands would flap the relays it just set.
             */
            return 0;
        default:
            break;
        }
    }

    return USB_NO_MSG; /* stall: not something this device does */
}

/* Host -> device: the command stream. */
USB_PUBLIC void usbFunctionWriteOut(uchar *data, uchar len)
{
    while (len--) {
        if (proto_feed(*data++)) proto_dispatch();
    }
}

/* ----------------------------------------------------------------- main -- */

int main(void)
{
    uint8_t i;

    /*
     * Pins first. Everything is OFF before USB is alive, and stays OFF if
     * USB never comes up at all.
     */
    channels_init();

    usbInit();

    /*
     * Drop off the bus and come back, so the host re-enumerates and reads
     * fresh descriptors rather than trusting a cached copy from the last
     * firmware. Without this, reflashing a device that already enumerated
     * reliably leaves Linux talking to stale descriptors.
     */
    usbDeviceDisconnect();
    for (i = 0; i < 255; i++) _delay_ms(1);
    usbDeviceConnect();

    /*
     * 8 s watchdog. V-USB does all its bit-banging inside one interrupt, so
     * a wedged loop is unrecoverable without a power cycle -- which is
     * exactly when you least want to reach for the cable. A reset lands in
     * channels_init(), so the worst case is the safe state.
     */
    wdt_enable(WDTO_8S);

    for (;;) {
        usbPoll();
        reply_pump();
        wdt_reset();
    }

    return 0;
}
