/* SPDX-License-Identifier: MIT */
/* Native Linux transport for Sony PaSoRi readers using libusb-1.0.
 * Framing, initialization and FeliCa polling remain in the shared code.
 * ACR122U and RC-S300 remain on the separate PC/SC transport.
 */
#include "nfc_usb.h"
#include "sony_usb_ids.h"

#include <libusb-1.0/libusb.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    libusb_context *context;
    libusb_device_handle *handle;
    int interface_number;
    bool claimed;
    unsigned char endpoint_in, endpoint_out;
    bool interrupt_in;
    unsigned short in_packet_size;
} linux_usb;

/* USB bus/address are unique within the current enumeration. A reconnect
 * may change the address; open then correctly reports a disconnected reader.
 * Keep it in nfc_reader.location so that duplicate PIDs remain distinct. */
static uint32_t device_location(libusb_device *dev)
{
    return (uint32_t)((unsigned)libusb_get_bus_number(dev) << 8) |
           (uint32_t)libusb_get_device_address(dev);
}

static int usb_failure(nfc_usb *usb, const char *action, int result)
{
    nfc_error(usb->error, sizeof usb->error, "%s: %s (%d)",
              action, libusb_error_name(result), result);
    if (result == LIBUSB_ERROR_TIMEOUT)
        return NFC_TIMEOUT;
    if (result == LIBUSB_ERROR_NO_DEVICE)
        return NFC_GONE;
    if (result == LIBUSB_ERROR_INTERRUPTED && nfc_stopped)
        return NFC_CANCELLED;
    return NFC_ERR;
}

int nfc_usb_enumerate(nfc_reader *readers, size_t capacity, size_t *count,
                      char *error, size_t error_cap)
{
    libusb_context *context = NULL;
    libusb_device **list = NULL;
    int result = libusb_init(&context);
    if (result < 0) {
        nfc_error(error, error_cap, "libusb initialization failed: %s",
                  libusb_error_name(result));
        return NFC_ERR;
    }
    ssize_t n = libusb_get_device_list(context, &list);
    if (n < 0) {
        nfc_error(error, error_cap, "libusb enumeration failed: %s",
                  libusb_error_name((int)n));
        libusb_exit(context);
        return NFC_ERR;
    }
    int status = NFC_OK;
    for (ssize_t i = 0; i < n; i++) {
        struct libusb_device_descriptor descriptor;
        nfc_kind kind;
        if (libusb_get_device_descriptor(list[i], &descriptor) != LIBUSB_SUCCESS ||
            !nfc_sony_usb_kind(descriptor.idVendor, descriptor.idProduct, &kind))
            continue;
        if (*count >= capacity) {
            nfc_error(error, error_cap, "too many readers (limit %zu)", capacity);
            status = NFC_ERR;
            break;
        }
        nfc_reader *r = &readers[(*count)++];
        memset(r, 0, sizeof *r);
        r->kind = kind;
        r->vid = descriptor.idVendor;
        r->pid = descriptor.idProduct;
        r->location = device_location(list[i]);
        nfc_error(r->name, sizeof r->name, "%s", nfc_sony_usb_model(r->pid));
    }
    libusb_free_device_list(list, 1);
    libusb_exit(context);
    return status;
}

static void linux_close(nfc_usb *usb)
{
    linux_usb *d = usb->opaque;
    if (!d)
        return;
    if (d->handle) {
        if (d->claimed)
            (void)libusb_release_interface(d->handle, d->interface_number);
        libusb_close(d->handle);
    }
    if (d->context)
        libusb_exit(d->context);
    free(d);
}

static int linux_read(nfc_usb *usb, uint8_t *buffer, size_t size, unsigned ms)
{
    linux_usb *d = usb->opaque;
    if (!buffer || !size || size > INT_MAX) {
        nfc_error(usb->error, sizeof usb->error, "invalid USB read buffer size");
        return NFC_ERR;
    }
    if (d->interrupt_in && size > d->in_packet_size)
        size = d->in_packet_size;
    int transferred = 0;
    int rc;
    if (d->interrupt_in)
        rc = libusb_interrupt_transfer(d->handle, d->endpoint_in,
                                       buffer, (int)size, &transferred,
                                       ms ? ms : 1);
    else
        rc = libusb_bulk_transfer(d->handle, d->endpoint_in,
                                 buffer, (int)size, &transferred, ms ? ms : 1);
    return rc == LIBUSB_SUCCESS ? transferred :
           usb_failure(usb, d->interrupt_in ? "libusb interrupt read" :
                       "libusb bulk read", rc);
}

