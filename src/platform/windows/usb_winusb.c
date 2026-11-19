/* SPDX-License-Identifier: MIT */
/* SONY RC-S320, RC-S330/360/370 and RC-S380/S/P via SetupAPI + WinUSB.
 * No libusb, libnfc or vendor DLL is needed at runtime. The device must
 * already be bound to the Windows-provided WinUSB (winusb.sys) driver and
 * have a registered DeviceInterfaceGUID(s), e.g. from a suitable driver INF.
 *
 * Sony RC-S320, RC-S330 and RC-S380 have been tested on Windows.
 * Do not change ACR122U's PC/SC
 * driver; the separate PC/SC backend handles that device unchanged.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
/* WinUSB uses USBD_PIPE_TYPE and USBD_ISO_PACKET_DESCRIPTOR from USB SDK.
 * Explicitly load the system header before winusb.h. Our internal header
 * is nfc_usb.h (not usb.h) to avoid -Isrc shadowing the MinGW SDK. */
#include <usb.h>
#include <winusb.h>
#include <objbase.h>

#include "nfc_usb.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define SONY_VID 0x054c
#define SONY_PID_S320 0x01bb
#define SONY_PID_S330 0x02e1
#define SONY_PID_S380S 0x06c1
#define SONY_PID_S380P 0x06c3
#define MAX_DEVICE_ID 1024
#define MAX_GUIDS 16

typedef struct {
    HANDLE file;
    WINUSB_INTERFACE_HANDLE handle;
    UCHAR pipe_in;
    UCHAR pipe_out;
    bool interrupt_in;
    USHORT in_packet_size;
} windows_usb;

static int win_error(nfc_usb *usb, const char *action, DWORD code)
{
    nfc_error(usb->error, sizeof usb->error, "%s: Windows error %lu (0x%08lx)",
              action, (unsigned long)code, (unsigned long)code);
    if (code == ERROR_SEM_TIMEOUT || code == WAIT_TIMEOUT)
        return NFC_TIMEOUT;
    if (code == ERROR_OPERATION_ABORTED && nfc_stopped)
        return NFC_CANCELLED;
    if (code == ERROR_DEVICE_NOT_CONNECTED || code == ERROR_NO_SUCH_DEVICE ||
        code == ERROR_INVALID_HANDLE)
        return NFC_GONE;
    return NFC_ERR;
}

/* The VID/PID maps to a protocol family; 330/360/370 share a USB ID.
 * Check delimiters to avoid accepting lookalike PIDs. */
static bool identify_sony(const char *id, nfc_kind *kind, uint16_t *pid)
{
    static const char prefix[] = "USB\\VID_054C&PID_";
    if (_strnicmp(id, prefix, sizeof prefix - 1) != 0)
        return false;
    const char *text = id + sizeof prefix - 1;
    if (strlen(text) < 5 || (text[4] != '\\' && text[4] != '&'))
        return false;
    static const struct { const char *id; uint16_t pid; nfc_kind kind; } models[] = {
        {"01BB", SONY_PID_S320, NFC_S320},
        {"02E1", SONY_PID_S330, NFC_S330},
        {"06C1", SONY_PID_S380S, NFC_S380},
        {"06C3", SONY_PID_S380P, NFC_S380}
    };
    for (size_t i = 0; i < sizeof models / sizeof models[0]; i++) {
        if (_strnicmp(text, models[i].id, 4) == 0) {
            *kind = models[i].kind;
            *pid = models[i].pid;
            return true;
        }
    }
    return false;
}

static const char *reader_model(uint16_t pid)
{
    switch (pid) {
    case SONY_PID_S320: return "SONY RC-S320";
    case SONY_PID_S330: return "SONY RC-S330/360/370";
    case SONY_PID_S380S: return "SONY RC-S380/S";
    case SONY_PID_S380P: return "SONY RC-S380/P";
    default: return "SONY unknown";
    }
}

