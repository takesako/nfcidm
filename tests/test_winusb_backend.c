/* SPDX-License-Identifier: MIT */
/* Windows API doubles: multi-device discovery, bulk/interrupt/control, timeout. */
#include "nfc_usb.h"
#include "windows.h"
#include "setupapi.h"
#include "winusb.h"
#include "objbase.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int driver_available = 1;
static int read_pending = 0;
static int cancelled = 0;
static DWORD last_error = ERROR_SUCCESS;
static const char *ids[] = {
    "USB\\VID_054C&PID_02E1\\FIRST",
    "USB\\VID_054C&PID_02E1\\SECOND",
    "USB\\VID_054C&PID_01BB\\S320",
    "USB\\VID_054C&PID_06C1\\S380S",
    "USB\\VID_054C&PID_06C3\\S380P",
    "USB\\VID_072F&PID_2200\\ACR122U",
    "USB\\VID_054C&PID_02E2\\OTHER"
};
static const char *paths[] = {
    "\\\\?\\usb#vid_054c&pid_02e1#first",
    "\\\\?\\usb#vid_054c&pid_02e1#second",
    "\\\\?\\usb#vid_054c&pid_01bb#s320",
    "\\\\?\\usb#vid_054c&pid_06c1#s380s",
    "\\\\?\\usb#vid_054c&pid_06c3#s380p"
};
static int selected = -1;

DWORD GetLastError(void) { return last_error; }
HDEVINFO SetupDiGetClassDevsA(const GUID *guid, const char *e, void *hwnd, DWORD flags)
{ (void)e; (void)hwnd; (void)flags; return (HDEVINFO)(intptr_t)(guid ? 2 : 1); }
BOOL SetupDiEnumDeviceInfo(HDEVINFO set, DWORD i, SP_DEVINFO_DATA *d)
{ if (set != (void *)(intptr_t)1 || i >= 7) return FALSE; d->DevInst = i; return TRUE; }
BOOL SetupDiGetDeviceInstanceIdA(HDEVINFO set, SP_DEVINFO_DATA *d, char *buf, DWORD sz, DWORD *needed)
{ (void)set; (void)needed; if (sz <= strlen(ids[d->DevInst])) return FALSE; strcpy(buf, ids[d->DevInst]); return TRUE; }
BOOL SetupDiGetDeviceRegistryPropertyA(HDEVINFO set, SP_DEVINFO_DATA *d, DWORD field, DWORD *type, BYTE *buf, DWORD sz, DWORD *need)
{ (void)set; (void)d; (void)field; (void)need; const char *s = driver_available ? "WinUSB" : ""; if (sz <= strlen(s)) return FALSE; *type = REG_SZ; strcpy((char *)buf, s); return TRUE; }
HKEY SetupDiOpenDevRegKey(HDEVINFO set, SP_DEVINFO_DATA *d, DWORD flags, DWORD spec, DWORD key, DWORD access)
{ (void)set; (void)d; (void)flags; (void)spec; (void)key; (void)access; return (HKEY)(intptr_t)3; }
long RegQueryValueExA(HKEY key, const char *name, void *reserved, DWORD *type, BYTE *buf, DWORD *bytes)
{
    (void)key; (void)reserved;
    if (strcmp(name, "DeviceInterfaceGUIDs") != 0) return ERROR_FILE_NOT_FOUND;
    const char id[] = "{ABCDEFAB-1234-5678-ABCD-1234567890AB}\0";
    *type = REG_MULTI_SZ;
    if (!buf) { *bytes = (DWORD)sizeof id; return ERROR_SUCCESS; }
    if (*bytes < sizeof id) return ERROR_FILE_NOT_FOUND;
    memcpy(buf, id, sizeof id);
    *bytes = (DWORD)sizeof id;
    return ERROR_SUCCESS;
}
long RegCloseKey(HKEY key) { (void)key; return ERROR_SUCCESS; }
BOOL SetupDiEnumDeviceInterfaces(HDEVINFO set, SP_DEVINFO_DATA *dev, const GUID *guid, DWORD i, SP_DEVICE_INTERFACE_DATA *iface)
{ (void)dev; (void)guid; if (set != (void *)(intptr_t)2 || i >= 5) return FALSE; iface->Reserved = i; return TRUE; }
BOOL SetupDiGetDeviceInterfaceDetailA(HDEVINFO set, SP_DEVICE_INTERFACE_DATA *iface, SP_DEVICE_INTERFACE_DETAIL_DATA_A *detail, DWORD capacity, DWORD *needed, SP_DEVINFO_DATA *dev)
{
    (void)set; int i = (int)iface->Reserved;
    DWORD required = (DWORD)(sizeof(*detail) + strlen(paths[i]) + 1);
    if (needed) *needed = required;
    if (!detail || capacity < required) return FALSE;
    strcpy(detail->DevicePath, paths[i]);
    dev->DevInst = (DWORD)i;
    return TRUE;
}
BOOL SetupDiDestroyDeviceInfoList(HDEVINFO set) { (void)set; return TRUE; }
long CLSIDFromString(const WCHAR *s, GUID *guid)
{ if (!s || !guid) return -1; memset(guid, 0, sizeof *guid); guid->Data1 = 0xabcdefab; return S_OK; }
int MultiByteToWideChar(unsigned cp, DWORD flags, const char *s, int len, WCHAR *out, int cap)
{ (void)cp; (void)flags; (void)len; int n = (int)strlen(s) + 1; if (cap < n) return 0; for(int i=0;i<n;i++) out[i]=(WCHAR)(unsigned char)s[i]; return n; }
HANDLE CreateFileA(const char *path, DWORD access, DWORD share, void *security, DWORD create, DWORD flags, HANDLE tpl)
{ (void)access;(void)share;(void)security;(void)create;(void)flags;(void)tpl;
  for (int i=0;i<5;i++) if (!strcmp(path, paths[i])) {
    selected=i; return (HANDLE)(intptr_t)4;
  }
  return INVALID_HANDLE_VALUE; }
