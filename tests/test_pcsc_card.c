/* SPDX-License-Identifier: MIT */
#include "pcsc_card.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t acr122u_felica_212[] = {
    0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0x00, 0x00,
    0x03, 0x06, 0x03, 0xf0, 0x11, 0x00, 0x00, 0x00, 0x00, 0x8a
};

static const uint8_t acr122u_felica_424[] = {
    0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0x00, 0x00,
    0x03, 0x06, 0x03, 0xf0, 0x12, 0x00, 0x00, 0x00, 0x00, 0x89
};

static const uint8_t standard_felica[] = {
    0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0x00, 0x00,
    0x03, 0x06, 0x11, 0x00, 0x3b, 0x00, 0x00, 0x00, 0x00, 0x42
};

static const uint8_t mifare_classic[] = {
    0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0x00, 0x00,
    0x03, 0x06, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x6a
};

static const uint8_t iso15693[] = {
    0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0x00, 0x00,
    0x03, 0x06, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63
};

static void test_reader_names(void)
{
    assert(nfcidm_pcsc_reader_is_acr122u("ACS ACR122U PICC Interface 0"));
    assert(nfcidm_pcsc_reader_is_acr122u("ACS ACR122 00 00"));
    assert(nfcidm_pcsc_reader_is_acr122u("acs acr122u-a9 picc interface"));
    assert(!nfcidm_pcsc_reader_is_acr122u("ACS ACR122U SAM Interface 0"));
    assert(!nfcidm_pcsc_reader_is_acr122u("ACS ACR122U ICC Interface 0"));
    assert(!nfcidm_pcsc_reader_is_acr122u("ACS ACR122U-SAM"));
    assert(!nfcidm_pcsc_reader_is_acr122u("ACS ACR1222U PICC Interface 0"));
    assert(!nfcidm_pcsc_reader_is_acr122u("ACS ACR1252U PICC Interface 0"));
    assert(!nfcidm_pcsc_reader_is_acr122u("SONY FeliCa RC-S300/P"));
    assert(!nfcidm_pcsc_reader_is_acr122u("Other ACR122U PICC Interface 0"));
    assert(!nfcidm_pcsc_reader_is_acr122u("NotACS ACR122U"));
    assert(!nfcidm_pcsc_reader_is_acr122u("ACS ACR122Unknown"));
    assert(!nfcidm_pcsc_reader_is_acr122u(""));
    assert(!nfcidm_pcsc_reader_is_acr122u(NULL));

    assert(nfcidm_pcsc_reader_model("ACS ACR122U PICC Interface 0") ==
           NFCIDM_PCSC_ACR122U);
    assert(nfcidm_pcsc_reader_model("SONY FeliCa RC-S300/P 00 00") ==
           NFCIDM_PCSC_SONY_RCS300);
    assert(nfcidm_pcsc_reader_model("SONY FeliCa RC-S300/S 00 00") ==
           NFCIDM_PCSC_SONY_RCS300);
    assert(nfcidm_pcsc_reader_model("SONY FeliCa RC-S300/S1 00 00") ==
           NFCIDM_PCSC_SONY_RCS300);
    assert(nfcidm_pcsc_reader_model("SONY FeliCa Port/PaSoRi 4.0 0") ==
           NFCIDM_PCSC_SONY_RCS300);
    assert(nfcidm_pcsc_reader_model("sony felica rc-s300/p") ==
           NFCIDM_PCSC_SONY_RCS300);
    assert(nfcidm_pcsc_reader_model("SONY FeliCa RC-S300/S1 SAM") ==
           NFCIDM_PCSC_UNSUPPORTED);
    assert(nfcidm_pcsc_reader_model("SONY FeliCa RC-S300/S1 ICC") ==
           NFCIDM_PCSC_UNSUPPORTED);
    assert(nfcidm_pcsc_reader_model("SONY FeliCa RC-S380/P") ==
           NFCIDM_PCSC_UNSUPPORTED);
    assert(nfcidm_pcsc_reader_model("SONY FeliCa RC-S300EXTRA") ==
           NFCIDM_PCSC_UNSUPPORTED);
    assert(nfcidm_pcsc_reader_model("SONY FeliCa Port/PaSoRi 3.0 0") ==
           NFCIDM_PCSC_UNSUPPORTED);
    assert(nfcidm_pcsc_reader_model("Unknown FeliCa RC-S300/P") ==
           NFCIDM_PCSC_UNSUPPORTED);
    assert(nfcidm_pcsc_reader_model("NotSONY FeliCa RC-S300/P") ==
           NFCIDM_PCSC_UNSUPPORTED);
    assert(nfcidm_pcsc_reader_model(NULL) == NFCIDM_PCSC_UNSUPPORTED);
}