static uint32_t instance_hash(const char *text)
{
    /* Stable display/sort ID, not a USB topology location. */
    uint32_t h = UINT32_C(2166136261);
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c >= 'a' && c <= 'z')
            c = (unsigned char)(c - 'a' + 'A');
        h = (h ^ c) * UINT32_C(16777619);
    }
    return h;
}

static bool parse_guid(const char *text, GUID *guid)
{
    WCHAR wide[64];
    int count = MultiByteToWideChar(CP_ACP, 0, text, -1, wide,
                                    (int)(sizeof wide / sizeof wide[0]));
    return count > 0 && CLSIDFromString(wide, guid) == S_OK;
}

static bool same_guid(const GUID *a, const GUID *b)
{
    return memcmp(a, b, sizeof *a) == 0;
}

/* WinUSB INF/MS OS descriptors register a per-device interface GUID.
 * Discover it from the device's registry instead of assuming one fixed GUID.
 * Both REG_SZ DeviceInterfaceGUID and REG_MULTI_SZ DeviceInterfaceGUIDs
 * are accepted, including when written into a driver software key.
 */
static size_t device_guids(HDEVINFO set, SP_DEVINFO_DATA *device,
                           GUID guids[MAX_GUIDS])
{
    static const DWORD keys[] = { DIREG_DEV, DIREG_DRV };
    static const char *names[] = { "DeviceInterfaceGUIDs", "DeviceInterfaceGUID" };
    size_t found = 0;
    for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) {
        HKEY key = SetupDiOpenDevRegKey(set, device, DICS_FLAG_GLOBAL, 0,
                                       keys[k], KEY_READ);
        if (key == INVALID_HANDLE_VALUE)
            continue;
        for (size_t n = 0; n < sizeof names / sizeof names[0]; n++) {
            DWORD type = 0, bytes = 0;
            if (RegQueryValueExA(key, names[n], NULL, &type, NULL, &bytes) != ERROR_SUCCESS ||
                (type != REG_SZ && type != REG_MULTI_SZ) || bytes < 2 || bytes > 65536)
                continue;
            char *buffer = calloc((size_t)bytes + 2, 1);
            if (!buffer)
                continue;
            if (RegQueryValueExA(key, names[n], NULL, &type, (BYTE *)buffer, &bytes)
                == ERROR_SUCCESS) {
                const char *p = buffer;
                const char *end = buffer + bytes;
                while (p < end && *p && found < MAX_GUIDS) {
                    GUID guid;
                    if (parse_guid(p, &guid)) {
                        bool duplicate = false;
                        for (size_t i = 0; i < found; i++)
                            if (same_guid(&guids[i], &guid)) {
                                duplicate = true;
                                break;
                            }
                        if (!duplicate)
                            guids[found++] = guid;
                    }
                    p += strlen(p) + 1;
                    if (type == REG_SZ)
                        break;
                }
            }
            free(buffer);
        }
        RegCloseKey(key);
    }
    return found;
}

/* Search registered interfaces for this *particular* PnP instance.
 * A GUID can be shared by several physical readers, so comparing the
 * device instance ID is essential when multiple readers are connected.
 */
