/* SPDX-License-Identifier: MIT */
#include "nfcidm.h"
#include <stdarg.h>
#include <stdio.h>

volatile sig_atomic_t nfc_stopped = 0;

void nfc_error(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap;
    if (cap == 0)
        return;
    va_start(ap, fmt);
    (void)vsnprintf(out, cap, fmt, ap);
    va_end(ap);
}

const char *nfc_kind_name(nfc_kind kind)
{
    switch (kind) {
    case NFC_S320: return "RC-S320";
    case NFC_S330: return "RC-S330/360/370";
    case NFC_S380: return "RC-S380";
    case NFC_PCSC: return "PC/SC";
    }
    return "unknown";
}
