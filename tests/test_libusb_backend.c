/* SPDX-License-Identifier: MIT */
/* Linux libusb transport mocks. No root privileges or physical reader needed. */
#include "nfc_usb.h"
#include "libusb-1.0/libusb.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct libusb_context { int marker; };
struct libusb_device {
    uint16_t vid, pid;
    uint8_t bus, addr;
};
struct libusb_device_handle { libusb_device *device; };

static struct libusb_context context;
static struct libusb_device_handle handle;
static struct libusb_device devices[] = {
    {0x054c, 0x02e1, 1, 5},   /* RC-S330 in the Alpine/QEMU report */
    {0x054c, 0x02e1, 1, 6},   /* second identical reader */
    {0x054c, 0x01bb, 2, 4},   /* interrupt-only RC-S320 */
    {0x054c, 0x06c1, 1, 7},   /* RC-S380/S */
    {0x054c, 0x06c3, 1, 8},   /* RC-S380/P */
    {0x072f, 0x2200, 1, 9},   /* ACR122U: PC/SC, not libusb */
    {0x054c, 0x02e2, 1, 10}  /* unsupported Sony device */
};

static const struct libusb_endpoint_descriptor bulk_eps[] = {
    {.bEndpointAddress = 0x81, .bmAttributes = LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK, .wMaxPacketSize = 64},
    {.bEndpointAddress = 0x02, .bmAttributes = LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK, .wMaxPacketSize = 64}
};
static const struct libusb_endpoint_descriptor interrupt_ep[] = {
    {.bEndpointAddress = 0x83, .bmAttributes = LIBUSB_ENDPOINT_TRANSFER_TYPE_INTERRUPT, .wMaxPacketSize = 8}
};
static const struct libusb_interface_descriptor bulk_alt[] = {
    {.bInterfaceNumber = 0, .bAlternateSetting = 0, .bNumEndpoints = 2, .endpoint = bulk_eps}
};
static const struct libusb_interface_descriptor int_alt[] = {
    {.bInterfaceNumber = 0, .bAlternateSetting = 0, .bNumEndpoints = 1, .endpoint = interrupt_ep}
};
static const struct libusb_interface_descriptor bulk_alt1[] = {
    {.bInterfaceNumber = 1, .bAlternateSetting = 1, .bNumEndpoints = 2, .endpoint = bulk_eps}
};
static const struct libusb_interface bulk_ifaces[] = { {bulk_alt, 1} };
static const struct libusb_interface int_ifaces[] = { {int_alt, 1} };
static const struct libusb_interface alt_ifaces[] = { {bulk_alt1, 1} };
static struct libusb_config_descriptor configuration = {
    .bNumInterfaces = 1, .bConfigurationValue = 1, .interface = bulk_ifaces
};

static int open_error = LIBUSB_SUCCESS;
static int claim_error = LIBUSB_SUCCESS;
static int io_error = LIBUSB_SUCCESS;
static bool unconfigured = false;
static int inits, exits, closes, releases, detaches, configures, altsets;
static int claims, bulk_reads, bulk_writes, interrupts, controls;
static int claimed_interface;

