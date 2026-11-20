/* SPDX-License-Identifier: MIT */
#ifndef NFCIDM_H
#define NFCIDM_H
#include <stdbool.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>

#define NFCIDM_VERSION "0.5.4"
#define NFCIDM_MAX_READERS 64
#define NFCIDM_ERROR_SIZE 256
#define NFCIDM_NAME_SIZE 256
#define NFCIDM_USB_PATH_SIZE 1024

enum { NFC_OK = 0, NFC_AGAIN = 1, NFC_ERR = -1, NFC_TIMEOUT = -2,
       NFC_GONE = -3, NFC_CANCELLED = -4, NFC_BAD_CARD = -5 };
typedef enum { NFC_S320, NFC_S330, NFC_S380, NFC_PCSC } nfc_kind;
typedef struct {
    nfc_kind kind;
    uint16_t vid, pid;
    uint32_t location;
    char name[NFCIDM_NAME_SIZE];
    char pcsc_name[NFCIDM_NAME_SIZE];
    /* Native device-interface path; used by Windows WinUSB only. */
    char usb_path[NFCIDM_USB_PATH_SIZE];
} nfc_reader;

extern volatile sig_atomic_t nfc_stopped;
uint64_t nfc_now_ms(void);
void nfc_sleep_ms(unsigned ms);
void nfc_error(char *out, size_t cap, const char *fmt, ...);
const char *nfc_kind_name(nfc_kind kind);

/* Enumeration appends to readers[*count]; capacity is never exceeded. */
int nfc_usb_enumerate(nfc_reader *readers, size_t capacity, size_t *count,
                      char *error, size_t error_cap);
int nfc_pcsc_enumerate(nfc_reader *readers, size_t capacity, size_t *count,
                       char *error, size_t error_cap);

typedef struct nfc_pcsc nfc_pcsc;
int nfc_pcsc_open(const nfc_reader *reader, bool verbose, nfc_pcsc **out,
                  char *error, size_t error_cap);
/* A single bounded card-state check. NFC_OK means 8-byte IDm obtained.
 * SCardTransmit uses the OS driver's timeout; no timeout is promised for
 * that synchronous API. The CLI provides a process-wide deadline. */
int nfc_pcsc_poll(nfc_pcsc *pcsc, uint8_t idm[8], unsigned timeout_ms,
                  char *error, size_t error_cap);
void nfc_pcsc_close(nfc_pcsc *pcsc);

#endif
