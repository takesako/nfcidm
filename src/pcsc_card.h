/* SPDX-License-Identifier: MIT */
#ifndef NFCIDM_PCSC_CARD_H
#define NFCIDM_PCSC_CARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* PC/SC provides reader names, not reliable USB VID/PID information.
 * Recognize only known NFC/FeliCa models and exclude SAM/contact slots.
 */
typedef enum {
    NFCIDM_PCSC_UNSUPPORTED = 0,
    NFCIDM_PCSC_ACR122U,
    NFCIDM_PCSC_SONY_RCS300
} nfcidm_pcsc_model;
nfcidm_pcsc_model nfcidm_pcsc_reader_model(const char *name);
bool nfcidm_pcsc_reader_is_acr122u(const char *name);

/* Recognize documented PC/SC synthetic FeliCa ATRs, including ACR122U. */
bool nfcidm_pcsc_atr_is_felica(const uint8_t *atr, size_t length);

/* Return 0 for exactly eight IDm bytes followed by 90 00, otherwise -1.
 * The output is left unchanged on failure. Call only after identifying FeliCa.
 */
int nfcidm_pcsc_parse_idm(const uint8_t *response, size_t length,
                        uint8_t idm[8]);

#endif