static bool device_path(const GUID *guid, const char *instance_id,
                        char path[NFCIDM_USB_PATH_SIZE])
{
    HDEVINFO set = SetupDiGetClassDevsA(guid, NULL, NULL,
                                       DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return false;
    bool found = false;
    for (DWORD i = 0; ; i++) {
        SP_DEVICE_INTERFACE_DATA interface_data;
        memset(&interface_data, 0, sizeof interface_data);
        interface_data.cbSize = sizeof interface_data;
        if (!SetupDiEnumDeviceInterfaces(set, NULL, guid, i, &interface_data))
            break;
        DWORD needed = 0;
        (void)SetupDiGetDeviceInterfaceDetailA(set, &interface_data,
                                                NULL, 0, &needed, NULL);
        if (needed < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A) || needed > 65536)
            continue;
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *detail = malloc(needed);
        if (!detail)
            continue;
        detail->cbSize = sizeof *detail;
        SP_DEVINFO_DATA info;
        memset(&info, 0, sizeof info);
        info.cbSize = sizeof info;
        char found_id[MAX_DEVICE_ID];
        if (SetupDiGetDeviceInterfaceDetailA(set, &interface_data, detail,
                                              needed, NULL, &info) &&
            SetupDiGetDeviceInstanceIdA(set, &info, found_id,
                                        (DWORD)sizeof found_id, NULL) &&
            _stricmp(instance_id, found_id) == 0 &&
            strlen(detail->DevicePath) < NFCIDM_USB_PATH_SIZE) {
            strcpy(path, detail->DevicePath);
            found = true;
        }
        free(detail);
        if (found)
            break;
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

int nfc_usb_enumerate(nfc_reader *readers, size_t capacity, size_t *count,
                      char *error, size_t error_cap)
{
    HDEVINFO set = SetupDiGetClassDevsA(NULL, "USB", NULL,
                                       DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) {
        nfc_error(error, error_cap, "SetupAPI USB enumeration failed: Windows error %lu",
                  (unsigned long)GetLastError());
        return NFC_ERR;
    }
    bool missing_driver = false;
    uint16_t missing_pid = 0;
    for (DWORD i = 0; ; i++) {
        SP_DEVINFO_DATA device;
        memset(&device, 0, sizeof device);
        device.cbSize = sizeof device;
        if (!SetupDiEnumDeviceInfo(set, i, &device))
            break;
        char id[MAX_DEVICE_ID];
        nfc_kind kind;
        uint16_t pid;
        if (!SetupDiGetDeviceInstanceIdA(set, &device, id,
                                         (DWORD)sizeof id, NULL) ||
            !identify_sony(id, &kind, &pid))
            continue;
        char service[128] = "";
        DWORD type = 0;
        if (!SetupDiGetDeviceRegistryPropertyA(set, &device, SPDRP_SERVICE,
                &type, (BYTE *)service, (DWORD)sizeof service, NULL) ||
            type != REG_SZ || _stricmp(service, "WinUSB") != 0) {
            missing_driver = true;
            missing_pid = pid;
            continue;
        }
        GUID guids[MAX_GUIDS];
        size_t nguid = device_guids(set, &device, guids);
        char path[NFCIDM_USB_PATH_SIZE] = "";
        bool has_path = false;
        for (size_t j = 0; j < nguid && !has_path; j++)
            has_path = device_path(&guids[j], id, path);
        if (!has_path) {
            missing_driver = true;
            missing_pid = pid;
            continue;
        }
        if (*count >= capacity) {
            SetupDiDestroyDeviceInfoList(set);
            nfc_error(error, error_cap, "too many readers (limit %lu)",
                      (unsigned long)capacity);
            return NFC_ERR;
        }
        nfc_reader *r = &readers[(*count)++];
        memset(r, 0, sizeof *r);
        r->kind = kind;
        r->vid = SONY_VID;
        r->pid = pid;
        r->location = instance_hash(id);
        strcpy(r->name, reader_model(pid));
        strcpy(r->usb_path, path);
    }
    SetupDiDestroyDeviceInfoList(set);
    if (missing_driver)
        nfc_error(error, error_cap,
            "%s (054c:%04x) detected without a usable WinUSB interface; "
            "bind WinUSB (winusb.sys) with a device-interface GUID and reconnect",
            reader_model(missing_pid), (unsigned)missing_pid);
    /* A missing Sony driver is not fatal to PC/SC readers such as ACR122U. */
    return NFC_OK;
}

static void win_close(nfc_usb *usb)
{
    windows_usb *w = usb->opaque;
    if (!w)
        return;
    if (w->handle)
        WinUsb_Free(w->handle);
    if (w->file != INVALID_HANDLE_VALUE)
        CloseHandle(w->file);
    free(w);
}

/* Wait for an overlapped WinUSB request and finish cancellation before the
 * request's stack OVERLAPPED structure or caller buffer can be released.
 */
static int transfer_result(nfc_usb *usb, windows_usb *w, OVERLAPPED *overlapped,
                           BOOL started, DWORD start_error, ULONG *length,
                           unsigned timeout_ms, const char *action)
{
    if (started)
        return NFC_OK;
    if (start_error != ERROR_IO_PENDING)
        return win_error(usb, action, start_error);
    DWORD result = WaitForSingleObject(overlapped->hEvent, timeout_ms ? timeout_ms : 1);
    if (result == WAIT_TIMEOUT) {
        (void)CancelIoEx(w->file, overlapped);
        (void)WinUsb_GetOverlappedResult(w->handle, overlapped, length, TRUE);
        nfc_error(usb->error, sizeof usb->error, "%s: timed out", action);
        return NFC_TIMEOUT;
    }
    if (result != WAIT_OBJECT_0) {
        DWORD error = GetLastError();
        (void)CancelIoEx(w->file, overlapped);
        (void)WinUsb_GetOverlappedResult(w->handle, overlapped, length, TRUE);
        return win_error(usb, action, error);
    }
    if (!WinUsb_GetOverlappedResult(w->handle, overlapped, length, FALSE))
        return win_error(usb, action, GetLastError());
    return NFC_OK;
}

static int win_read(nfc_usb *usb, uint8_t *buffer, size_t size, unsigned timeout_ms)
{
    windows_usb *w = usb->opaque;
    if (!buffer || !size || size > ULONG_MAX)
        return NFC_ERR;
    /* Interrupt reads use one maximum-size USB packet, like macOS. */
    if (w->interrupt_in && size > w->in_packet_size)
        size = w->in_packet_size;
    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof overlapped);
    overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!overlapped.hEvent)
        return win_error(usb, "create read event", GetLastError());
    ULONG length = 0;
    BOOL started = WinUsb_ReadPipe(w->handle, w->pipe_in, buffer,
                                   (ULONG)size, &length, &overlapped);
    DWORD error = started ? ERROR_SUCCESS : GetLastError();
    int rc = transfer_result(usb, w, &overlapped, started, error,
                             &length, timeout_ms, w->interrupt_in ? "WinUSB interrupt read" : "WinUSB bulk read");
    CloseHandle(overlapped.hEvent);
    return rc == NFC_OK ? (int)length : rc;
}

