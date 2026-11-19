/* SPDX-License-Identifier: MIT */
#include "nfcidm.h"
#include <PCSC/winscard.h>

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

volatile sig_atomic_t nfc_stopped;
const SCARD_IO_REQUEST nfcidm_mock_pci_t0 = { 1, 8 };
const SCARD_IO_REQUEST nfcidm_mock_pci_t1 = { 2, 8 };

static const char reader_name[] = "ACS ACR122U PICC Interface 0";
static const char sony_name[] = "SONY FeliCa RC-S300/P 00 00";
static const char reader_list[] =
    "Unrelated reader\0ACS ACR122U PICC Interface 0\0"
    "ACS ACR122U SAM Interface 0\0ACS ACR1222U PICC Interface 0\0"
    "SONY FeliCa RC-S300/P 00 00\0"
    "SONY FeliCa RC-S300/S 00 00\0"
    "SONY FeliCa RC-S300/S1 00 00\0"
    "SONY FeliCa Port/PaSoRi 4.0 0\0"
    "SONY FeliCa RC-S300/S1 SAM Interface 0\0";
static const uint8_t felica_atr[] = {
    0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0, 0,
    3, 6, 3, 0xf0, 0x11, 0, 0, 0, 0, 0x8a
};

static struct {
    uint32_t list_result, wait_result, connect_result, transmit_result;
    uint32_t events, wait_ms, last_current_state;
    const char *names;
    const char *active_reader;
    uint32_t names_length;
    uint8_t atr[33], response[258];
    uint32_t atr_length, response_length;
    unsigned establish_count, release_count, connect_count, transmit_count;
    unsigned disconnect_count;
    int interrupt_after_transmit;
} mock;

void nfc_error(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (out != NULL && cap != 0)
        (void)vsnprintf(out, cap, fmt, ap);
    va_end(ap);
}

int32_t SCardEstablishContext(uint32_t scope, const void *a, const void *b,
                              SCARDCONTEXT *context)
{
    assert(scope == SCARD_SCOPE_SYSTEM && a == NULL && b == NULL);
    *context = 100;
    mock.establish_count++;
    return 0;
}

int32_t SCardReleaseContext(SCARDCONTEXT context)
{
    assert(context == 100);
    mock.release_count++;
    return 0;
}

int32_t SCardListReaders(SCARDCONTEXT context, const char *groups,
                         char *names, uint32_t *length)
{
    assert(context == 100 && groups == NULL);
    if (mock.list_result != 0)
        return (int32_t)mock.list_result;
    if (names != NULL) {
        assert(*length >= mock.names_length);
        memcpy(names, mock.names, mock.names_length);
    }
    *length = mock.names_length;
    return 0;
}

int32_t SCardGetStatusChange(SCARDCONTEXT context, uint32_t wait,
                              SCARD_READERSTATE *state, uint32_t count)
{
    assert(context == 100 && count == 1);
    assert(strcmp(state->szReader, mock.active_reader) == 0);
    mock.wait_ms = wait;
    mock.last_current_state = state->dwCurrentState;
    state->dwEventState = mock.events;
    return (int32_t)mock.wait_result;
}

int32_t SCardConnect(SCARDCONTEXT context, const char *name, uint32_t share,
                     uint32_t preferred, SCARDHANDLE *card, uint32_t *protocol)
{
    assert(context == 100 && strcmp(name, mock.active_reader) == 0);
    assert(share == SCARD_SHARE_SHARED);
    assert(preferred == (SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1));
    mock.connect_count++;
    *card = 200;
    *protocol = SCARD_PROTOCOL_T1;
    return (int32_t)mock.connect_result;
}

int32_t SCardStatus(SCARDHANDLE card, char *name, uint32_t *name_length,
                    uint32_t *state, uint32_t *protocol,
                    unsigned char *atr, uint32_t *atr_length)
{
    size_t actual_length = strlen(mock.active_reader) + 1;
    assert(card == 200 && *name_length >= actual_length);
    assert(*atr_length >= mock.atr_length);
    memcpy(name, mock.active_reader, actual_length);
    *name_length = (uint32_t)actual_length;
    *state = SCARD_STATE_PRESENT;
    *protocol = SCARD_PROTOCOL_T1;
    memcpy(atr, mock.atr, mock.atr_length);
    *atr_length = mock.atr_length;
    return 0;
}