static int linux_write(nfc_usb *usb, const uint8_t *buffer, size_t size, unsigned ms)
{
    linux_usb *d = usb->opaque;
    if (!d->endpoint_out || !buffer || size > INT_MAX) {
        nfc_error(usb->error, sizeof usb->error, "reader has no bulk OUT endpoint");
        return NFC_ERR;
    }
    int transferred = 0;
    int rc = libusb_bulk_transfer(d->handle, d->endpoint_out,
                                   (unsigned char *)buffer, (int)size,
                                   &transferred, ms ? ms : 1);
    if (rc != LIBUSB_SUCCESS)
        return usb_failure(usb, "libusb bulk write", rc);
    if ((size_t)transferred != size) {
        nfc_error(usb->error, sizeof usb->error, "libusb bulk write: short transfer");
        return NFC_ERR;
    }
    return NFC_OK;
}

static int linux_control(nfc_usb *usb, uint8_t request, uint16_t value,
                         uint16_t index, const uint8_t *buffer, size_t size,
                         unsigned ms)
{
    linux_usb *d = usb->opaque;
    if (size > UINT16_MAX || (size && !buffer)) {
        nfc_error(usb->error, sizeof usb->error, "invalid USB control transfer size");
        return NFC_ERR;
    }
    /* Same vendor/device/OUT request type as IOKit DeviceRequestTO and WinUSB. */
    int result = libusb_control_transfer(d->handle,
                        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR |
                            LIBUSB_RECIPIENT_DEVICE,
                        request, value, index, (unsigned char *)buffer,
                        (uint16_t)size, ms ? ms : 1);
    if (result < 0)
        return usb_failure(usb, "libusb vendor control write", result);
    if ((size_t)result != size) {
        nfc_error(usb->error, sizeof usb->error, "libusb vendor control write: short transfer");
        return NFC_ERR;
    }
    return NFC_OK;
}

/* Select by endpoint descriptors rather than fixed interface/endpoint numbers.
 * RC-S320 reads interrupt packets and writes vendor control requests;
 * RC-S330/360/370 and RC-S380 use bulk IN and bulk OUT.
 */
static int linux_claim_interface(nfc_usb *usb, nfc_kind kind,
                                  const struct libusb_config_descriptor *config)
{
    linux_usb *d = usb->opaque;
    int last_rc = LIBUSB_SUCCESS;
    for (uint8_t i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface *interface = &config->interface[i];
        for (int alt = 0; alt < interface->num_altsetting; alt++) {
            const struct libusb_interface_descriptor *settings =
                &interface->altsetting[alt];
            unsigned char in = 0, out = 0;
            unsigned short packet_size = 0;
            for (uint8_t e = 0; e < settings->bNumEndpoints; e++) {
                const struct libusb_endpoint_descriptor *ep = &settings->endpoint[e];
                unsigned char transfer_type = ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
                if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN &&
                    transfer_type == (kind == NFC_S320 ?
                                     LIBUSB_ENDPOINT_TRANSFER_TYPE_INTERRUPT : LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK) &&
                    (ep->wMaxPacketSize & 0x07ff)) {
                    in = ep->bEndpointAddress;
                    packet_size = ep->wMaxPacketSize & 0x07ff;
                } else if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT &&
                           transfer_type == LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK) {
                    out = ep->bEndpointAddress;
                }
            }
            if (!in || (kind != NFC_S320 && !out))
                continue;
            int rc = libusb_claim_interface(d->handle, settings->bInterfaceNumber);
            if (rc != LIBUSB_SUCCESS) {
                last_rc = rc;
                continue;
            }
            if (settings->bAlternateSetting) {
                rc = libusb_set_interface_alt_setting(d->handle,
                        settings->bInterfaceNumber, settings->bAlternateSetting);
                if (rc != LIBUSB_SUCCESS) {
                    last_rc = rc;
                    (void)libusb_release_interface(d->handle, settings->bInterfaceNumber);
                    continue;
                }
            }
            d->interface_number = settings->bInterfaceNumber;
            d->claimed = true;
            d->endpoint_in = in;
            d->endpoint_out = out;
            d->interrupt_in = kind == NFC_S320;
            d->in_packet_size = packet_size;
            return NFC_OK;
        }
    }
    if (last_rc != LIBUSB_SUCCESS)
        (void)usb_failure(usb, "claim USB interface (another app may be using it)", last_rc);
    else
        nfc_error(usb->error, sizeof usb->error,
                  "USB interface lacks required %s endpoints",
                  kind == NFC_S320 ? "interrupt IN" : "bulk IN/OUT");
    return NFC_ERR;
}