static int win_write(nfc_usb *usb, const uint8_t *buffer, size_t size,
                     unsigned timeout_ms)
{
    windows_usb *w = usb->opaque;
    if (!w->pipe_out || !buffer || size > ULONG_MAX) {
        nfc_error(usb->error, sizeof usb->error,
                  "reader has no bulk OUT pipe");
        return NFC_ERR;
    }
    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof overlapped);
    overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!overlapped.hEvent)
        return win_error(usb, "create write event", GetLastError());
    ULONG length = 0;
    BOOL started = WinUsb_WritePipe(w->handle, w->pipe_out,
                                    (UCHAR *)buffer, (ULONG)size,
                                    &length, &overlapped);
    DWORD error = started ? ERROR_SUCCESS : GetLastError();
    int rc = transfer_result(usb, w, &overlapped, started, error,
                             &length, timeout_ms, "WinUSB bulk write");
    CloseHandle(overlapped.hEvent);
    if (rc == NFC_OK && length != size) {
        nfc_error(usb->error, sizeof usb->error, "WinUSB bulk write: short transfer");
        return NFC_ERR;
    }
    return rc;
}

static int win_control(nfc_usb *usb, uint8_t request, uint16_t value,
                       uint16_t index, const uint8_t *buffer, size_t size,
                       unsigned timeout_ms)
{
    windows_usb *w = usb->opaque;
    if (size > UINT16_MAX)
        return NFC_ERR;
    WINUSB_SETUP_PACKET setup;
    setup.RequestType = 0x40; /* vendor, device, host-to-device */
    setup.Request = request;
    setup.Value = value;
    setup.Index = index;
    setup.Length = (USHORT)size;
    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof overlapped);
    overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!overlapped.hEvent)
        return win_error(usb, "create control event", GetLastError());
    ULONG length = 0;
    BOOL started = WinUsb_ControlTransfer(w->handle, setup, (UCHAR *)buffer,
                                          (ULONG)size, &length, &overlapped);
    DWORD error = started ? ERROR_SUCCESS : GetLastError();
    int rc = transfer_result(usb, w, &overlapped, started, error,
                             &length, timeout_ms, "WinUSB vendor control");
    CloseHandle(overlapped.hEvent);
    if (rc == NFC_OK && length != size) {
        nfc_error(usb->error, sizeof usb->error, "WinUSB vendor control: short transfer");
        return NFC_ERR;
    }
    return rc;
}

