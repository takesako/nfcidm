/* SPDX-License-Identifier: MIT */
#include "nfcidm.h"
#include "pcsc_card.h"

#include "pcsc_compat.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One PC/SC implementation for Apple PCSC, Windows WinSCard and pcsc-lite.
 * Keep the ABI-defined LONG/DWORD types and use explicit ANSI APIs on Windows.
 */
struct nfc_pcsc {
    SCARDCONTEXT context;
    nfc_pcsc_reader_state state;
    char name[NFCIDM_NAME_SIZE];
    bool verbose;
};

static const char *error_description(uint32_t code)
{
    switch (code) {
    case SCARD_E_NO_SMARTCARD: return "no card is present";
    case SCARD_W_REMOVED_CARD: return "the card was removed";
    case SCARD_W_RESET_CARD: return "the card was reset; retrying";
    case SCARD_E_READER_UNAVAILABLE: return "the reader was disconnected";
    case SCARD_E_UNKNOWN_READER: return "the reader is no longer available";
    case SCARD_E_NO_SERVICE: return "the PC/SC smart-card service is unavailable";
    case SCARD_E_SERVICE_STOPPED: return "the PC/SC smart-card service stopped";
    case SCARD_E_SHARING_VIOLATION: return "the reader is in use by another application";
    case SCARD_E_TIMEOUT: return "the operation timed out";
    case SCARD_E_CANCELLED: return "the operation was cancelled";
    case SCARD_W_UNRESPONSIVE_CARD: return "the card did not respond";
    case SCARD_W_UNPOWERED_CARD: return "the card is no longer powered";
    case SCARD_E_PROTO_MISMATCH: return "the card protocol is unsupported";
    case SCARD_E_INSUFFICIENT_BUFFER: return "the response exceeded its buffer";
    default: return "PC/SC communication failed";
    }
}

static int pcsc_failure(const char *operation, LONG result,
                        char *error, size_t error_cap)
{
    uint32_t code = (uint32_t)result;
    int status = NFC_ERR;

    if (nfc_stopped || code == (uint32_t)SCARD_E_CANCELLED)
        status = NFC_CANCELLED;
    else if (code == (uint32_t)SCARD_E_READER_UNAVAILABLE || code == (uint32_t)SCARD_E_UNKNOWN_READER)
        status = NFC_GONE;
    else if (code == (uint32_t)SCARD_E_NO_SMARTCARD || code == (uint32_t)SCARD_W_REMOVED_CARD ||
             code == (uint32_t)SCARD_W_RESET_CARD || code == (uint32_t)SCARD_W_UNPOWERED_CARD)
        status = NFC_AGAIN;

    nfc_error(error, error_cap, "%s: %s (0x%08" PRIx32 ")",
              operation, error_description(code), code);
    return status;
}

int nfc_pcsc_enumerate(nfc_reader *readers, size_t capacity, size_t *count,
                       char *error, size_t error_cap)
{
    SCARDCONTEXT context;
    char *names = NULL;
    DWORD names_length = 0;
    DWORD allocated = 0;
    LONG result;
    size_t start_count, offset;
    int attempt, status = NFC_OK;

    if (count == NULL || readers == NULL || *count > capacity) {
        nfc_error(error, error_cap, "Invalid PC/SC reader enumeration arguments");
        return NFC_ERR;
    }
    if (nfc_stopped)
        return NFC_CANCELLED;
    start_count = *count;
    result = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &context);
    if ((uint32_t)result != (uint32_t)SCARD_S_SUCCESS)
        return pcsc_failure("SCardEstablishContext", result, error, error_cap);

    /* A hotplug between the size query and read may change the MULTI_SZ. */
    for (attempt = 0; attempt < 3; attempt++) {
        names_length = 0;
        result = nfc_scard_list_readers(context, NULL, NULL, &names_length);
        if ((uint32_t)result == (uint32_t)SCARD_E_NO_READERS_AVAILABLE)
            goto done;
        if ((uint32_t)result != (uint32_t)SCARD_S_SUCCESS) {
            status = pcsc_failure("SCardListReaders", result, error, error_cap);
            goto done;
        }
        if (names_length == 0)
            goto done;
        if (names_length > 65536) {
            nfc_error(error, error_cap, "PC/SC reader list is unexpectedly large");
            status = NFC_ERR;
            goto done;
        }
        free(names);
        names = calloc(1, names_length);
        if (names == NULL) {
            nfc_error(error, error_cap, "Cannot allocate PC/SC reader list");
            status = NFC_ERR;
            goto done;
        }
        allocated = names_length;
        result = nfc_scard_list_readers(context, NULL, names, &names_length);
        if ((uint32_t)result == (uint32_t)SCARD_E_NO_READERS_AVAILABLE)
            goto done;
        if ((uint32_t)result == (uint32_t)SCARD_E_INSUFFICIENT_BUFFER)
            continue;
        if ((uint32_t)result != (uint32_t)SCARD_S_SUCCESS) {
            status = pcsc_failure("SCardListReaders", result, error, error_cap);
            goto done;
        }
        break;
    }
    if (attempt == 3 || names_length == 0 || names_length > allocated ||
        names[names_length - 1] != '\0' ||
        (names_length > 1 && names[names_length - 2] != '\0')) {
        nfc_error(error, error_cap, "PC/SC reader list changed or is malformed; retry");
        status = NFC_ERR;
        goto done;
    }

    for (offset = 0; offset < names_length && names[offset] != '\0'; ) {
        const char *name = names + offset;
        const char *end = memchr(name, '\0', names_length - offset);
        size_t length;
        nfc_reader *reader;
        nfcidm_pcsc_model model;

        if (end == NULL) {
            nfc_error(error, error_cap, "PC/SC returned an unterminated reader name");
            status = NFC_ERR;
            goto done;
        }
        length = (size_t)(end - name);
        offset += length + 1;
        model = nfcidm_pcsc_reader_model(name);
        if (model == NFCIDM_PCSC_UNSUPPORTED)
            continue;
        if (*count == capacity) {
            nfc_error(error, error_cap, "Too many supported NFC readers");
            status = NFC_ERR;
            goto done;
        }
        if (length >= NFCIDM_NAME_SIZE) {
            nfc_error(error, error_cap, "The supported PC/SC reader name is too long");
            status = NFC_ERR;
            goto done;
        }
        reader = &readers[(*count)++];
        memset(reader, 0, sizeof(*reader));
        reader->kind = NFC_PCSC;
        /* USB IDs are not provided by PC/SC. ACR122U IDs are a reference
         * value; keep Sony's VID/PID unknown instead of inventing them.
         */
        if (model == NFCIDM_PCSC_ACR122U) {
            reader->vid = 0x072f;
            reader->pid = 0x2200;
        }
        memcpy(reader->name, name, length + 1);
        memcpy(reader->pcsc_name, name, length + 1);
    }

