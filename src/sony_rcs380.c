/* SPDX-License-Identifier: MIT */
#include "sony.h"

#include <string.h>

/* Sony NFC Port-100 wire protocol (RC-S380/S and RC-S380/P).
 * Protocol facts cross-checked with the nfcpy RC-S380 driver and the Linux
 * Port-100 driver. This is an IDm-only implementation, not a driver port.
 * See docs/protocol.md for the wire protocol and references.
 */
enum {
    P100_SET_RF = 0x00,
    P100_SET_PROTOCOL = 0x02,
    P100_COMM_RF = 0x04,
    P100_SWITCH_RF = 0x06,
    P100_SET_COMMAND_TYPE = 0x2a,
    P100_USB_TIMEOUT_MS = 500
};

static int port100_exchange(nfc_sony *s, uint8_t command,
                           const uint8_t *parameters, size_t parameter_size,
                           uint8_t *reply, size_t *reply_size, unsigned timeout_ms)
{
    uint8_t request[64];
    int rc;

    if (parameter_size > sizeof(request) - 2) {
        nfc_error(s->frame.usb->error, sizeof(s->frame.usb->error),
                  "RC-S380 command is too long");
        return NFC_ERR;
    }
    request[0] = 0xd6;
    request[1] = command;
    if (parameter_size)
        memcpy(request + 2, parameters, parameter_size);

    rc = nfc_frame_command(&s->frame, request, parameter_size + 2,
                           reply, reply_size, timeout_ms);
    if (rc != NFC_OK)
        return rc;
    if (*reply_size < 2 || reply[0] != 0xd7 ||
        reply[1] != (uint8_t)(command + 1)) {
        nfc_error(s->frame.usb->error, sizeof(s->frame.usb->error),
                  "RC-S380 returned an invalid response to command 0x%02x",
                  (unsigned)command);
        return NFC_ERR;
    }
    *reply_size -= 2;
    memmove(reply, reply + 2, *reply_size);
    return NFC_OK;
}

static int port100_configure(nfc_sony *s, uint8_t command,
                            const uint8_t *parameters, size_t parameter_size,
                            unsigned timeout_ms)
{
    uint8_t reply[8];
    size_t reply_size = sizeof(reply);
    int rc = port100_exchange(s, command, parameters, parameter_size,
                              reply, &reply_size, timeout_ms);
    if (rc != NFC_OK)
        return rc;
    if (reply_size != 1) {
        nfc_error(s->frame.usb->error, sizeof(s->frame.usb->error),
                  "RC-S380 returned an invalid status length for command 0x%02x",
                  (unsigned)command);
        return NFC_ERR;
    }
    if (reply[0] != 0) {
        nfc_error(s->frame.usb->error, sizeof(s->frame.usb->error),
                  "RC-S380 rejected command 0x%02x (status 0x%02x)",
                  (unsigned)command, (unsigned)reply[0]);
        return NFC_ERR;
    }
    return NFC_OK;
}

