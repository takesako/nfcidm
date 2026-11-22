/* SPDX-License-Identifier: MIT */
#include "sony_usb_ids.h"

bool nfc_sony_usb_kind(uint16_t vid, uint16_t pid, nfc_kind *kind)
{
    if (vid != NFC_SONY_USB_VID || !kind)
        return false;
    switch (pid) {
    case NFC_SONY_USB_S320: *kind = NFC_S320; return true;
    case NFC_SONY_USB_S330: *kind = NFC_S330; return true;
    case NFC_SONY_USB_S380_S:
    case NFC_SONY_USB_S380_P: *kind = NFC_S380; return true;
    default: return false;
    }
}

const char *nfc_sony_usb_model(uint16_t pid)
{
    switch (pid) {
    case NFC_SONY_USB_S320: return "SONY RC-S320";
    case NFC_SONY_USB_S330: return "SONY RC-S330/360/370";
    case NFC_SONY_USB_S380_S: return "SONY RC-S380/S";
    case NFC_SONY_USB_S380_P: return "SONY RC-S380/P";
    default: return "SONY unknown";
    }
}
