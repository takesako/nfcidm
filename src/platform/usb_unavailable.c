/* SPDX-License-Identifier: MIT */
/* Linux PC/SC-only target until a native Linux USB backend is added.
 * Returning an empty USB list is intentional: PC/SC readers still work.
 */
#include "nfc_usb.h"
#include <string.h>

int nfc_usb_enumerate(nfc_reader *readers, size_t cap, size_t *count,
                      char *error, size_t error_cap)
{
    (void)readers; (void)cap; (void)count; (void)error; (void)error_cap;
    return NFC_OK;
}

int nfc_usb_open(const nfc_reader *reader, bool verbose, nfc_usb *usb)
{
    (void)reader; (void)verbose;
    memset(usb, 0, sizeof *usb);
    nfc_error(usb->error, sizeof usb->error,
              "Native USB transport for old SONY readers is not implemented on this OS");
    return NFC_ERR;
}
