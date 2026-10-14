#include "reply.h"
#include "usbdrv.h"

#include <avr/pgmspace.h>

/*
 * A low-speed interrupt endpoint carries at most 8 bytes per packet, and
 * USB_BUFSIZE is exactly PID + 8 + CRC2, so 8 is the hard ceiling.
 */
#define TX_PACKET 8

static char buf[REPLY_MAX];
static uint8_t len;   /* how much of the reply is built */
static uint8_t sent;  /* how much the host has taken  */
static uint8_t armed; /* an untransmitted reply exists  */

void reply_begin(void)
{
    len = 0;
    sent = 0;
    armed = 0;
}

void reply_ch(char c)
{
    if (len < (REPLY_MAX - 1)) buf[len++] = c;
}

void reply_str(const char *s)
{
    while (*s) reply_ch(*s++);
}

void reply_strp(const char *s)
{
    char c;
    while ((c = (char)pgm_read_byte(s++)) != 0) reply_ch(c);
}

void reply_u(uint16_t v)
{
    char tmp[5];
    uint8_t n = 0;

    if (v == 0) {
        reply_ch('0');
        return;
    }
    while (v) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n) reply_ch(tmp[--n]);
}

void reply_commit(void)
{
    buf[len] = '\0';
    armed = 1;
}

uint8_t reply_busy(void)
{
    return armed;
}

/*
 * Hand the next packet to V-USB, if the endpoint is ready for it.
 *
 * usbTxLen1 & 0x10 means the packet buffer is empty: either NAKed away by
 * the host, or never filled. Anything else means the bit-bang ISR still
 * holds our bytes and usbSetInterrupt() would overwrite them.
 *
 * Call this from the main loop, not from the receive path. The host polls
 * EP1 IN every 10 ms, so the 108-byte HELP line takes about 140 ms to
 * drain, and the host has to be left waiting for it.
 */
void reply_pump(void)
{
    uint8_t n;

    if (!armed) return;
    if (!(usbTxLen1 & 0x10)) return; /* still busy with the previous packet */

    n = (uint8_t)(len - sent);
    if (n > TX_PACKET) n = TX_PACKET;

    usbSetInterrupt((uint8_t *)buf + sent, n);
    sent = (uint8_t)(sent + n);

    if (sent >= len) armed = 0;
}
