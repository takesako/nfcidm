/* Windows SDK <usb.h> stand-in for the native Linux unit tests. */
#ifndef NFCIDM_MOCK_SYSTEM_USB_H
#define NFCIDM_MOCK_SYSTEM_USB_H
#include "windows.h"
typedef enum { UsbdPipeTypeControl = 0, UsbdPipeTypeIsochronous,
               UsbdPipeTypeBulk, UsbdPipeTypeInterrupt } USBD_PIPE_TYPE;
typedef struct { UCHAR bLength, bDescriptorType, bInterfaceNumber,
                 bAlternateSetting, bNumEndpoints, bInterfaceClass,
                 bInterfaceSubClass, bInterfaceProtocol, iInterface;
               } USB_INTERFACE_DESCRIPTOR;
#endif
