/* SPDX-License-Identifier: MIT */
#include "nfc_usb.h"
#include "sony_usb_ids.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USB.h>
#include <limits.h>
#include <mach/mach_error.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool done;
    IOReturn result;
    size_t size;
    uint8_t data[4096];
} pending_read;

typedef struct {
    IOUSBDeviceInterface500 **device;
    IOUSBInterfaceInterface550 **interface;
    bool device_open, interface_open;
    UInt8 in_pipe, out_pipe, in_type;
    UInt16 in_packet_size;
    CFRunLoopSourceRef source;
    CFRunLoopRef runloop;
    pending_read *pending;
} mac_usb;

static int io_error(nfc_usb *usb, const char *action, IOReturn status)
{
    nfc_error(usb->error, sizeof usb->error, "%s: %s (0x%08x)", action,
              mach_error_string(status), (unsigned)status);
    if (status == kIOReturnTimeout)
        return NFC_TIMEOUT;
    if (status == kIOReturnNoDevice || status == kIOReturnNotResponding ||
        status == kIOReturnOffline)
        return NFC_GONE;
    return NFC_ERR;
}

static bool number_property(io_service_t service, CFStringRef key, uint32_t *out)
{
    CFTypeRef property = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    int32_t n = 0;
    bool ok = property && CFGetTypeID(property) == CFNumberGetTypeID() &&
              CFNumberGetValue((CFNumberRef)property, kCFNumberSInt32Type, &n);
    if (property)
        CFRelease(property);
    if (ok)
        *out = (uint32_t)n;
    return ok;
}

static bool identify(io_service_t service, nfc_reader *r)
{
    uint32_t vid, pid, location;
    nfc_kind kind;
    if (!number_property(service, CFSTR("idVendor"), &vid) ||
        !number_property(service, CFSTR("idProduct"), &pid) ||
        !number_property(service, CFSTR("locationID"), &location) ||
        vid > UINT16_MAX || pid > UINT16_MAX ||
        !nfc_sony_usb_kind((uint16_t)vid, (uint16_t)pid, &kind))
        return false;
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->vid = (uint16_t)vid;
    r->pid = (uint16_t)pid;
    r->location = location;
    nfc_error(r->name, sizeof r->name, "%s", nfc_sony_usb_model(r->pid));
    return true;
}

int nfc_usb_enumerate(nfc_reader *readers, size_t capacity, size_t *count,
                      char *error, size_t error_cap)
{
    io_iterator_t iterator = IO_OBJECT_NULL;
    /* MACH_PORT_NULL selects the default IOKit main port. */
    IOReturn rc = IOServiceGetMatchingServices(MACH_PORT_NULL,
                       IOServiceMatching(kIOUSBDeviceClassName), &iterator);
    if (rc != kIOReturnSuccess) {
        nfc_error(error, error_cap, "IOKit reader enumeration failed: 0x%08x", (unsigned)rc);
        return NFC_ERR;
    }
    io_service_t service;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        nfc_reader r;
        bool supported = identify(service, &r);
        IOObjectRelease(service);
        if (!supported)
            continue;
        if (*count == capacity) {
            IOObjectRelease(iterator);
            nfc_error(error, error_cap, "too many readers (limit %zu)", capacity);
            return NFC_ERR;
        }
        readers[(*count)++] = r;
    }
    IOObjectRelease(iterator);
    return NFC_OK;
}

static void read_complete(void *opaque, IOReturn result, void *arg0)
{
    pending_read *p = opaque;
    p->result = result;
    p->size = (size_t)(uintptr_t)arg0;
    p->done = true;
}

static void pump_until(pending_read *p, uint64_t deadline)
{
    while (!p->done) {
        uint64_t now = nfc_now_ms();
        if (now >= deadline)
            break;
        unsigned left = (unsigned)(deadline - now);
        (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode,
                                (double)(left < 20 ? left : 20) / 1000.0, true);
    }
}

