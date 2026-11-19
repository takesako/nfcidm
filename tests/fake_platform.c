/* SPDX-License-Identifier: MIT */
/* CLI integration fixture, never included in the production executable. */
#define _POSIX_C_SOURCE 200809L
#include "nfc_usb.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct nfc_pcsc { int unused; };

static const char *mode(void)
{
    const char *s = getenv("NFCIDM_TEST_MODE");
    return s ? s : "success";
}

int nfc_usb_enumerate(nfc_reader *r, size_t cap, size_t *n, char *e, size_t ec)
{
    (void)r; (void)cap; (void)n; (void)e; (void)ec;
    return NFC_OK;
}

int nfc_pcsc_enumerate(nfc_reader *r, size_t cap, size_t *n, char *e, size_t ec)
{
    (void)e; (void)ec;
    unsigned count = !strcmp(mode(), "none") ? 0 : !strcmp(mode(), "multiple") ? 2 : 1;
    while (count-- && *n < cap) {
        memset(&r[*n], 0, sizeof r[*n]);
        r[*n].kind = NFC_PCSC;
        nfc_error(r[*n].name, sizeof r[*n].name, "ACS ACR122U test %zu", *n);
        (*n)++;
    }
    return NFC_OK;
}

int nfc_usb_open(const nfc_reader *r, bool v, nfc_usb *usb)
{
    (void)r; (void)v;
    memset(usb, 0, sizeof *usb);
    return NFC_ERR;
}

int nfc_pcsc_open(const nfc_reader *r, bool v, nfc_pcsc **out, char *e, size_t ec)
{
    (void)r; (void)v; (void)e; (void)ec;
    *out = calloc(1, sizeof **out);
    return *out ? NFC_OK : NFC_ERR;
}

int nfc_pcsc_poll(nfc_pcsc *p, uint8_t idm[8], unsigned ms, char *e, size_t ec)
{
    (void)p;
    if (!strcmp(mode(), "stall"))
        for (;;)
            pause();
    if (!strcmp(mode(), "timeout")) {
        nfc_sleep_ms(ms);
        return NFC_AGAIN;
    }
    if (!strcmp(mode(), "bad-card")) {
        nfc_error(e, ec, "card is not FeliCa");
        return NFC_BAD_CARD;
    }
    static const uint8_t value[8] = { 1, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
    memcpy(idm, value, sizeof value);
    return NFC_OK;
}

void nfc_pcsc_close(nfc_pcsc *p)
{
    free(p);
}