int libusb_init(libusb_context **ctx) { ++inits; *ctx = &context; return LIBUSB_SUCCESS; }
void libusb_exit(libusb_context *ctx) { assert(ctx == &context); ++exits; }
ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***out)
{
    assert(ctx == &context);
    size_t count = sizeof(devices) / sizeof(devices[0]);
    *out = calloc(count + 1, sizeof **out);
    assert(*out);
    for (size_t i = 0; i < count; i++)
        (*out)[i] = &devices[i];
    return (ssize_t)count;
}
void libusb_free_device_list(libusb_device **list, int unref)
{ assert(unref == 1); free(list); }
int libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc)
{ memset(desc, 0, sizeof *desc); desc->idVendor = dev->vid; desc->idProduct = dev->pid; return LIBUSB_SUCCESS; }
uint8_t libusb_get_bus_number(libusb_device *dev) { return dev->bus; }
uint8_t libusb_get_device_address(libusb_device *dev) { return dev->addr; }
int libusb_open(libusb_device *dev, libusb_device_handle **out)
{ if (open_error) return open_error; handle.device = dev; *out = &handle; return LIBUSB_SUCCESS; }
void libusb_close(libusb_device_handle *h) { assert(h == &handle); ++closes; }
libusb_device *libusb_get_device(libusb_device_handle *h)
{ assert(h == &handle); return h->device; }
int libusb_set_auto_detach_kernel_driver(libusb_device_handle *h, int enable)
{ assert(h == &handle && enable == 1); ++detaches; return LIBUSB_SUCCESS; }
int libusb_get_active_config_descriptor(libusb_device *dev, struct libusb_config_descriptor **out)
{
    if (unconfigured) return LIBUSB_ERROR_NOT_FOUND;
    configuration.interface = dev->pid == 0x01bb ? int_ifaces :
                              dev->pid == 0x06c3 ? alt_ifaces : bulk_ifaces;
    *out = &configuration;
    return LIBUSB_SUCCESS;
}
int libusb_get_config_descriptor(libusb_device *dev, uint8_t index, struct libusb_config_descriptor **out)
{ (void)dev; assert(index == 0); *out = &configuration; return LIBUSB_SUCCESS; }
int libusb_set_configuration(libusb_device_handle *h, int value)
{ assert(h == &handle && value == 1); ++configures; unconfigured = false; return LIBUSB_SUCCESS; }
void libusb_free_config_descriptor(struct libusb_config_descriptor *config)
{ assert(config == &configuration); }
int libusb_claim_interface(libusb_device_handle *h, int number)
{ assert(h == &handle); ++claims; claimed_interface = number; return claim_error; }
int libusb_set_interface_alt_setting(libusb_device_handle *h, int number, int setting)
{ assert(h == &handle && number == 1 && setting == 1); ++altsets; return LIBUSB_SUCCESS; }
int libusb_release_interface(libusb_device_handle *h, int number)
{ assert(h == &handle && number == claimed_interface); ++releases; return LIBUSB_SUCCESS; }
int libusb_bulk_transfer(libusb_device_handle *h, unsigned char ep, unsigned char *buffer,
                         int len, int *transferred, unsigned timeout)
{
    assert(h == &handle && timeout);
    if (ep & 0x80) {
        ++bulk_reads;
        assert(ep == 0x81 && len >= 4);
        memcpy(buffer, "\x01\x02\x03\x04", 4);
        *transferred = 4;
    } else {
        ++bulk_writes;
        assert(ep == 0x02 && len == 3 && buffer[0] == 0xa0);
        *transferred = len;
    }
    return io_error;
}
int libusb_interrupt_transfer(libusb_device_handle *h, unsigned char ep, unsigned char *buffer,
                              int len, int *transferred, unsigned timeout)
{
    assert(h == &handle && ep == 0x83 && len == 8 && timeout);
    ++interrupts;
    memcpy(buffer, "\x01\x02\x03\x04", 4);
    *transferred = 4;
    return io_error;
}
int libusb_control_transfer(libusb_device_handle *h, uint8_t type, uint8_t request,
                            uint16_t value, uint16_t index, unsigned char *buffer,
                            uint16_t len, unsigned timeout)
{
    assert(h == &handle && type == 0x40 && request == 0x23);
    assert(value == 0 && index == 0 && len == 3 && buffer[0] == 0xa0 && timeout);
    ++controls;
    return io_error ? io_error : (int)len;
}
const char *libusb_error_name(int code)
{
    switch (code) {
    case LIBUSB_ERROR_ACCESS: return "LIBUSB_ERROR_ACCESS";
    case LIBUSB_ERROR_BUSY: return "LIBUSB_ERROR_BUSY";
    case LIBUSB_ERROR_TIMEOUT: return "LIBUSB_ERROR_TIMEOUT";
    case LIBUSB_ERROR_NO_DEVICE: return "LIBUSB_ERROR_NO_DEVICE";
    default: return "LIBUSB_ERROR_OTHER";
    }
}