BOOL CloseHandle(HANDLE h) { (void)h; return TRUE; }
HANDLE CreateEventA(void *p, BOOL m, BOOL initial, const char *name)
{ (void)p;(void)m;(void)initial;(void)name; return (HANDLE)(intptr_t)6; }
DWORD WaitForSingleObject(HANDLE h, DWORD ms)
{ (void)h; (void)ms; return read_pending ? WAIT_TIMEOUT : WAIT_OBJECT_0; }
BOOL CancelIoEx(HANDLE h, OVERLAPPED *ov) { (void)h;(void)ov; cancelled++; return TRUE; }
BOOL WinUsb_Initialize(HANDLE file, WINUSB_INTERFACE_HANDLE *out)
{ (void)file; *out=(WINUSB_INTERFACE_HANDLE)(intptr_t)5; return TRUE; }
BOOL WinUsb_Free(WINUSB_INTERFACE_HANDLE h) { (void)h; return TRUE; }
BOOL WinUsb_QueryInterfaceSettings(WINUSB_INTERFACE_HANDLE h, UCHAR alt, USB_INTERFACE_DESCRIPTOR *d)
{ (void)h; (void)alt; memset(d,0,sizeof *d); d->bNumEndpoints= selected==2 ? 1 : 2; return TRUE; }
BOOL WinUsb_QueryPipe(WINUSB_INTERFACE_HANDLE h, UCHAR alt, UCHAR i, WINUSB_PIPE_INFORMATION *p)
{ (void)h; (void)alt; memset(p,0,sizeof *p);
  p->PipeType= selected==2 ? UsbdPipeTypeInterrupt : UsbdPipeTypeBulk;
  p->PipeId=i==0 ? 0x81 : 0x02;
  p->MaximumPacketSize=selected==2 ? 8 : 64; return TRUE; }
BOOL WinUsb_ReadPipe(WINUSB_INTERFACE_HANDLE h, UCHAR pipe, UCHAR *buf, ULONG cap, ULONG *got, OVERLAPPED *ov)
{ (void)h; (void)ov; assert(pipe==0x81 && cap>=4);
  if (selected==2) assert(cap==8); /* interrupt reads capped at packet size */
  if (read_pending) {last_error=ERROR_IO_PENDING;return FALSE;}
  buf[0]=0x01;buf[1]=0x02;buf[2]=0x03;buf[3]=0x04;*got=4;return TRUE; }
