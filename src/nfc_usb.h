/* SPDX-License-Identifier: MIT */
#ifndef NFCIDM_USB_H
#define NFCIDM_USB_H
#include "nfcidm.h"

typedef struct nfc_usb nfc_usb;
struct nfc_usb {
    void *opaque;
    uint16_t vid, pid;
    bool verbose;
    char error[NFCIDM_ERROR_SIZE];
    /* Raw USB transfer callbacks; read may return split or combined frames.
     * write/control return NFC_OK or negative status, read returns byte
     * count or negative status. Timeouts MUST return NFC_TIMEOUT. */
    int (*write_fn)(nfc_usb *, const uint8_t *, size_t, unsigned);
    int (*read_fn)(nfc_usb *, uint8_t *, size_t, unsigned);
    int (*control_fn)(nfc_usb *, uint8_t request, uint16_t value,
                      uint16_t index, const uint8_t *, size_t, unsigned);
    void (*close_fn)(nfc_usb *);
};
int nfc_usb_open(const nfc_reader *reader, bool verbose, nfc_usb *usb);
void nfc_usb_close(nfc_usb *usb);
int nfc_usb_write(nfc_usb *usb, const uint8_t *data, size_t size, unsigned ms);
int nfc_usb_read(nfc_usb *usb, uint8_t *data, size_t size, unsigned ms);
int nfc_usb_control(nfc_usb *usb, uint8_t request, uint16_t value,
                    uint16_t index, const uint8_t *data, size_t size, unsigned ms);

/* Normal PN53x/PaSoRi frames and Port-100 extended frames. Extended
 * lengths are big endian on PN53x, little endian on Port-100. */
typedef enum { NFC_FRAME_NORMAL, NFC_FRAME_PN53X, NFC_FRAME_PORT100 } nfc_frame_type;
typedef struct {
    nfc_usb *usb;
    bool control_out;
    nfc_frame_type type;
    bool command_acked;
    uint8_t input[4096];
    size_t used;
} nfc_framer;
enum { NFC_FRAME_DATA = 0, NFC_FRAME_ACK = 1, NFC_FRAME_NACK = 2 };
void nfc_frame_init(nfc_framer *f, nfc_usb *usb, nfc_frame_type type, bool control_out);
int nfc_frame_send(nfc_framer *f, const uint8_t *data, size_t size, unsigned ms);
int nfc_frame_ack(nfc_framer *f, unsigned ms);
/* *size is capacity on entry and payload length on successful DATA exit. */
int nfc_frame_recv(nfc_framer *f, uint8_t *data, size_t *size, unsigned ms);
/* Send one command, consume ACK(s), return the first valid data payload.
 * Does not automatically send a host ACK after the response. */
int nfc_frame_command(nfc_framer *f, const uint8_t *command, size_t command_size,
                       uint8_t *response, size_t *response_size, unsigned ms);
/* Discard at most 8 short reads and reset the stream buffer. */
void nfc_frame_drain(nfc_framer *f);

#endif
