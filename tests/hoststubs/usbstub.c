/* Implementation of the V-USB bits stubbed in hoststubs/usbdrv.h. */

#include "usbdrv.h"

unsigned char usbStubSent[64];
unsigned char usbStubLen;
unsigned char usbStubTxBusy;

void usbSetInterrupt(unsigned char *data, unsigned char len)
{
    unsigned char i;
    if (usbStubLen + len > sizeof usbStubSent) return;
    for (i = 0; i < len; i++) usbStubSent[usbStubLen++] = data[i];
}
