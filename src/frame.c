/* SPDX-License-Identifier: MIT */
#include "nfc_usb.h"
#include <string.h>

#define MAX_PAYLOAD 1024U

void nfc_frame_init(nfc_framer *f, nfc_usb *usb, nfc_frame_type type, bool control_out)
{
    memset(f, 0, sizeof *f);
    f->usb = usb;
    f->type = type;
    f->control_out = control_out;
}

static int send_raw(nfc_framer *f, const uint8_t *p, size_t n, unsigned ms)
{
    if (f->control_out)
        return nfc_usb_control(f->usb, 0, 0, 0, p, n, ms);
    return nfc_usb_write(f->usb, p, n, ms);
}

int nfc_frame_ack(nfc_framer *f, unsigned ms)
{
    static const uint8_t ack[] = { 0, 0, 0xff, 0, 0xff, 0 };
    return send_raw(f, ack, sizeof ack, ms);
}

int nfc_frame_send(nfc_framer *f, const uint8_t *data, size_t size, unsigned ms)
{
    uint8_t packet[MAX_PAYLOAD + 10];
    uint8_t sum = 0;
    size_t h;
    bool extended = f->type == NFC_FRAME_PORT100 || size > 254;
    if (size == 0 || size > MAX_PAYLOAD || (extended && f->type == NFC_FRAME_NORMAL)) {
        nfc_error(f->usb->error, sizeof f->usb->error, "invalid command frame length (must be 1..%u bytes)", (unsigned)MAX_PAYLOAD);
        return NFC_ERR;
    }
    packet[0] = packet[1] = 0;
    packet[2] = 0xff;
    if (extended) {
        packet[3] = packet[4] = 0xff;
        packet[5] = (uint8_t)(f->type == NFC_FRAME_PORT100 ? size : size >> 8);
        packet[6] = (uint8_t)(f->type == NFC_FRAME_PORT100 ? size >> 8 : size);
        packet[7] = (uint8_t)(0U - packet[5] - packet[6]);
        h = 8;
    } else {
        packet[3] = (uint8_t)size;
        packet[4] = (uint8_t)(0U - size);
        h = 5;
    }
    memcpy(packet + h, data, size);
    for (size_t i = 0; i < size; i++)
        sum = (uint8_t)(sum + data[i]);
    packet[h + size] = (uint8_t)(0U - sum);
    packet[h + size + 1] = 0;
    return send_raw(f, packet, h + size + 2, ms);
}

static void consume(nfc_framer *f, size_t n)
{
    f->used -= n;
    memmove(f->input, f->input + n, f->used);
}

static int bad_frame(nfc_framer *f, const char *reason)
{
    nfc_error(f->usb->error, sizeof f->usb->error, "invalid USB frame: %s", reason);
    f->used = 0;
    return NFC_ERR;
}

int nfc_frame_recv(nfc_framer *f, uint8_t *data, size_t *size, unsigned ms)
{
    uint64_t end = nfc_now_ms() + ms;
    const size_t capacity = *size;
    for (;;) {
        size_t h = 5, length = 0, total = 0;
        uint8_t *p = f->input;
        if (f->used >= 3 && (p[0] != 0 || p[1] != 0 || p[2] != 0xff))
            return bad_frame(f, "preamble");
        if (f->used >= 6) {
            if (p[3] == 0 && p[4] == 0xff && p[5] == 0) {
                consume(f, 6);
                *size = 0;
                return NFC_FRAME_ACK;
            }
            if (p[3] == 0xff && p[4] == 0 && p[5] == 0) {
                consume(f, 6);
                *size = 0;
                return NFC_FRAME_NACK;
            }
            if (p[3] == 0xff && p[4] == 0xff) {
                h = 8;
                if (f->type == NFC_FRAME_NORMAL)
                    return bad_frame(f, "unexpected extended frame");
                if (f->used >= h) {
                    if ((uint8_t)(p[5] + p[6] + p[7]) != 0)
                        return bad_frame(f, "extended length checksum");
                    length = f->type == NFC_FRAME_PORT100 ?
                        (size_t)p[5] | ((size_t)p[6] << 8) :
                        ((size_t)p[5] << 8) | p[6];
                }
            } else {
                if ((uint8_t)(p[3] + p[4]) != 0)
                    return bad_frame(f, "length checksum");
                length = p[3];
            }
            if (f->used >= h) {
                if (length == 0 || length > MAX_PAYLOAD || length > capacity)
                    return bad_frame(f, "payload length exceeds limit");
                total = h + length + 2;
            }
            if (total && f->used >= total) {
                uint8_t sum = 0;
                for (size_t i = h; i <= h + length; i++)
                    sum = (uint8_t)(sum + p[i]);
                if (sum != 0 || p[total - 1] != 0)
                    return bad_frame(f, "data checksum or postamble");
                memcpy(data, p + h, length);
                consume(f, total);
                *size = length;
                return NFC_FRAME_DATA;
            }
        }
        uint64_t now = nfc_now_ms();
        if (now >= end) {
            nfc_error(f->usb->error, sizeof f->usb->error,
                      f->used ? "timed out in a partial USB frame" : "USB response timed out");
            return NFC_TIMEOUT;
        }
        if (f->used == sizeof f->input)
            return bad_frame(f, "receive buffer full");
        int n = nfc_usb_read(f->usb, f->input + f->used,
                             sizeof f->input - f->used, (unsigned)(end - now));
        if (n < 0)
            return n;
        if (n == 0)
            return bad_frame(f, "empty USB transfer");
        f->used += (size_t)n;
    }
}

int nfc_frame_command(nfc_framer *f, const uint8_t *command, size_t command_size,
                       uint8_t *response, size_t *response_size, unsigned ms)
{
    uint64_t end = nfc_now_ms() + ms;
    size_t capacity = *response_size;
    f->command_acked = false;
    int rc = nfc_frame_send(f, command, command_size, ms);
    if (rc < 0)
        return rc;
    for (unsigned acknowledgements = 0; acknowledgements < 8; acknowledgements++) {
        uint64_t now = nfc_now_ms();
        if (now >= end) {
            nfc_error(f->usb->error, sizeof f->usb->error, "reader command timed out");
            return NFC_TIMEOUT;
        }
        *response_size = capacity;
        rc = nfc_frame_recv(f, response, response_size, (unsigned)(end - now));
        if (rc == NFC_FRAME_DATA)
            return NFC_OK;
        if (rc < 0)
            return rc;
        if (rc == NFC_FRAME_NACK) {
            nfc_error(f->usb->error, sizeof f->usb->error, "reader rejected command (NACK)");
            return NFC_ERR;
        }
        f->command_acked = true;
    }
    return bad_frame(f, "too many ACK frames");
}

void nfc_frame_drain(nfc_framer *f)
{
    uint8_t scratch[1024];
    char saved[NFCIDM_ERROR_SIZE];
    memcpy(saved, f->usb->error, sizeof saved);
    f->used = 0;
    for (unsigned i = 0; i < 8; i++) {
        if (nfc_usb_read(f->usb, scratch, sizeof scratch, 10) <= 0)
            break;
    }
    memcpy(f->usb->error, saved, sizeof saved);
}