int nfc_usb_open(const nfc_reader *reader, bool verbose, nfc_usb *usb)
{
    memset(usb, 0, sizeof *usb);
    usb->vid = reader->vid;
    usb->pid = reader->pid;
    usb->verbose = verbose;
    nfc_kind kind;
    if (!nfc_sony_usb_kind(reader->vid, reader->pid, &kind) ||
        kind != reader->kind) {
        nfc_error(usb->error, sizeof usb->error,
                  "libusb transport does not support this reader VID:PID");
        return NFC_ERR;
    }
    linux_usb *d = calloc(1, sizeof *d);
    if (!d) {
        nfc_error(usb->error, sizeof usb->error, "out of memory");
        return NFC_ERR;
    }
    usb->opaque = d;
    usb->read_fn = linux_read;
    usb->write_fn = linux_write;
    usb->control_fn = linux_control;
    usb->close_fn = linux_close;
    int rc = libusb_init(&d->context);
    if (rc != LIBUSB_SUCCESS) {
        (void)usb_failure(usb, "initialize libusb", rc);
        goto fail;
    }
    libusb_device **list = NULL;
    ssize_t n = libusb_get_device_list(d->context, &list);
    if (n < 0) {
        (void)usb_failure(usb, "find USB reader", (int)n);
        goto fail;
    }
    libusb_device *device = NULL;
    for (ssize_t i = 0; i < n; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) == LIBUSB_SUCCESS &&
            desc.idVendor == reader->vid && desc.idProduct == reader->pid &&
            device_location(list[i]) == reader->location) {
            device = list[i];
            break;
        }
    }
    if (!device) {
        nfc_error(usb->error, sizeof usb->error,
                  "USB reader disconnected (bus/address changed)");
        libusb_free_device_list(list, 1);
        goto fail;
    }
    rc = libusb_open(device, &d->handle);
    libusb_free_device_list(list, 1);
    if (rc != LIBUSB_SUCCESS) {
        (void)usb_failure(usb, "open USB reader (check /dev/bus/usb permissions)", rc);
        goto fail;
    }
    /* On Linux libusb will detach a kernel driver on claim and restore it
     * when releasing the interface. Ignore NOT_SUPPORTED on other builds. */
    rc = libusb_set_auto_detach_kernel_driver(d->handle, 1);
    if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_NOT_SUPPORTED) {
        (void)usb_failure(usb, "enable kernel-driver auto detach", rc);
        goto fail;
    }
    struct libusb_config_descriptor *config = NULL;
    rc = libusb_get_active_config_descriptor(libusb_get_device(d->handle), &config);
    if (rc == LIBUSB_ERROR_NOT_FOUND) {
        /* A newly passed-through USB device may still be unconfigured. */
        rc = libusb_get_config_descriptor(libusb_get_device(d->handle), 0, &config);
        if (rc == LIBUSB_SUCCESS) {
            int conf_rc = libusb_set_configuration(d->handle, config->bConfigurationValue);
            libusb_free_config_descriptor(config);
            config = NULL;
            if (conf_rc == LIBUSB_SUCCESS)
                rc = libusb_get_active_config_descriptor(libusb_get_device(d->handle), &config);
            else
                rc = conf_rc;
        }
    }
    if (rc != LIBUSB_SUCCESS) {
        (void)usb_failure(usb, "read USB configuration", rc);
        goto fail;
    }
    int status = linux_claim_interface(usb, kind, config);
    libusb_free_config_descriptor(config);
    if (status != NFC_OK)
        goto fail;
    return NFC_OK;
fail:
    linux_close(usb);
    usb->opaque = NULL;
    usb->close_fn = NULL;
    return NFC_ERR;
}