int nfc_usb_open(const nfc_reader *reader, bool verbose, nfc_usb *usb)
{
    memset(usb, 0, sizeof *usb);
    usb->vid = reader->vid;
    usb->pid = reader->pid;
    usb->verbose = verbose;
    bool supported = reader->vid == SONY_VID && reader->usb_path[0] &&
        ((reader->kind == NFC_S320 && reader->pid == SONY_PID_S320) ||
         (reader->kind == NFC_S330 && reader->pid == SONY_PID_S330) ||
         (reader->kind == NFC_S380 && (reader->pid == SONY_PID_S380S ||
                                       reader->pid == SONY_PID_S380P)));
    if (!supported) {
        nfc_error(usb->error, sizeof usb->error,
                  "WinUSB transport does not support this reader VID:PID");
        return NFC_ERR;
    }
    windows_usb *w = calloc(1, sizeof *w);
    if (!w) {
        nfc_error(usb->error, sizeof usb->error, "out of memory");
        return NFC_ERR;
    }
    w->file = INVALID_HANDLE_VALUE;
    usb->opaque = w;
    usb->read_fn = win_read;
    usb->write_fn = win_write;
    usb->control_fn = win_control;
    usb->close_fn = win_close;
    w->file = CreateFileA(reader->usb_path, GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                          FILE_FLAG_OVERLAPPED, NULL);
    if (w->file == INVALID_HANDLE_VALUE) {
        (void)win_error(usb, "open WinUSB device", GetLastError());
        goto fail;
    }
    if (!WinUsb_Initialize(w->file, &w->handle)) {
        (void)win_error(usb, "WinUsb_Initialize", GetLastError());
        goto fail;
    }
    USB_INTERFACE_DESCRIPTOR interface_descriptor;
    if (!WinUsb_QueryInterfaceSettings(w->handle, 0, &interface_descriptor)) {
        (void)win_error(usb, "WinUsb_QueryInterfaceSettings", GetLastError());
        goto fail;
    }
    for (UCHAR i = 0; i < interface_descriptor.bNumEndpoints; i++) {
        WINUSB_PIPE_INFORMATION pipe;
        if (!WinUsb_QueryPipe(w->handle, 0, i, &pipe)) {
            (void)win_error(usb, "WinUsb_QueryPipe", GetLastError());
            goto fail;
        }
        if (reader->kind == NFC_S320) {
            if (pipe.PipeType == UsbdPipeTypeInterrupt && (pipe.PipeId & 0x80)) {
                w->pipe_in = pipe.PipeId;
                w->interrupt_in = true;
                w->in_packet_size = pipe.MaximumPacketSize;
            }
        } else if (pipe.PipeType == UsbdPipeTypeBulk) {
            if (pipe.PipeId & 0x80)
                w->pipe_in = pipe.PipeId;
            else
                w->pipe_out = pipe.PipeId;
        }
    }
    if (!w->pipe_in || (reader->kind == NFC_S320 && !w->in_packet_size) ||
        (reader->kind != NFC_S320 && !w->pipe_out)) {
        nfc_error(usb->error, sizeof usb->error,
                  "%s: WinUSB interface lacks required %s endpoints",
                  reader_model(reader->pid),
                  reader->kind == NFC_S320 ? "interrupt IN" : "bulk IN/OUT");
        goto fail;
    }
    return NFC_OK;
fail:
    win_close(usb);
    usb->opaque = NULL;
    usb->close_fn = NULL;
    return NFC_ERR;
}
