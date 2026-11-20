/* SPDX-License-Identifier: MIT */
#ifndef NFCIDM_PCSC_COMPAT_H
#define NFCIDM_PCSC_COMPAT_H

/* PC/SC uses int32_t/uint32_t in Apple's framework, Win32 LONG/DWORD on
 * Windows, and LONG/DWORD (potentially wider) in pcsc-lite on Linux.
 * Use the exact types expected by each platform's SCard API.
 */
#include <stdint.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winscard.h>
typedef SCARD_READERSTATEA nfc_pcsc_reader_state;
typedef LONG nfc_pcsc_result_t;
typedef DWORD nfc_pcsc_dword_t;
#define nfc_scard_list_readers SCardListReadersA
#define nfc_scard_status SCardStatusA
#define nfc_scard_get_status_change SCardGetStatusChangeA
#define nfc_scard_connect SCardConnectA
#else
#include <PCSC/winscard.h>
typedef SCARD_READERSTATE nfc_pcsc_reader_state;
#if defined(__APPLE__)
typedef int32_t nfc_pcsc_result_t;
typedef uint32_t nfc_pcsc_dword_t;
#else
typedef LONG nfc_pcsc_result_t;
typedef DWORD nfc_pcsc_dword_t;
#endif
#define nfc_scard_list_readers SCardListReaders
#define nfc_scard_status SCardStatus
#define nfc_scard_get_status_change SCardGetStatusChange
#define nfc_scard_connect SCardConnect
#endif

/* An ATR is at most 36 bytes in Windows PC/SC; the 64-byte buffer also
 * accommodates pcsc-lite and avoids non-portable MAX_ATR_SIZE macros. */
#define NFCIDM_PCSC_ATR_BUFFER_SIZE 64

#endif