static int read_interrupt(nfc_usb *usb, uint8_t *data, size_t size, unsigned ms)
{
    mac_usb *m = usb->opaque;
    if (m->pending) {
        nfc_error(usb->error, sizeof usb->error, "previous USB read did not finish cancellation");
        return NFC_ERR;
    }
    pending_read *p = calloc(1, sizeof *p);
    if (!p) {
        nfc_error(usb->error, sizeof usb->error, "out of memory");
        return NFC_ERR;
    }
    size_t request = size < m->in_packet_size ? size : m->in_packet_size;
    if (request > sizeof p->data)
        request = sizeof p->data;
    m->pending = p;
    IOReturn rc = (*m->interface)->ReadPipeAsync(m->interface, m->in_pipe,
                      p->data, (UInt32)request, read_complete, p);
    if (rc != kIOReturnSuccess) {
        free(p);
        m->pending = NULL;
        return io_error(usb, "submit interrupt read", rc);
    }
    pump_until(p, nfc_now_ms() + (ms ? ms : 1));
    bool timed_out = !p->done;
    if (timed_out) {
        (void)(*m->interface)->AbortPipe(m->interface, m->in_pipe);
        pump_until(p, nfc_now_ms() + 100);
    }
    if (!p->done) {
        /* Keep callback storage alive until close. Never submit another read. */
        nfc_error(usb->error, sizeof usb->error, "USB interrupt read could not be cancelled");
        return NFC_ERR;
    }
    rc = p->result;
    int result;
    if (rc == kIOReturnNoDevice || rc == kIOReturnNotResponding || rc == kIOReturnOffline) {
        result = io_error(usb, "interrupt read", rc);
    } else if (timed_out) {
        nfc_error(usb->error, sizeof usb->error, "USB interrupt read timed out");
        result = NFC_TIMEOUT;
    } else if (rc != kIOReturnSuccess) {
        result = io_error(usb, "interrupt read", rc);
    } else if (p->size > request) {
        nfc_error(usb->error, sizeof usb->error, "USB interrupt response exceeded buffer size");
        result = NFC_ERR;
    } else {
        memcpy(data, p->data, p->size);
        result = (int)p->size;
    }
    free(p);
    m->pending = NULL;
    return result;
}

static int mac_read(nfc_usb *usb, uint8_t *data, size_t size, unsigned ms)
{
    mac_usb *m = usb->opaque;
    if (!size || size > UINT32_MAX)
        return NFC_ERR;
    if (m->in_type == kUSBInterrupt)
        return read_interrupt(usb, data, size, ms);
    UInt32 length = (UInt32)size;
    UInt32 timeout = ms ? ms : 1;
    IOReturn rc = (*m->interface)->ReadPipeTO(m->interface, m->in_pipe,
                        data, &length, timeout, timeout);
    if (rc != kIOReturnSuccess)
        return io_error(usb, "bulk read", rc);
    return (int)length;
}

static int mac_write(nfc_usb *usb, const uint8_t *data, size_t size, unsigned ms)
{
    mac_usb *m = usb->opaque;
    if (!m->out_pipe || size > UINT32_MAX) {
        nfc_error(usb->error, sizeof usb->error, "reader has no bulk output pipe");
        return NFC_ERR;
    }
    UInt32 timeout = ms ? ms : 1;
    IOReturn rc = (*m->interface)->WritePipeTO(m->interface, m->out_pipe,
                        (void *)data, (UInt32)size, timeout, timeout);
    return rc == kIOReturnSuccess ? NFC_OK : io_error(usb, "bulk write", rc);
}

static int mac_control(nfc_usb *usb, uint8_t request, uint16_t value,
                        uint16_t index, const uint8_t *data, size_t size, unsigned ms)
{
    mac_usb *m = usb->opaque;
    if (size > UINT16_MAX) {
        nfc_error(usb->error, sizeof usb->error, "control request is too large");
        return NFC_ERR;
    }
    IOUSBDevRequestTO r;
    memset(&r, 0, sizeof r);
    r.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
    r.bRequest = request;
    r.wValue = value;
    r.wIndex = index;
    r.wLength = (UInt16)size;
    r.pData = (void *)data;
    r.noDataTimeout = r.completionTimeout = ms ? ms : 1;
    IOReturn rc = (*m->device)->DeviceRequestTO(m->device, &r);
    if (rc != kIOReturnSuccess)
        return io_error(usb, "vendor control write", rc);
    if (r.wLenDone != size) {
        nfc_error(usb->error, sizeof usb->error, "short vendor control write");
        return NFC_ERR;
    }
    return NFC_OK;
}