int32_t SCardTransmit(SCARDHANDLE card, LPCSCARD_IO_REQUEST pci,
                      const unsigned char *command, uint32_t command_length,
                      SCARD_IO_REQUEST *receive_pci, unsigned char *response,
                      uint32_t *response_length)
{
    static const uint8_t get_idm[] = { 0xff, 0xca, 0, 0, 0 };
    assert(card == 200 && pci == SCARD_PCI_T1 && receive_pci == NULL);
    assert(command_length == sizeof(get_idm));
    assert(memcmp(command, get_idm, sizeof(get_idm)) == 0);
    assert(*response_length >= mock.response_length);
    mock.transmit_count++;
    memcpy(response, mock.response, mock.response_length);
    *response_length = mock.response_length;
    if (mock.interrupt_after_transmit)
        nfc_stopped = 1;
    return (int32_t)mock.transmit_result;
}

int32_t SCardDisconnect(SCARDHANDLE card, uint32_t disposition)
{
    assert(card == 200 && disposition == SCARD_LEAVE_CARD);
    mock.disconnect_count++;
    return 0;
}

static void reset_mock(void)
{
    static const uint8_t response[] = {
        1, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x90, 0
    };
    memset(&mock, 0, sizeof(mock));
    nfc_stopped = 0;
    mock.names = reader_list;
    mock.active_reader = reader_name;
    mock.names_length = sizeof(reader_list);
    mock.events = SCARD_STATE_PRESENT | SCARD_STATE_CHANGED;
    memcpy(mock.atr, felica_atr, sizeof(felica_atr));
    mock.atr_length = sizeof(felica_atr);
    memcpy(mock.response, response, sizeof(response));
    mock.response_length = sizeof(response);
}

static nfc_pcsc *open_reader_named(const char *name)
{
    nfc_reader reader = {0};
    nfc_pcsc *pcsc = NULL;
    char error[NFCIDM_ERROR_SIZE];
    reader.kind = NFC_PCSC;
    assert(strlen(name) < sizeof(reader.pcsc_name));
    strcpy(reader.pcsc_name, name);
    mock.active_reader = name;
    assert(nfc_pcsc_open(&reader, false, &pcsc, error, sizeof(error)) == NFC_OK);
    assert(pcsc != NULL);
    return pcsc;
}

static nfc_pcsc *open_reader(void)
{
    return open_reader_named(reader_name);
}

static void test_enumeration(void)
{
    nfc_reader readers[8];
    size_t count = 0;
    char error[NFCIDM_ERROR_SIZE];
    reset_mock();
    assert(nfc_pcsc_enumerate(readers, 8, &count, error, sizeof(error)) == NFC_OK);
    assert(count == 5 && readers[0].kind == NFC_PCSC);
    assert(strcmp(readers[0].pcsc_name, reader_name) == 0);
    assert(readers[0].vid == 0x072f && readers[0].pid == 0x2200);
    assert(strcmp(readers[1].pcsc_name, sony_name) == 0);
    for (size_t i = 1; i < count; i++) {
        assert(readers[i].kind == NFC_PCSC);
        assert(readers[i].vid == 0 && readers[i].pid == 0);
    }
    assert(strcmp(readers[2].pcsc_name, "SONY FeliCa RC-S300/S 00 00") == 0);
    assert(strcmp(readers[3].pcsc_name, "SONY FeliCa RC-S300/S1 00 00") == 0);
    assert(strcmp(readers[4].pcsc_name, "SONY FeliCa Port/PaSoRi 4.0 0") == 0);
    assert(mock.establish_count == mock.release_count);

    count = 0;
    mock.names_length--; /* Remove the terminating empty MULTI_SZ element. */
    assert(nfc_pcsc_enumerate(readers, 8, &count, error, sizeof(error)) == NFC_ERR);
    assert(count == 0 && mock.establish_count == mock.release_count);

    mock.list_result = SCARD_E_NO_READERS_AVAILABLE;
    assert(nfc_pcsc_enumerate(readers, 8, &count, error, sizeof(error)) == NFC_OK);
    assert(count == 0 && mock.establish_count == mock.release_count);
}

