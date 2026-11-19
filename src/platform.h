/* SPDX-License-Identifier: MIT */
#ifndef NFCIDM_PLATFORM_H
#define NFCIDM_PLATFORM_H

/* The CLI uses this interface rather than POSIX alarm/getopt/unistd.
 * USB and PC/SC backends have independent, OS-specific implementations.
 */
enum { NFCIDM_STOP_TIMEOUT = 1000 };

int nfc_platform_init(void);
int nfc_platform_set_timeout(unsigned seconds);
void nfc_platform_clear_timeout(void);
/* Called only after nfc_stopped becomes nonzero; reports timeout on stderr. */
int nfc_platform_stopped_status(void);
const char *nfc_platform_usb_label(void);

#endif