static void mac_close(nfc_usb *usb)
{
    mac_usb *m = usb->opaque;
    if (!m)
        return;
    if (m->interface && m->interface_open) {
        if (m->pending && !m->pending->done) {
            (void)(*m->interface)->AbortPipe(m->interface, m->in_pipe);
            pump_until(m->pending, nfc_now_ms() + 100);
        }
        (void)(*m->interface)->USBInterfaceClose(m->interface);
    }
    if (m->source) {
        CFRunLoopRemoveSource(m->runloop, m->source, kCFRunLoopDefaultMode);
        CFRelease(m->source);
    }
    if (m->runloop)
        CFRelease(m->runloop);
    if (m->interface)
        (*m->interface)->Release(m->interface);
    if (m->device) {
        if (m->device_open)
            (void)(*m->device)->USBDeviceClose(m->device);
        (*m->device)->Release(m->device);
    }
    /* A malfunctioning OS driver might still own callback storage. Leave
     * that one allocation alive rather than risk a late callback UAF.
     * The CLI exits after this fatal failure; normal closes free it. */
    if (m->pending && m->pending->done)
        free(m->pending);
    free(m);
}

static int open_interface(nfc_usb *usb, nfc_kind kind)
{
    mac_usb *m = usb->opaque;
    IOUSBFindInterfaceRequest request = { kIOUSBFindInterfaceDontCare,
        kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare };
    io_iterator_t iterator = IO_OBJECT_NULL;
    IOReturn rc = (*m->device)->CreateInterfaceIterator(m->device, &request, &iterator);
    if (rc != kIOReturnSuccess)
        return io_error(usb, "enumerate USB interfaces", rc);
    io_service_t service;
    IOReturn last = kIOReturnNotFound;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        IOCFPlugInInterface **plugin = NULL;
        IOUSBInterfaceInterface550 **interface = NULL;
        SInt32 score = 0;
        rc = IOCreatePlugInInterfaceForService(service, kIOUSBInterfaceUserClientTypeID,
                             kIOCFPlugInInterfaceID, &plugin, &score);
        IOObjectRelease(service);
        if (rc != kIOReturnSuccess || !plugin) {
            last = rc;
            continue;
        }
        HRESULT hr = (*plugin)->QueryInterface(plugin,
                    CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID550), (LPVOID *)&interface);
        (*plugin)->Release(plugin);
        if (hr || !interface) {
            last = kIOReturnUnsupported;
            continue;
        }
        rc = (*interface)->USBInterfaceOpen(interface);
        if (rc != kIOReturnSuccess) {
            last = rc;
            (*interface)->Release(interface);
            continue;
        }
        UInt8 endpoints = 0, in_pipe = 0, out_pipe = 0, in_type = 0;
        UInt16 in_packet = 0;
        rc = (*interface)->GetNumEndpoints(interface, &endpoints);
        if (rc != kIOReturnSuccess)
            last = rc;
        if (rc == kIOReturnSuccess) {
            for (unsigned p = 1; p <= endpoints; p++) {
                UInt8 direction, number, type, interval;
                UInt16 packet;
                rc = (*interface)->GetPipeProperties(interface, (UInt8)p, &direction,
                                                      &number, &type, &packet, &interval);
                if (rc != kIOReturnSuccess)
                    continue;
                if (direction == kUSBIn && packet &&
                    type == (kind == NFC_S320 ? kUSBInterrupt : kUSBBulk)) {
                    in_pipe = (UInt8)p; in_type = type; in_packet = packet;
                }
                if (direction == kUSBOut && type == kUSBBulk)
                    out_pipe = (UInt8)p;
            }
        }
        if (in_pipe && (kind == NFC_S320 || out_pipe)) {
            m->interface = interface;
            m->interface_open = true;
            m->in_pipe = in_pipe; m->out_pipe = out_pipe;
            m->in_type = in_type; m->in_packet_size = in_packet;
            IOObjectRelease(iterator);
            if (in_type == kUSBInterrupt) {
                rc = (*interface)->CreateInterfaceAsyncEventSource(interface, &m->source);
                if (rc != kIOReturnSuccess)
                    return io_error(usb, "create USB interrupt event source", rc);
                m->runloop = CFRunLoopGetCurrent();
                CFRetain(m->runloop);
                CFRunLoopAddSource(m->runloop, m->source, kCFRunLoopDefaultMode);
            }
            return NFC_OK;
        }
        (void)(*interface)->USBInterfaceClose(interface);
        (*interface)->Release(interface);
    }
    IOObjectRelease(iterator);
    return io_error(usb, "open matching USB interface (another app may be using it)", last);
}

