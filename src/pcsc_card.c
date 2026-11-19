/* SPDX-License-Identifier: MIT */
#include "pcsc_card.h"

#include <string.h>

static unsigned char ascii_upper(unsigned char c)
{
    return c >= 'a' && c <= 'z' ? (unsigned char)(c - ('a' - 'A')) : c;
}

static bool ascii_alnum(unsigned char c)
{
    c = ascii_upper(c);
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static bool starts_with_ci(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (ascii_upper((unsigned char)*text) !=
            ascii_upper((unsigned char)*prefix))
            return false;
        text++;
        prefix++;
    }
    return true;
}

static bool contains_word_ci(const char *text, const char *word)
{
    size_t i, length = strlen(word);

    for (i = 0; text[i] != '\0'; i++) {
        if ((i == 0 || !ascii_alnum((unsigned char)text[i - 1])) &&
            starts_with_ci(text + i, word) &&
            !ascii_alnum((unsigned char)text[i + length]))
            return true;
    }
    return false;
}

nfcidm_pcsc_model nfcidm_pcsc_reader_model(const char *name)
{
    if (name == NULL || contains_word_ci(name, "SAM") ||
        contains_word_ci(name, "ICC") || contains_word_ci(name, "CONTACT"))
        return NFCIDM_PCSC_UNSUPPORTED;

    /* Reader names, not USB IDs: only the ACR122U PICC/CL slot is accepted. */
    if (contains_word_ci(name, "ACS") &&
        (contains_word_ci(name, "ACR122") ||
         contains_word_ci(name, "ACR122U")))
        return NFCIDM_PCSC_ACR122U;

    /* Sony's macOS PC/SC naming includes "SONY FeliCa RC-S300/P".
     * Some drivers identify the same family as "FeliCa Port/PaSoRi 4.0".
     * Unknown families and contact/SAM slots are deliberately ignored.
     */
    if (contains_word_ci(name, "SONY") &&
        contains_word_ci(name, "FELICA") &&
        (contains_word_ci(name, "RC-S300") ||
         (contains_word_ci(name, "PORT/PASORI") &&
          contains_word_ci(name, "4.0"))))
        return NFCIDM_PCSC_SONY_RCS300;

    return NFCIDM_PCSC_UNSUPPORTED;
}

bool nfcidm_pcsc_reader_is_acr122u(const char *name)
{
    return nfcidm_pcsc_reader_model(name) == NFCIDM_PCSC_ACR122U;
}

bool nfcidm_pcsc_atr_is_felica(const uint8_t *atr, size_t length)
{
    /* PC/SC storage-card ATR: T=1, 15 historical bytes, RID A000000306.
     * ACR122U API v2.03, sections 3.1 and 7.3:
     *   SS/C0/C1 = 03 F0 11 (212 kbps), 03 F0 12 (424 kbps).
     * Newer ACS readers document SS/C0/C1 = 11 00 3B for FeliCa.
     * Do not accept an arbitrary eight-byte UID as a FeliCa IDm.
     */
    static const uint8_t prefix[] = {
        0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f,
        0x0c, 0xa0, 0x00, 0x00, 0x03, 0x06
    };
    uint8_t check = 0;
    size_t i;

    if (atr == NULL || length != 20 ||
        memcmp(atr, prefix, sizeof(prefix)) != 0)
        return false;

    for (i = 1; i < length; i++)
        check ^= atr[i];
    if (check != 0)
        return false;

    /* The last four historical bytes are reserved in this ATR format. */
    if ((atr[15] | atr[16] | atr[17] | atr[18]) != 0)
        return false;

    return (atr[12] == 0x03 && atr[13] == 0xf0 &&
            (atr[14] == 0x11 || atr[14] == 0x12)) ||
           (atr[12] == 0x11 && atr[13] == 0x00 && atr[14] == 0x3b);
}

int nfcidm_pcsc_parse_idm(const uint8_t *response, size_t length,
                        uint8_t idm[8])
{
    if (response == NULL || idm == NULL || length != 10 ||
        response[8] != 0x90 || response[9] != 0x00)
        return -1;

    memcpy(idm, response, 8);
    return 0;
}