BOOL WinUsb_WritePipe(WINUSB_INTERFACE_HANDLE h, UCHAR pipe, UCHAR *buf, ULONG cap, ULONG *got, OVERLAPPED *ov)
{ (void)h;(void)ov;assert(selected!=2 && pipe==0x02 && cap==3 && buf[0]==0xa0);*got=cap;return TRUE; }
BOOL WinUsb_ControlTransfer(WINUSB_INTERFACE_HANDLE h, WINUSB_SETUP_PACKET p, UCHAR *buf, ULONG cap, ULONG *got, OVERLAPPED *ov)
{ (void)h;(void)ov;assert(p.RequestType==0x40 && p.Request==0x23 && buf[0]==0xa0);*got=cap;return TRUE; }
BOOL WinUsb_GetOverlappedResult(WINUSB_INTERFACE_HANDLE h, OVERLAPPED *ov, ULONG *got, BOOL wait)
{ (void)h;(void)ov;(void)got;(void)wait;return FALSE; }

int main(void)
{
    nfc_reader readers[8];
    size_t count = 0;
    char error[256] = "";
    memset(readers, 0, sizeof readers);
    assert(nfc_usb_enumerate(readers, 8, &count, error, sizeof error) == NFC_OK);
    assert(count == 5 && error[0] == '\0');
    assert(readers[0].kind == NFC_S330 && readers[0].pid == 0x02e1);
    assert(readers[2].kind == NFC_S320 && readers[2].pid == 0x01bb);
    assert(readers[3].kind == NFC_S380 && readers[3].pid == 0x06c1);
    assert(readers[4].kind == NFC_S380 && readers[4].pid == 0x06c3);
    assert(strcmp(readers[0].usb_path, paths[0])==0);
    assert(strcmp(readers[1].usb_path, paths[1])==0);
    assert(readers[0].location != readers[1].location);
    nfc_usb usb;
    assert(nfc_usb_open(&readers[1], false, &usb) == NFC_OK);
    assert(selected==1);
    uint8_t req[] = {0xa0,0xa1,0xa2};
    uint8_t response[8] = {0};
    assert(nfc_usb_write(&usb, req, sizeof req, 200) == NFC_OK);
    assert(nfc_usb_read(&usb, response, sizeof response, 200)==4);
    assert(response[0]==1 && response[3]==4);
    assert(nfc_usb_control(&usb, 0x23, 0, 0, req, sizeof req, 200)==NFC_OK);
    read_pending=1;
    assert(nfc_usb_read(&usb, response, sizeof response, 10)==NFC_TIMEOUT);
    assert(cancelled == 1);
    nfc_usb_close(&usb);
    read_pending=0;
    for (int idx=2; idx<=4; idx++) {
        assert(nfc_usb_open(&readers[idx], false, &usb) == NFC_OK);
        assert(selected == idx);
        memset(response,0,sizeof response);
        assert(nfc_usb_read(&usb, response, idx==2 ? 4096 : sizeof response, 200)==4);
        assert(response[0] == 1);
        if (idx==2) {
            assert(nfc_usb_write(&usb, req, sizeof req, 200) == NFC_ERR);
            assert(strstr(usb.error, "no bulk OUT") != NULL);
            assert(nfc_usb_control(&usb, 0x23, 0, 0, req, sizeof req, 200) == NFC_OK);
        } else {
            assert(nfc_usb_write(&usb, req, sizeof req, 200)==NFC_OK);
        }
        nfc_usb_close(&usb);
    }
    driver_available=0;
    count=0;
    memset(error, 0, sizeof error);
    assert(nfc_usb_enumerate(readers, 8, &count, error, sizeof error)==NFC_OK);
    assert(count==0 && strstr(error, "WinUSB")!=NULL);
    puts("Windows WinUSB transport mock tests passed (no hardware)");
    return 0;
}