static void test_poll(void)
{
    nfc_pcsc *pcsc;
    uint8_t idm[8], unchanged[8];
    char error[NFCIDM_ERROR_SIZE];

    memset(unchanged, 0xa5, sizeof(unchanged));
    reset_mock();
    pcsc = open_reader();
    assert(nfc_pcsc_poll(pcsc, idm, 25, error, sizeof(error)) == NFC_OK);
    assert(memcmp(idm, mock.response, sizeof(idm)) == 0);
    assert(mock.wait_ms == 25 && mock.connect_count == 1);
    assert(mock.transmit_count == 1 && mock.disconnect_count == 1);

    mock.wait_result = SCARD_E_TIMEOUT;
    assert(nfc_pcsc_poll(pcsc, idm, 7, error, sizeof(error)) == NFC_AGAIN);
    assert(mock.wait_ms == 7 && mock.transmit_count == 1);
    assert(mock.last_current_state == SCARD_STATE_PRESENT);
    mock.wait_result = 0;
    mock.events = SCARD_STATE_UNAVAILABLE | SCARD_STATE_CHANGED;
    assert(nfc_pcsc_poll(pcsc, idm, 0, error, sizeof(error)) == NFC_GONE);
    nfc_pcsc_close(pcsc);
    assert(mock.establish_count == mock.release_count);

    reset_mock();
    pcsc = open_reader();
    mock.atr[13] = 0; /* A valid MIFARE Classic ATR, not a FeliCa card. */
    mock.atr[14] = 1;
    mock.atr[19] = 0x6a;
    memcpy(idm, unchanged, sizeof(idm));
    assert(nfc_pcsc_poll(pcsc, idm, 10, error, sizeof(error)) == NFC_BAD_CARD);
    assert(mock.transmit_count == 0 && mock.disconnect_count == 1);
    assert(memcmp(idm, unchanged, sizeof(idm)) == 0);
    nfc_pcsc_close(pcsc);

    reset_mock();
    pcsc = open_reader();
    mock.response_length = 9;
    assert(nfc_pcsc_poll(pcsc, idm, 10, error, sizeof(error)) == NFC_ERR);
    assert(mock.disconnect_count == 1);
    assert(memcmp(idm, unchanged, sizeof(idm)) == 0);
    nfc_pcsc_close(pcsc);

    reset_mock();
    pcsc = open_reader();
    mock.transmit_result = SCARD_W_REMOVED_CARD;
    assert(nfc_pcsc_poll(pcsc, idm, 10, error, sizeof(error)) == NFC_AGAIN);
    assert(mock.disconnect_count == 1);
    assert(memcmp(idm, unchanged, sizeof(idm)) == 0);
    mock.transmit_result = 0;
    mock.interrupt_after_transmit = 1;
    assert(nfc_pcsc_poll(pcsc, idm, 10, error, sizeof(error)) == NFC_CANCELLED);
    assert(mock.last_current_state == SCARD_STATE_UNAWARE);
    assert(mock.disconnect_count == 2);
    assert(memcmp(idm, unchanged, sizeof(idm)) == 0);
    nfc_pcsc_close(pcsc);
    assert(mock.establish_count == mock.release_count);
}

static void test_sony_rcs300_poll(void)
{
    static const uint8_t sony_felica_atr[] = {
        0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0, 0,
        3, 6, 0x11, 0x00, 0x3b, 0, 0, 0, 0, 0x42
    };
    nfc_pcsc *pcsc;
    uint8_t idm[8], unchanged[8];
    char error[NFCIDM_ERROR_SIZE];

    reset_mock();
    pcsc = open_reader_named(sony_name);
    memcpy(mock.atr, sony_felica_atr, sizeof(sony_felica_atr));
    mock.atr_length = sizeof(sony_felica_atr);
    assert(nfc_pcsc_poll(pcsc, idm, 25, error, sizeof(error)) == NFC_OK);
    assert(memcmp(idm, mock.response, 8) == 0);
    assert(mock.connect_count == 1 && mock.transmit_count == 1);
    assert(mock.disconnect_count == 1);
    nfc_pcsc_close(pcsc);
    assert(mock.establish_count == mock.release_count);

    /* Sony must not report other card types' eight-byte UID as an IDm. */
    reset_mock();
    pcsc = open_reader_named(sony_name);
    mock.atr[13] = 0; /* MIFARE Classic, not FeliCa. */
    mock.atr[14] = 1;
    mock.atr[19] = 0x6a;
    memset(idm, 0xa5, sizeof(idm));
    memset(unchanged, 0xa5, sizeof(unchanged));
    assert(nfc_pcsc_poll(pcsc, idm, 25, error, sizeof(error)) == NFC_BAD_CARD);
    assert(mock.transmit_count == 0 && mock.disconnect_count == 1);
    assert(memcmp(idm, unchanged, 8) == 0);
    nfc_pcsc_close(pcsc);
    assert(mock.establish_count == mock.release_count);
}

int main(void)
{
    test_enumeration();
    test_poll();
    test_sony_rcs300_poll();
    puts("PC/SC backend mock tests passed");
    return 0;
}
