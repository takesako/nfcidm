/* SPDX-License-Identifier: MIT */
/* Test double for the subset of Apple's PC/SC C API used by nfcidm.
 * This is not a replacement SDK and does not validate macOS runtime behavior.
 */
#ifndef NFCIDM_TEST_WINSCARD_H
#define NFCIDM_TEST_WINSCARD_H
#include <stdint.h>

typedef int32_t SCARDCONTEXT;
typedef int32_t SCARDHANDLE;
#define MAX_ATR_SIZE 33
typedef struct {
    const char *szReader;
    void *pvUserData;
    uint32_t dwCurrentState, dwEventState, cbAtr;
    unsigned char rgbAtr[MAX_ATR_SIZE];
} SCARD_READERSTATE;
typedef struct {
    uint32_t dwProtocol, cbPciLength;
} SCARD_IO_REQUEST;
typedef const SCARD_IO_REQUEST *LPCSCARD_IO_REQUEST;

#define SCARD_S_SUCCESS               UINT32_C(0x00000000)
#define SCARD_E_CANCELLED             UINT32_C(0x80100002)
#define SCARD_E_INSUFFICIENT_BUFFER   UINT32_C(0x80100008)
#define SCARD_E_UNKNOWN_READER        UINT32_C(0x80100009)
#define SCARD_E_TIMEOUT               UINT32_C(0x8010000a)
#define SCARD_E_SHARING_VIOLATION     UINT32_C(0x8010000b)
#define SCARD_E_NO_SMARTCARD          UINT32_C(0x8010000c)
#define SCARD_E_PROTO_MISMATCH        UINT32_C(0x8010000f)
#define SCARD_E_READER_UNAVAILABLE    UINT32_C(0x80100017)
#define SCARD_E_NO_SERVICE           UINT32_C(0x8010001d)
#define SCARD_E_SERVICE_STOPPED      UINT32_C(0x8010001e)
#define SCARD_E_NO_READERS_AVAILABLE UINT32_C(0x8010002e)
#define SCARD_W_UNRESPONSIVE_CARD    UINT32_C(0x80100066)
#define SCARD_W_UNPOWERED_CARD       UINT32_C(0x80100067)
#define SCARD_W_RESET_CARD           UINT32_C(0x80100068)
#define SCARD_W_REMOVED_CARD         UINT32_C(0x80100069)

#define SCARD_SCOPE_SYSTEM 2
#define SCARD_SHARE_SHARED 2
#define SCARD_PROTOCOL_T0 1
#define SCARD_PROTOCOL_T1 2
#define SCARD_LEAVE_CARD 0
#define SCARD_STATE_UNAWARE 0
#define SCARD_STATE_CHANGED 2
#define SCARD_STATE_UNKNOWN 4
#define SCARD_STATE_UNAVAILABLE 8
#define SCARD_STATE_EMPTY 16
#define SCARD_STATE_PRESENT 32
#define SCARD_STATE_MUTE 512

extern const SCARD_IO_REQUEST nfcidm_mock_pci_t0, nfcidm_mock_pci_t1;
#define SCARD_PCI_T0 (&nfcidm_mock_pci_t0)
#define SCARD_PCI_T1 (&nfcidm_mock_pci_t1)

int32_t SCardEstablishContext(uint32_t, const void *, const void *, SCARDCONTEXT *);
int32_t SCardReleaseContext(SCARDCONTEXT);
int32_t SCardListReaders(SCARDCONTEXT, const char *, char *, uint32_t *);
int32_t SCardGetStatusChange(SCARDCONTEXT, uint32_t, SCARD_READERSTATE *, uint32_t);
int32_t SCardConnect(SCARDCONTEXT, const char *, uint32_t, uint32_t,
                     SCARDHANDLE *, uint32_t *);
int32_t SCardStatus(SCARDHANDLE, char *, uint32_t *, uint32_t *, uint32_t *,
                    unsigned char *, uint32_t *);
int32_t SCardTransmit(SCARDHANDLE, LPCSCARD_IO_REQUEST, const unsigned char *,
                      uint32_t, SCARD_IO_REQUEST *, unsigned char *, uint32_t *);
int32_t SCardDisconnect(SCARDHANDLE, uint32_t);
#endif
