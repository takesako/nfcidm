/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include "nfcidm.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void stop_handler(int sig)
{
    int saved = errno;
    if (nfc_stopped) {
        if (nfc_stopped == NFCIDM_STOP_TIMEOUT) {
            static const char message[] = "nfcidm: timed out waiting for a card or reader\n";
            (void)write(STDERR_FILENO, message, sizeof(message) - 1);
            _exit(2);
        }
        _exit(128 + nfc_stopped);
    }
    nfc_stopped = sig == SIGALRM ? NFCIDM_STOP_TIMEOUT : sig;
    /* Permit cleanup of RF state, but bound a stalled OS-driver call. */
    alarm(2);
    errno = saved;
}

int nfc_platform_init(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = stop_handler;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGINT);
    sigaddset(&action.sa_mask, SIGTERM);
    sigaddset(&action.sa_mask, SIGALRM);
    action.sa_flags = 0;
    return sigaction(SIGINT, &action, NULL) ||
           sigaction(SIGTERM, &action, NULL) ||
           sigaction(SIGALRM, &action, NULL);
}

int nfc_platform_set_timeout(unsigned seconds)
{
    if (seconds)
        alarm(seconds);
    return 0;
}

void nfc_platform_clear_timeout(void)
{
    alarm(0);
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
#if defined(__APPLE__)
    return "IOKit";
#else
    return "USB";
#endif
}

uint64_t nfc_now_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0)
        return 0;
    return (uint64_t)t.tv_sec * 1000U + (uint64_t)t.tv_nsec / 1000000U;
}

void nfc_sleep_ms(unsigned ms)
{
    struct timespec t = {(time_t)(ms / 1000U), (long)(ms % 1000U) * 1000000L};
    while (nanosleep(&t, &t) != 0 && errno == EINTR) {
        if (nfc_stopped)
            break;
    }
}
