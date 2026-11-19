/* SPDX-License-Identifier: MIT */
#include "nfc_usb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t incoming[2048], outgoing[2048];
    size_t length, offset, chunk, written;
    bool control;
} fixture;

static int read_bytes(nfc_usb *usb, uint8_t *p, size_t cap, unsigned ms)
{
    (void)ms;
    fixture *x = usb->opaque;
    size_t n = x->length - x->offset;
    if (!n)
        return NFC_TIMEOUT;
    if (n > x->chunk) n = x->chunk;
    if (n > cap) n = cap;
    memcpy(p, x->incoming + x->offset, n);
    x->offset += n;
    return (int)n;
}

static int write_bytes(nfc_usb *usb, const uint8_t *p, size_t n, unsigned ms)
{
    (void)ms;
    fixture *x = usb->opaque;
    assert(n <= sizeof x->outgoing);
    memcpy(x->outgoing, p, n);
    x->written = n;
    return NFC_OK;
}

static int control_bytes(nfc_usb *usb, uint8_t request, uint16_t value,
                          uint16_t index, const uint8_t *p, size_t n, unsigned ms)
{
    fixture *x = usb->opaque;
    assert(!request && !value && !index);
    x->control = true;
    return write_bytes(usb, p, n, ms);
}

int main(void)
{
    static const uint8_t normal[] = {0,0,0xff,2,0xfe,0xd4,2,0x2a,0};
    static const uint8_t extended[] = {0,0,0xff,0xff,0xff,3,0,0xfd,0xd6,0x2a,1,0xff,0};
    static const uint8_t ack[] = {0,0,0xff,0,0xff,0};
    fixture x;
    nfc_usb usb;
    nfc_framer frame;
    uint8_t output[1024];
    memset(&usb, 0, sizeof usb);
    usb.opaque = &x;
    usb.read_fn = read_bytes; usb.write_fn = write_bytes; usb.control_fn = control_bytes;

    memset(&x, 0, sizeof x);
    nfc_frame_init(&frame, &usb, NFC_FRAME_NORMAL, true);
    assert(nfc_frame_send(&frame, normal + 5, 2, 100) == NFC_OK);
    assert(x.control && x.written == sizeof normal);
    assert(!memcmp(x.outgoing, normal, sizeof normal));
    nfc_frame_init(&frame, &usb, NFC_FRAME_PORT100, false);
    assert(nfc_frame_send(&frame, extended + 8, 3, 100) == NFC_OK);
    assert(x.written == sizeof extended && !memcmp(x.outgoing, extended, sizeof extended));

    /* Independent wire vectors; every small USB packet boundary is tried. */
    for (size_t chunk = 1; chunk <= sizeof normal + sizeof ack; chunk++) {
        memset(&x, 0, sizeof x);
        memcpy(x.incoming, ack, sizeof ack);
        memcpy(x.incoming + sizeof ack, normal, sizeof normal);
        x.length = sizeof ack + sizeof normal; x.chunk = chunk;
        nfc_frame_init(&frame, &usb, NFC_FRAME_PN53X, false);
        size_t n = sizeof output;
        assert(nfc_frame_recv(&frame, output, &n, 100) == NFC_FRAME_ACK);
        n = sizeof output;
        assert(nfc_frame_recv(&frame, output, &n, 100) == NFC_FRAME_DATA);
        assert(n == 2 && output[0] == 0xd4 && output[1] == 2);
    }

    /* PN53x and Port-100 must disagree on the endian order of length 256. */
    for (unsigned little = 0; little <= 1; little++) {
        memset(&x, 0, sizeof x);
        const uint8_t header[] = {0,0,0xff,0xff,0xff,1,0,0xff};
        memcpy(x.incoming, header, sizeof header);
        x.incoming[5] = little ? 0 : 1;
        x.incoming[6] = little ? 1 : 0;
        x.length = 266; x.chunk = 19; /* zero payload, checksum, postamble */
        nfc_frame_init(&frame, &usb, little ? NFC_FRAME_PORT100 : NFC_FRAME_PN53X, false);
        size_t n = sizeof output;
        assert(nfc_frame_recv(&frame, output, &n, 100) == NFC_FRAME_DATA && n == 256);
    }

    for (size_t corrupt = 0; corrupt < sizeof normal; corrupt++) {
        memset(&x, 0, sizeof x);
        memcpy(x.incoming, normal, sizeof normal);
        x.incoming[corrupt] ^= 0x10;
        x.length = sizeof normal; x.chunk = sizeof normal;
        memset(output, 0xa5, sizeof output);
        nfc_frame_init(&frame, &usb, NFC_FRAME_NORMAL, false);
        size_t n = sizeof output;
        assert(nfc_frame_recv(&frame, output, &n, 100) < 0);
        assert(output[0] == 0xa5);
    }

    memset(&x, 0, sizeof x);
    memcpy(x.incoming, normal, sizeof normal);
    x.length = sizeof normal; x.chunk = 20;
    nfc_frame_init(&frame, &usb, NFC_FRAME_NORMAL, false);
    size_t n = 1; /* output buffer too small */
    assert(nfc_frame_recv(&frame, output, &n, 100) == NFC_ERR);
    nfc_frame_init(&frame, &usb, NFC_FRAME_NORMAL, false);
    assert(nfc_frame_send(&frame, output, 255, 100) == NFC_ERR);
    assert(nfc_frame_send(&frame, output, 0, 100) == NFC_ERR);
    puts("Frame boundary tests passed");
    return 0;
}
