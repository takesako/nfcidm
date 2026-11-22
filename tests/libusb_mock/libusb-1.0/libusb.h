/* SPDX-License-Identifier: MIT */
/* Minimal libusb-1.0 declarations used solely by the offline transport test. */
#ifndef NFCIDM_TEST_LIBUSB_H
#define NFCIDM_TEST_LIBUSB_H
#include <stdint.h>
#include <sys/types.h>

typedef struct libusb_context libusb_context;
typedef struct libusb_device libusb_device;
typedef struct libusb_device_handle libusb_device_handle;

#define LIBUSB_SUCCESS              0
#define LIBUSB_ERROR_IO            -1
#define LIBUSB_ERROR_INVALID_PARAM -2
#define LIBUSB_ERROR_ACCESS        -3
#define LIBUSB_ERROR_NO_DEVICE     -4
#define LIBUSB_ERROR_NOT_FOUND     -5
#define LIBUSB_ERROR_BUSY          -6
#define LIBUSB_ERROR_TIMEOUT       -7
#define LIBUSB_ERROR_OVERFLOW      -8
#define LIBUSB_ERROR_PIPE          -9
#define LIBUSB_ERROR_INTERRUPTED  -10
#define LIBUSB_ERROR_NO_MEM       -11
#define LIBUSB_ERROR_NOT_SUPPORTED -12
#define LIBUSB_ERROR_OTHER        -99

#define LIBUSB_ENDPOINT_OUT          0x00
#define LIBUSB_ENDPOINT_IN           0x80
#define LIBUSB_ENDPOINT_DIR_MASK     0x80
#define LIBUSB_REQUEST_TYPE_VENDOR   0x40
#define LIBUSB_RECIPIENT_DEVICE      0x00
#define LIBUSB_TRANSFER_TYPE_MASK    0x03
#define LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK    0x02
#define LIBUSB_ENDPOINT_TRANSFER_TYPE_INTERRUPT 0x03

struct libusb_device_descriptor {
    uint8_t bLength, bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
    uint16_t idVendor, idProduct, bcdDevice;
    uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
};
struct libusb_endpoint_descriptor {
    uint8_t bLength, bDescriptorType, bEndpointAddress, bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval, bRefresh, bSynchAddress;
    const unsigned char *extra;
    int extra_length;
};
struct libusb_interface_descriptor {
    uint8_t bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting;
    uint8_t bNumEndpoints, bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol;
    uint8_t iInterface;
    const struct libusb_endpoint_descriptor *endpoint;
    const unsigned char *extra;
    int extra_length;
};
struct libusb_interface {
    const struct libusb_interface_descriptor *altsetting;
    int num_altsetting;
};
struct libusb_config_descriptor {
    uint8_t bLength, bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces, bConfigurationValue, iConfiguration, bmAttributes, MaxPower;
    const struct libusb_interface *interface;
    const unsigned char *extra;
    int extra_length;
};

int libusb_init(libusb_context **context);
void libusb_exit(libusb_context *context);
ssize_t libusb_get_device_list(libusb_context *context, libusb_device ***list);
void libusb_free_device_list(libusb_device **list, int unref_devices);
int libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc);
uint8_t libusb_get_bus_number(libusb_device *dev);
uint8_t libusb_get_device_address(libusb_device *dev);
int libusb_open(libusb_device *dev, libusb_device_handle **handle);
void libusb_close(libusb_device_handle *handle);
libusb_device *libusb_get_device(libusb_device_handle *handle);
int libusb_set_auto_detach_kernel_driver(libusb_device_handle *handle, int enable);
int libusb_get_active_config_descriptor(libusb_device *dev, struct libusb_config_descriptor **config);
int libusb_get_config_descriptor(libusb_device *dev, uint8_t index, struct libusb_config_descriptor **config);
int libusb_set_configuration(libusb_device_handle *handle, int config_value);
void libusb_free_config_descriptor(struct libusb_config_descriptor *config);
int libusb_claim_interface(libusb_device_handle *handle, int interface_number);
int libusb_set_interface_alt_setting(libusb_device_handle *handle, int interface_number, int alternate_setting);
int libusb_release_interface(libusb_device_handle *handle, int interface_number);
int libusb_bulk_transfer(libusb_device_handle *handle, unsigned char endpoint,
                         unsigned char *buffer, int length, int *transferred, unsigned int timeout);
int libusb_interrupt_transfer(libusb_device_handle *handle, unsigned char endpoint,
                              unsigned char *buffer, int length, int *transferred, unsigned int timeout);
int libusb_control_transfer(libusb_device_handle *handle, uint8_t request_type, uint8_t request,
                            uint16_t value, uint16_t index, unsigned char *data,
                            uint16_t length, unsigned int timeout);
const char *libusb_error_name(int error_code);

#endif
