/* SPDX-License-Identifier: MIT */
#ifndef NFCIDM_SONY_USB_IDS_H
#define NFCIDM_SONY_USB_IDS_H

#include "nfcidm.h"

/* Shared Sony VID/PID mapping: platform transports must not disagree about
 * which protocol engine a reader requires. */
#define NFC_SONY_USB_VID       0x054c
#define NFC_SONY_USB_S320      0x01bb
#define NFC_SONY_USB_S330      0x02e1
#define NFC_SONY_USB_S380_S    0x06c1
#define NFC_SONY_USB_S380_P    0x06c3

bool nfc_sony_usb_kind(uint16_t vid, uint16_t pid, nfc_kind *kind);
const char *nfc_sony_usb_model(uint16_t pid);

#endif