done:
    free(names);
    (void)SCardReleaseContext(context);
    if (nfc_stopped)
        status = NFC_CANCELLED;
    if (status != NFC_OK)
        *count = start_count;
    return status;
}

int nfc_pcsc_open(const nfc_reader *reader, bool verbose, nfc_pcsc **out,
                  char *error, size_t error_cap)
{
    nfc_pcsc *pcsc;
    LONG result;

    if (out == NULL) {
        nfc_error(error, error_cap, "Invalid PC/SC output handle");
        return NFC_ERR;
    }
    *out = NULL;
    if (reader == NULL || reader->kind != NFC_PCSC ||
        memchr(reader->pcsc_name, '\0', sizeof(reader->pcsc_name)) == NULL ||
        nfcidm_pcsc_reader_model(reader->pcsc_name) == NFCIDM_PCSC_UNSUPPORTED) {
        nfc_error(error, error_cap, "This PC/SC backend supports ACR122U and SONY RC-S300 family readers only");
        return NFC_ERR;
    }
    if (nfc_stopped)
        return NFC_CANCELLED;
    pcsc = calloc(1, sizeof(*pcsc));
    if (pcsc == NULL) {
        nfc_error(error, error_cap, "Cannot allocate PC/SC reader state");
        return NFC_ERR;
    }
    result = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &pcsc->context);
    if ((uint32_t)result != (uint32_t)SCARD_S_SUCCESS) {
        free(pcsc);
        return pcsc_failure("SCardEstablishContext", result, error, error_cap);
    }
    memcpy(pcsc->name, reader->pcsc_name, strlen(reader->pcsc_name) + 1);
    pcsc->state.szReader = pcsc->name;
    pcsc->state.dwCurrentState = SCARD_STATE_UNAWARE;
    pcsc->verbose = verbose;
    *out = pcsc;
    return NFC_OK;
}

static void log_atr(const uint8_t *atr, size_t length)
{
    size_t i;

    fputs("nfcidm: PC/SC ATR:", stderr);
    for (i = 0; i < length; i++)
        fprintf(stderr, " %02X", (unsigned)atr[i]);
    fputc('\n', stderr);
}