static void test_atr(void)
{
    uint8_t damaged[sizeof(acr122u_felica_212) + 1];
    size_t i;

    assert(nfcidm_pcsc_atr_is_felica(acr122u_felica_212,
                                    sizeof(acr122u_felica_212)));
    assert(nfcidm_pcsc_atr_is_felica(acr122u_felica_424,
                                    sizeof(acr122u_felica_424)));
    assert(nfcidm_pcsc_atr_is_felica(standard_felica,
                                    sizeof(standard_felica)));
    assert(!nfcidm_pcsc_atr_is_felica(mifare_classic,
                                     sizeof(mifare_classic)));
    /* ISO15693 also has an eight-byte UID; it must not be accepted as IDm. */
    assert(!nfcidm_pcsc_atr_is_felica(iso15693, sizeof(iso15693)));
    assert(!nfcidm_pcsc_atr_is_felica(NULL, 20));

    for (i = 0; i < sizeof(acr122u_felica_212); i++)
        assert(!nfcidm_pcsc_atr_is_felica(acr122u_felica_212, i));

    memcpy(damaged, acr122u_felica_212, sizeof(acr122u_felica_212));
    damaged[20] = 0;
    assert(!nfcidm_pcsc_atr_is_felica(damaged, sizeof(damaged)));
    damaged[19] ^= 1;
    assert(!nfcidm_pcsc_atr_is_felica(damaged, 20));
    damaged[19] ^= 1;
    damaged[12] ^= 0x01;
    damaged[19] ^= 0x01; /* Valid checksum, unsupported card identifier. */
    assert(!nfcidm_pcsc_atr_is_felica(damaged, 20));
}

static void test_response(void)
{
    uint8_t response[] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x90, 0x00, 0x00
    };
    uint8_t idm[8];
    const uint8_t unchanged[8] = { 0xa5, 0xa5, 0xa5, 0xa5,
                                   0xa5, 0xa5, 0xa5, 0xa5 };
    size_t i;

    assert(nfcidm_pcsc_parse_idm(response, 10, idm) == 0);
    assert(memcmp(idm, response, sizeof(idm)) == 0);

    memcpy(idm, unchanged, sizeof(idm));
    for (i = 0; i < 10; i++) {
        assert(nfcidm_pcsc_parse_idm(response, i, idm) == -1);
        assert(memcmp(idm, unchanged, sizeof(idm)) == 0);
    }
    assert(nfcidm_pcsc_parse_idm(response, 11, idm) == -1);
    assert(nfcidm_pcsc_parse_idm(NULL, 10, idm) == -1);
    assert(nfcidm_pcsc_parse_idm(response, 10, NULL) == -1);
    response[8] = 0x63;
    assert(nfcidm_pcsc_parse_idm(response, 10, idm) == -1);
    assert(memcmp(idm, unchanged, sizeof(idm)) == 0);
    response[8] = 0x90;
    response[9] = 0x01;
    assert(nfcidm_pcsc_parse_idm(response, 10, idm) == -1);
    assert(memcmp(idm, unchanged, sizeof(idm)) == 0);
}

int main(void)
{
    test_reader_names();
    test_atr();
    test_response();
    puts("PC/SC card parsing tests passed");
    return 0;
}
