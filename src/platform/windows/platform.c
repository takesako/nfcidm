/* SPDX-License-Identifier: MIT */
/* Windows Win32 portability layer (PC/SC backend only; no WinUSB yet).
 * Not validated on physical Windows hardware.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <signal.h>
#include <stdio.h>
#include "nfcidm.h"
#include "platform.h"

static HANDLE watchdog_event;
static HANDLE watchdog_thread;

static DWORD WINAPI forced_exit(void *unused)
{
    (void)unused;
    Sleep(2000);
    ExitProcess(nfc_stopped == NFCIDM_STOP_TIMEOUT ? 2U : (UINT)(128 + nfc_stopped));
    return 0;
}

static BOOL WINAPI console_handler(DWORD event)
{
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT &&
        event != CTRL_CLOSE_EVENT && event != CTRL_LOGOFF_EVENT &&
        event != CTRL_SHUTDOWN_EVENT)
        return FALSE;
    if (!nfc_stopped)
        nfc_stopped = event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ? SIGINT : SIGTERM;
    /* Follow the POSIX backend: allow two seconds for disconnect/RF-off. */
    HANDLE thread = CreateThread(NULL, 0, forced_exit, NULL, 0, NULL);
    if (thread)
        CloseHandle(thread);
    return TRUE;
}

static DWORD WINAPI timeout_thread(void *arg)
{
    unsigned seconds = (unsigned)(uintptr_t)arg;
    if (WaitForSingleObject(watchdog_event, seconds * 1000U) == WAIT_TIMEOUT) {
        if (!nfc_stopped)
            nfc_stopped = NFCIDM_STOP_TIMEOUT;
        if (WaitForSingleObject(watchdog_event, 2000) == WAIT_TIMEOUT)
            ExitProcess(2);
    }
    return 0;
}

int nfc_platform_init(void)
{
    return !SetConsoleCtrlHandler(console_handler, TRUE);
}

int nfc_platform_set_timeout(unsigned seconds)
{
    if (!seconds)
        return 0;
    watchdog_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!watchdog_event)
        return -1;
    watchdog_thread = CreateThread(NULL, 0, timeout_thread,
                                   (void *)(uintptr_t)seconds, 0, NULL);
    if (!watchdog_thread) {
        CloseHandle(watchdog_event);
        watchdog_event = NULL;
        return -1;
    }
    return 0;
}

void nfc_platform_clear_timeout(void)
{
    if (watchdog_event)
        SetEvent(watchdog_event);
    if (watchdog_thread) {
        WaitForSingleObject(watchdog_thread, INFINITE);
        CloseHandle(watchdog_thread);
        watchdog_thread = NULL;
    }
    if (watchdog_event) {
        CloseHandle(watchdog_event);
        watchdog_event = NULL;
    }
}

int nfc_platform_stopped_status(void)
{
    if (nfc_stopped == NFCIDM_STOP_TIMEOUT) {
        fputs("nfcidm: timed out waiting for a FeliCa card\n", stderr);
        return 2;
    }
    return 128 + (int)nfc_stopped;
}

const char *nfc_platform_usb_label(void)
{
    return "WinUSB";
}

uint64_t nfc_now_ms(void)
{
    return (uint64_t)GetTickCount64();
}

void nfc_sleep_ms(unsigned ms)
{
    Sleep(ms);
}