int nfc_s380_init(nfc_sony *s, nfc_usb *usb)
{
    const uint8_t command_type = 1;
    const uint8_t rf_off = 0;
    /* Transmit table 1 / receive table 15, NFC-F at 212 kbit/s. */
    static const uint8_t rf[] = {1, 1, 15, 1};
    /* InSetProtocol is a sequence of parameter-number/value pairs.
     * Explicitly reset all relevant fields left by previous applications.
     */
    static const uint8_t protocol[] = {
        0x00, 24, /* Initial guard time. */
        0x01, 1,  /* Add the NFC-F CRC on transmission. */
        0x02, 1,  /* Check and remove the CRC on reception. */
        0x03, 0,  /* One card, not multi-card reception. */
        0x04, 0,  /* No ISO 14443-A parity on transmission. */
        0x05, 0,  /* No ISO 14443-A parity on reception. */
        0x06, 0,  /* No bitwise anti-collision mode. */
        0x07, 8,  /* Eight valid bits in the last transmit byte. */
        0x08, 0,  /* Disable Crypto1. */
        0x09, 0,  /* Disable Type-B start-of-frame insertion. */
        0x0a, 0,  /* Disable Type-B start-of-frame checking. */
        0x0b, 0,  /* Disable Type-B end-of-frame insertion. */
        0x0c, 0,  /* Disable Type-B end-of-frame checking. */
        0x0e, 4,  /* Deaf time after transmission. */
        0x0f, 0,  /* Disable continuous receive mode. */
        0x10, 0,  /* No minimum-length filter for that mode. */
        0x11, 0,  /* Disable Type-1 tag special framing. */
        0x12, 0,  /* Disable RF collision avoidance. */
        0x13, 6   /* Guard time as initiator. */
    };
    int rc;

    memset(s, 0, sizeof(*s));
    s->kind = NFC_S380;
    nfc_frame_init(&s->frame, usb, NFC_FRAME_PORT100, false);

    /* A host ACK cancels an unfinished command from an earlier session. */
    rc = nfc_frame_ack(&s->frame, P100_USB_TIMEOUT_MS);
    if (rc != NFC_OK)
        return rc;
    nfc_frame_drain(&s->frame);

    rc = port100_configure(s, P100_SET_COMMAND_TYPE, &command_type, 1,
                           P100_USB_TIMEOUT_MS);
    if (rc == NFC_OK)
        rc = port100_configure(s, P100_SWITCH_RF, &rf_off, 1,
                               P100_USB_TIMEOUT_MS);
    if (rc == NFC_OK)
        rc = port100_configure(s, P100_SET_RF, rf, sizeof(rf),
                               P100_USB_TIMEOUT_MS);
    if (rc == NFC_OK)
        rc = port100_configure(s, P100_SET_PROTOCOL, protocol, sizeof(protocol),
                               P100_USB_TIMEOUT_MS);
    if (rc == NFC_OK)
        s->initialized = true;
    return rc;
}

int nfc_s380_poll(nfc_sony *s, uint8_t idm[8])
{
    /* RF receive timeout: 110 * 0.1 ms = 11 ms.
     * FeliCa Polling: LEN=6, command=0, system=FFFF, request=0, slot=0.
     * Request 0 asks only for IDm and PMm, giving an 18-byte RF response.
     */
    static const uint8_t poll[] = {0x6e, 0x00, 6, 0, 0xff, 0xff, 0, 0};
    uint8_t reply[64];
    size_t size = sizeof(reply);
    uint32_t status;
    int rc;

    if (!s->initialized) {
        nfc_error(s->frame.usb->error, sizeof(s->frame.usb->error),
                  "RC-S380 has not been initialized");
        return NFC_ERR;
    }
    rc = port100_exchange(s, P100_COMM_RF, poll, sizeof(poll), reply, &size,
                          P100_USB_TIMEOUT_MS);
    if (rc != NFC_OK)
        return rc;

    /* Error responses may have only the four-byte RF status. */
    if (size < 4) {
        nfc_error(s->frame.usb->error, sizeof(s->frame.usb->error),
                  "RC-S380 returned a truncated RF status");
        return NFC_ERR;
    }
    status = (uint32_t)reply[0] | ((uint32_t)reply[1] << 8) |
             ((uint32_t)reply[2] << 16) | ((uint32_t)reply[3] << 24);
    if (status == 0x80)
        return NFC_AGAIN; /* A completed polling command found no card. */
    if (status != 0) {
        nfc_error(s->frame.usb->error, sizeof(s->frame.usb->error),
                  "RC-S380 RF communication failed (status 0x%08x)",
                  (unsigned)status);
        return NFC_ERR;
    }

    /* Status[4], collision information[1], LEN, response=1, IDm[8], PMm[8].
     * Check the complete Polling response before exposing any ID bytes.
     */
    if (size != 23 || reply[5] != 18 || reply[6] != 1) {
        nfc_error(s->frame.usb->error, sizeof(s->frame.usb->error),
                  "RC-S380 returned an invalid FeliCa Polling response");
        return NFC_ERR;
    }
    memcpy(idm, reply + 7, 8);
    return NFC_OK;
}

void nfc_s380_close(nfc_sony *s)
{
    uint8_t rf_off = 0;
    char saved_error[NFCIDM_ERROR_SIZE];

    if (!s || !s->frame.usb)
        return;
    memcpy(saved_error, s->frame.usb->error, sizeof(saved_error));
    /* Also try this after a partially completed initialization. */
    (void)port100_configure(s, P100_SWITCH_RF, &rf_off, 1, 100);
    (void)nfc_frame_ack(&s->frame, 100);
    memcpy(s->frame.usb->error, saved_error, sizeof(saved_error));
    s->initialized = false;
}
