/*
 * usbconfig.h - V-USB configuration for a CDC-ACM device on an ATtiny85.
 *
 * Two things here are not obvious and both are load-bearing.
 *
 * 1. The interrupt. V-USB's examples all put D+ on INT0 because that is
 *    the highest-priority interrupt on AVR. On the ATtiny85 INT0 is PB2,
 *    and the Digispark puts D+ on PB4, so we are forced onto pin-change
 *    interrupts -- the lowest priority there is. This is not an
 *    optimisation knob: usbdrv.c's usbPoll() does no bit-banging at all,
 *    it only drains buffers. All NRZI decoding happens in the assembly
 *    installed at USB_INTR_VECTOR. If that interrupt never fires, USB
 *    never works.
 *
 * 2. The endpoints. See the descriptor in main.c. USB forbids bulk
 *    endpoints at low speed; obdev say so plainly in their own
 *    documentation. We get a working ttyACM by declaring the CDC data
 *    endpoints as interrupt, which Linux's cdc-acm does not care about.
 */
#ifndef __USB_CONFIG_H__
#define __USB_CONFIG_H__

#include "config.h"

#define USB_CFG_IOPORTNAME B
#define USB_CFG_DMINUS_BIT 3
#define USB_CFG_DPLUS_BIT  4

#define USB_INTR_CFG      PCMSK
#define USB_INTR_CFG_SET  (1 << USB_CFG_DPLUS_BIT)
#define USB_INTR_ENABLE   GIMSK
#define USB_INTR_ENABLE_BIT PCIE
#define USB_INTR_PENDING  GIFR
#define USB_INTR_PENDING_BIT PCIF
#define USB_INTR_VECTOR   PCINT0_vect

#define USB_CFG_IOPORTNAME_WIDTH 6
#define USB_CFG_PORTRAIT         0
#define USB_CFG_DMINUS_IS_HIGHD  0

#define USB_CFG_SEND_TX_ERROR   1
#define USB_CFG_SEND_TX_NAK     1
#define USB_CFG_SEND_TX_WAKEUP  1

/* F_CPU/1000 = 16500 selects usbdrvasm165.inc, the assembly that tolerates
 * an untrimmed RC oscillator. */
#define USB_CFG_CLOCK_KHZ     (F_CPU / 1000)
#define USB_CFG_CHECK_TOKENS  0
#define USB_CFG_CHECK_NAKED_IN 0

/*
 * Direction matters more here than it looks.
 *
 * V-USB reaches usbFunctionRead()/usbFunctionWrite() only for CONTROL
 * transfers. Traffic on the CDC data endpoint has its own path, and the
 * only way in from the host is usbFunctionWriteOut(), which exists if and
 * only if USB_CFG_IMPLEMENT_FN_WRITEOUT is 1. Leave it at 0 and every
 * command the host sends is received, ACKed by the USB layer, and thrown
 * away without ever reaching the application. It fails silently, which is
 * the worst way to fail.
 */
#define USB_CFG_EP3_SIZE                  8
#define USB_CFG_IMPLEMENT_FN_WRITEOUT     1
#define USB_CFG_IMPLEMENT_FN_WRITE        0
#define USB_CFG_IMPLEMENT_FN_READ         0
#define USB_CFG_IMPLEMENT_FN_TXOK         0
#define USB_CFG_IMPLEMENT_FN_HALT         0
#define USB_CFG_HAVE_UCALL_TRUNCATED_VIA_USBPOLL_B 0
#define USB_CFG_HAVE_FLOWCONTROL          0

/*
 * usbSetInterrupt() is the ONLY way V-USB will put data on an interrupt IN
 * endpoint. The transmit path inside usbPoll() is reserved for control
 * replies, driven by usbMsgLen, which is file-static inside usbdrv.c and
 * therefore unreachable from application code.
 *
 * usbSetInterrupt() overwrites unconditionally and sends one packet, so
 * reply_pump() in reply.c checks that usbTxLen1 shows the buffer free before
 * queueing the next 8 bytes.
 */
#define USB_CFG_HAVE_INTRIN_ENDPOINT 1

#define USB_CFG_SEND_STRINGS 0

/* Both descriptors are hand-written for CDC, so fetch them at runtime. */
#define USB_CFG_DESCR_PROPS_DEVICE        USB_PROP_IS_DYNAMIC
#define USB_CFG_DESCR_PROPS_CONFIGURATION USB_PROP_IS_DYNAMIC
#define USB_CFG_DESCR_PROPS_STRINGS                 0
#define USB_CFG_DESCR_PROPS_STRING_0                0
#define USB_CFG_DESCR_PROPS_STRING_VENDOR           0
#define USB_CFG_DESCR_PROPS_STRING_PRODUCT          0
#define USB_CFG_DESCR_PROPS_STRING_SERIAL_NUMBER    0
#define USB_CFG_DESCR_PROPS_HID                     0
#define USB_CFG_DESCR_PROPS_HID_REPORT              0
#define USB_CFG_DESCR_PROPS_UNKNOWN                 0

#define USB_CFG_FIXEDNUM_EP 3
#define USB_CFG_BIG_ENDIAN   0
#define USB_CFG_SERIAL_OUTPUT 1
#define USB_CFG_USER_SIGNAL   0
#define USB_CFG_USER_POLL     1

/*
 * VID/PID 0x16c0/0x05e1 is obdev's shared pair reserved for CDC-ACM class
 * devices, free for open-source use under the rules in
 * firmware/vendor/v-usb/usbdrv/USB-IDs-for-free.txt.
 *
 * Those rules require the manufacturer string to contain a domain name or
 * e-mail address that YOU own. TODO: replace the placeholder below before
 * publishing anything, otherwise you are squatting on someone else's ID.
 */
#define USB_CFG_VENDOR_NAME 'e','x','a','m','p','l','e','.','i','n','v','a','l','i','d'
#define USB_CFG_VENDOR_NAME_LEN 15

#define USB_CFG_DEVICE_NAME 'a','t','t','i','n','y','8','5','-','s','s','w','i','t','c','h'
#define USB_CFG_DEVICE_NAME_LEN 16

#define USB_CFG_DEVICE_VERSION 0x01, 0x00
#define USB_CFG_DEVICE_CLASS     0x02 /* CDC */
#define USB_CFG_DEVICE_SUBCLASS  0x00
#define USB_CFG_DEVICE_PROTOCOL  0x01 /* AT commands */

#define USB_CFG_INTERFACE_CLASS     0
#define USB_CFG_INTERFACE_SUBCLASS  0
#define USB_CFG_INTERFACE_PROTOCOL  0

#define USB_CFG_VENDOR_ID  0xc0, 0x16
#define USB_CFG_DEVICE_ID  0xe1, 0x05

/* 100 mA is the ceiling for a bus-powered low-speed device. */
#define USB_CFG_MAX_BUS_POWER 100

#define USB_CFG_IS_SELF_POWERED 0

#endif /* __USB_CONFIG_H__ */
