/* SPDX-License-Identifier: MIT */
#ifndef NFCIDM_SONY_H
#define NFCIDM_SONY_H
#include "nfc_usb.h"
typedef struct {
    nfc_kind kind;
    nfc_framer frame;
    bool initialized;
} nfc_sony;
/* Each init sets up frame and the reader. Each poll performs ONE bounded
 * FeliCa polling attempt; NFC_AGAIN means no FeliCa card. IDm byte order
 * is preserved exactly as received over the air. System code is FFFF. */
int nfc_s320_init(nfc_sony *s, nfc_usb *usb);
int nfc_s320_poll(nfc_sony *s, uint8_t idm[8]);
void nfc_s320_close(nfc_sony *s);
int nfc_s330_init(nfc_sony *s, nfc_usb *usb);
int nfc_s330_poll(nfc_sony *s, uint8_t idm[8]);
void nfc_s330_close(nfc_sony *s);
int nfc_s380_init(nfc_sony *s, nfc_usb *usb);
int nfc_s380_poll(nfc_sony *s, uint8_t idm[8]);
void nfc_s380_close(nfc_sony *s);
#endif