int nfc_pcsc_poll(nfc_pcsc *pcsc, uint8_t idm[8], unsigned timeout_ms,
                  char *error, size_t error_cap)
{
    static const uint8_t get_idm[] = { 0xff, 0xca, 0x00, 0x00, 0x00 };
    SCARDHANDLE card;
    LPCSCARD_IO_REQUEST pci;
    char returned_name[NFCIDM_NAME_SIZE + 2];
    uint8_t atr[NFCIDM_PCSC_ATR_BUFFER_SIZE], response[258], value[8];
    DWORD protocol = 0, card_state = 0;
    DWORD name_length = (DWORD)sizeof(returned_name), atr_length = (DWORD)sizeof(atr);
    DWORD response_length = (DWORD)sizeof(response);
    DWORD wait_ms = (DWORD)timeout_ms;
    DWORD events;
    LONG result;
    int status = NFC_ERR;

    if (pcsc == NULL || idm == NULL) {
        nfc_error(error, error_cap, "Invalid PC/SC polling arguments");
        return NFC_ERR;
    }
    if (nfc_stopped)
        return NFC_CANCELLED;

    /* No infinite PC/SC card wait; main owns the overall deadline and Ctrl-C. */
    if (wait_ms == (DWORD)0xffffffffUL)
        wait_ms--;
    result = nfc_scard_get_status_change(pcsc->context, wait_ms,
                                  &pcsc->state, 1);
    if (nfc_stopped)
        return NFC_CANCELLED;
    if ((uint32_t)result == (uint32_t)SCARD_E_TIMEOUT)
        return NFC_AGAIN;
    if ((uint32_t)result != (uint32_t)SCARD_S_SUCCESS)
        return pcsc_failure("SCardGetStatusChange", result, error, error_cap);

    events = pcsc->state.dwEventState;
    pcsc->state.dwCurrentState = events & ~(DWORD)SCARD_STATE_CHANGED;
    if ((events & (SCARD_STATE_UNKNOWN | SCARD_STATE_UNAVAILABLE)) != 0) {
        nfc_error(error, error_cap, "The PC/SC NFC reader was disconnected");
        return NFC_GONE;
    }
    if ((events & SCARD_STATE_PRESENT) == 0)
        return NFC_AGAIN;
    if ((events & SCARD_STATE_MUTE) != 0) {
        nfc_error(error, error_cap, "The card is present but is not responding");
        return NFC_BAD_CARD;
    }

    result = nfc_scard_connect(pcsc->context, pcsc->name, SCARD_SHARE_SHARED,
                           SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
                           &card, &protocol);
    if ((uint32_t)result != (uint32_t)SCARD_S_SUCCESS) {
        pcsc->state.dwCurrentState = SCARD_STATE_UNAWARE;
        return pcsc_failure("SCardConnect", result, error, error_cap);
    }
    if (nfc_stopped) {
        status = NFC_CANCELLED;
        goto disconnect;
    }

    /* Obtain the ATR from this connection, not a stale insertion event. */
    result = nfc_scard_status(card, returned_name, &name_length, &card_state,
                          &protocol, atr, &atr_length);
    if ((uint32_t)result != (uint32_t)SCARD_S_SUCCESS) {
        status = pcsc_failure("SCardStatus", result, error, error_cap);
        goto disconnect;
    }
    if (atr_length > sizeof(atr)) {
        nfc_error(error, error_cap, "PC/SC returned an invalid ATR length");
        goto disconnect;
    }
    if (pcsc->verbose)
        log_atr(atr, atr_length);
    if (!nfcidm_pcsc_atr_is_felica(atr, atr_length)) {
        nfc_error(error, error_cap, "The card is not a recognized FeliCa card; refusing to output an NFC UID as IDm");
        status = NFC_BAD_CARD;
        goto disconnect;
    }
    if (protocol == SCARD_PROTOCOL_T1)
        pci = SCARD_PCI_T1;
    else if (protocol == SCARD_PROTOCOL_T0)
        pci = SCARD_PCI_T0;
    else {
        nfc_error(error, error_cap, "PC/SC negotiated an unsupported protocol");
        goto disconnect;
    }
    if (nfc_stopped) {
        status = NFC_CANCELLED;
        goto disconnect;
    }

    /* SCardTransmit itself has no per-call timeout. The CLI's watchdog bounds
     * a stalled system driver; SCardCancel cannot cancel this operation.
     */
    result = SCardTransmit(card, pci, get_idm, (DWORD)sizeof(get_idm), NULL,
                            response, &response_length);
    if ((uint32_t)result != (uint32_t)SCARD_S_SUCCESS) {
        status = pcsc_failure("SCardTransmit(GET DATA)", result, error, error_cap);
        goto disconnect;
    }
    if (response_length > sizeof(response)) {
        nfc_error(error, error_cap, "PC/SC returned an invalid GET DATA response length");
        goto disconnect;
    }
    if (nfcidm_pcsc_parse_idm(response, response_length, value) != 0) {
        if (response_length >= 2 &&
            (response[response_length - 2] != 0x90 ||
             response[response_length - 1] != 0x00))
            nfc_error(error, error_cap, "GET DATA failed (SW=%02X%02X)",
                       (unsigned)response[response_length - 2],
                       (unsigned)response[response_length - 1]);
        else
            nfc_error(error, error_cap, "GET DATA returned %u bytes; expected an 8-byte IDm and 9000", (unsigned)response_length);
        goto disconnect;
    }
    status = NFC_OK;

disconnect:
    result = SCardDisconnect(card, SCARD_LEAVE_CARD);
    if ((uint32_t)result != (uint32_t)SCARD_S_SUCCESS && pcsc->verbose)
        fprintf(stderr, "nfcidm: SCardDisconnect: %s (0x%08" PRIx32 ")\n",
                error_description((uint32_t)result), (uint32_t)result);
    if (status == NFC_AGAIN)
        pcsc->state.dwCurrentState = SCARD_STATE_UNAWARE;
    if (nfc_stopped)
        return NFC_CANCELLED;
    if (status == NFC_OK)
        memcpy(idm, value, sizeof(value));
    return status;
}

void nfc_pcsc_close(nfc_pcsc *pcsc)
{
    if (pcsc != NULL) {
        (void)SCardReleaseContext(pcsc->context);
        free(pcsc);
    }
}
