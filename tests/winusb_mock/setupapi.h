#ifndef MOCK_SETUPAPI_H
#define MOCK_SETUPAPI_H
#include "windows.h"
#define DIGCF_PRESENT 0x02U
#define DIGCF_ALLCLASSES 0x04U
#define DIGCF_DEVICEINTERFACE 0x10U
#define DICS_FLAG_GLOBAL 1U
#define DIREG_DEV 1U
#define DIREG_DRV 2U
#define SPDRP_SERVICE 4U
typedef void *HDEVINFO;
typedef struct { DWORD cbSize; GUID ClassGuid; DWORD DevInst; uintptr_t Reserved; } SP_DEVINFO_DATA;
typedef struct { DWORD cbSize; GUID InterfaceClassGuid; DWORD Flags; uintptr_t Reserved; } SP_DEVICE_INTERFACE_DATA;
typedef struct { DWORD cbSize; char DevicePath[1]; } SP_DEVICE_INTERFACE_DETAIL_DATA_A;
HDEVINFO SetupDiGetClassDevsA(const GUID *, const char *, void *, DWORD);
BOOL SetupDiEnumDeviceInfo(HDEVINFO, DWORD, SP_DEVINFO_DATA *);
BOOL SetupDiGetDeviceInstanceIdA(HDEVINFO, SP_DEVINFO_DATA *, char *, DWORD, DWORD *);
BOOL SetupDiGetDeviceRegistryPropertyA(HDEVINFO, SP_DEVINFO_DATA *, DWORD, DWORD *, BYTE *, DWORD, DWORD *);
HKEY SetupDiOpenDevRegKey(HDEVINFO, SP_DEVINFO_DATA *, DWORD, DWORD, DWORD, DWORD);
BOOL SetupDiDestroyDeviceInfoList(HDEVINFO);
BOOL SetupDiEnumDeviceInterfaces(HDEVINFO, SP_DEVINFO_DATA *, const GUID *, DWORD, SP_DEVICE_INTERFACE_DATA *);
BOOL SetupDiGetDeviceInterfaceDetailA(HDEVINFO, SP_DEVICE_INTERFACE_DATA *, SP_DEVICE_INTERFACE_DETAIL_DATA_A *, DWORD, DWORD *, SP_DEVINFO_DATA *);
#endif
