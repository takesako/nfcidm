/* SPDX-License-Identifier: MIT */
#include "nfc_usb.h"
#include <stdio.h>

static void trace(nfc_usb *usb, const char *direction, const uint8_t *data, size_t n)
{
    if (!usb->verbose)
        return;
    (void)fprintf(stderr, "usb %s (%lu):", direction, (unsigned long)n);
    for (size_t i = 0; i < n; i++)
        (void)fprintf(stderr, " %02X", data[i]);
    (void)fputc('\n', stderr);
}

int nfc_usb_write(nfc_usb *usb, const uint8_t *data, size_t size, unsigned ms)
{
    trace(usb, "TX", data, size);
    return usb->write_fn(usb, data, size, ms);
}

int nfc_usb_read(nfc_usb *usb, uint8_t *data, size_t size, unsigned ms)
{
    int n = usb->read_fn(usb, data, size, ms);
    if (n >= 0 && (size_t)n <= size)
        trace(usb, "RX", data, (size_t)n);
    else if (n >= 0) {
        nfc_error(usb->error, sizeof usb->error, "USB read exceeded buffer capacity");
        return NFC_ERR;
    }
    return n;
}

int nfc_usb_control(nfc_usb *usb, uint8_t request, uint16_t value,
                    uint16_t index, const uint8_t *data, size_t size, unsigned ms)
{
    trace(usb, "CONTROL TX", data, size);
    return usb->control_fn(usb, request, value, index, data, size, ms);
}

void nfc_usb_close(nfc_usb *usb)
{
    if (usb->close_fn)
        usb->close_fn(usb);
    usb->opaque = NULL;
    usb->close_fn = NULL;
}