int main(void)
{
    nfc_reader readers[8] = {{0}};
    char error[NFCIDM_ERROR_SIZE] = "";
    size_t count = 0;
    assert(nfc_usb_enumerate(readers, 8, &count, error, sizeof error) == NFC_OK);
    assert(count == 5 && !error[0]);
    assert(readers[0].kind == NFC_S330 && readers[0].pid == 0x02e1);
    assert(readers[0].location != readers[1].location);
    assert(readers[2].kind == NFC_S320 && readers[2].location == (2u << 8 | 4u));
    assert(readers[3].kind == NFC_S380 && readers[4].kind == NFC_S380);
    assert(strcmp(readers[4].name, "SONY RC-S380/P") == 0);
    count = 0;
    assert(nfc_usb_enumerate(readers, 4, &count, error, sizeof error) == NFC_ERR);
    assert(count == 4 && strstr(error, "too many readers"));

    nfc_usb usb;
    uint8_t data[8] = {0};
    uint8_t cmd[] = {0xa0, 0xa1, 0xa2};
    assert(nfc_usb_open(&readers[1], false, &usb) == NFC_OK);
    assert(handle.device == &devices[1] && claimed_interface == 0);
    assert(nfc_usb_write(&usb, cmd, sizeof cmd, 200) == NFC_OK);
    assert(nfc_usb_read(&usb, data, sizeof data, 200) == 4);
    assert(data[0] == 1 && data[3] == 4);
    assert(nfc_usb_control(&usb, 0x23, 0, 0, cmd, sizeof cmd, 200) == NFC_OK);
    io_error = LIBUSB_ERROR_TIMEOUT;
    assert(nfc_usb_read(&usb, data, sizeof data, 1) == NFC_TIMEOUT);
    assert(strstr(usb.error, "TIMEOUT"));
    io_error = LIBUSB_ERROR_NO_DEVICE;
    assert(nfc_usb_read(&usb, data, sizeof data, 1) == NFC_GONE);
    io_error = LIBUSB_SUCCESS;
    nfc_usb_close(&usb);
    assert(closes == 1 && releases == 1 && detaches == 1);

    assert(nfc_usb_open(&readers[2], false, &usb) == NFC_OK);
    assert(nfc_usb_read(&usb, data, 4096, 200) == 4);
    assert(interrupts == 1);
    assert(nfc_usb_write(&usb, cmd, sizeof cmd, 200) == NFC_ERR);
    assert(strstr(usb.error, "no bulk OUT"));
    assert(nfc_usb_control(&usb, 0x23, 0, 0, cmd, sizeof cmd, 200) == NFC_OK);
    nfc_usb_close(&usb);

    assert(nfc_usb_open(&readers[4], false, &usb) == NFC_OK);
    assert(altsets == 1 && claimed_interface == 1);
    nfc_usb_close(&usb);

    unconfigured = true;
    assert(nfc_usb_open(&readers[0], false, &usb) == NFC_OK);
    assert(configures == 1);
    nfc_usb_close(&usb);

    open_error = LIBUSB_ERROR_ACCESS;
    assert(nfc_usb_open(&readers[0], false, &usb) == NFC_ERR);
    assert(strstr(usb.error, "permissions"));
    assert(strstr(usb.error, "ACCESS"));
    open_error = LIBUSB_SUCCESS;

    claim_error = LIBUSB_ERROR_BUSY;
    assert(nfc_usb_open(&readers[0], false, &usb) == NFC_ERR);
    assert(strstr(usb.error, "BUSY"));
    claim_error = LIBUSB_SUCCESS;

    assert(inits == exits && closes == releases + 1);
    assert(claims >= 4 && bulk_reads && bulk_writes && controls);
    puts("Linux libusb transport mock tests passed (no hardware)");
    return 0;
}