int nfc_usb_open(const nfc_reader *reader, bool verbose, nfc_usb *usb)
{
    memset(usb, 0, sizeof *usb);
    usb->vid = reader->vid; usb->pid = reader->pid; usb->verbose = verbose;
    mac_usb *m = calloc(1, sizeof *m);
    if (!m) {
        nfc_error(usb->error, sizeof usb->error, "out of memory");
        return NFC_ERR;
    }
    usb->opaque = m;
    usb->write_fn = mac_write; usb->read_fn = mac_read;
    usb->control_fn = mac_control; usb->close_fn = mac_close;
    io_iterator_t iterator = IO_OBJECT_NULL;
    IOReturn rc = IOServiceGetMatchingServices(MACH_PORT_NULL,
                            IOServiceMatching(kIOUSBDeviceClassName), &iterator);
    if (rc != kIOReturnSuccess) {
        (void)io_error(usb, "find USB reader", rc);
        goto fail;
    }
    io_service_t service;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        nfc_reader found;
        if (identify(service, &found) && found.pid == reader->pid &&
            found.location == reader->location) {
            IOCFPlugInInterface **plugin = NULL;
            SInt32 score = 0;
            rc = IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID,
                          kIOCFPlugInInterfaceID, &plugin, &score);
            IOObjectRelease(service);
            if (rc == kIOReturnSuccess && plugin) {
                HRESULT hr = (*plugin)->QueryInterface(plugin,
                       CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID500), (LPVOID *)&m->device);
                (*plugin)->Release(plugin);
                if (hr)
                    m->device = NULL;
            }
            break;
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    if (!m->device) {
        nfc_error(usb->error, sizeof usb->error, "USB reader disconnected or could not be opened");
        goto fail;
    }
    rc = (*m->device)->USBDeviceOpen(m->device);
    if (rc != kIOReturnSuccess) {
        (void)io_error(usb, "open USB device (close other NFC applications)", rc);
        goto fail;
    }
    m->device_open = true;
    UInt8 configuration = 0;
    rc = (*m->device)->GetConfiguration(m->device, &configuration);
    if (rc != kIOReturnSuccess) {
        (void)io_error(usb, "read USB configuration", rc);
        goto fail;
    }
    if (configuration == 0) {
        IOUSBConfigurationDescriptorPtr descriptor = NULL;
        rc = (*m->device)->GetConfigurationDescriptorPtr(m->device, 0, &descriptor);
        if (rc == kIOReturnSuccess && descriptor)
            rc = (*m->device)->SetConfiguration(m->device, descriptor->bConfigurationValue);
        if (rc != kIOReturnSuccess || !descriptor) {
            (void)io_error(usb, "configure USB reader", rc);
            goto fail;
        }
    }
    if (open_interface(usb, reader->kind) != NFC_OK)
        goto fail;
    return NFC_OK;
fail:
    nfc_usb_close(usb);
    return NFC_ERR;
}
