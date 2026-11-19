#ifndef MOCK_WINDOWS_H
#define MOCK_WINDOWS_H
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
#include <strings.h>
#define _strnicmp strncasecmp
#define _stricmp strcasecmp
#define WINAPI
#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define ERROR_SUCCESS 0UL
#define ERROR_FILE_NOT_FOUND 2UL
#define ERROR_INVALID_HANDLE 6UL
#define ERROR_SEM_TIMEOUT 121UL
#define ERROR_NO_SUCH_DEVICE 433UL
#define ERROR_DEVICE_NOT_CONNECTED 1167UL
#define ERROR_OPERATION_ABORTED 995UL
#define ERROR_IO_PENDING 997UL
#define WAIT_TIMEOUT 258UL
#define WAIT_OBJECT_0 0UL
#define CP_ACP 0U
#define GENERIC_READ 0x80000000UL
#define GENERIC_WRITE 0x40000000UL
#define FILE_SHARE_READ 1UL
#define FILE_SHARE_WRITE 2UL
#define OPEN_EXISTING 3UL
#define FILE_FLAG_OVERLAPPED 0x40000000UL
#define KEY_READ 0x20019UL
#define REG_SZ 1UL
#define REG_MULTI_SZ 7UL
typedef void *HANDLE;
typedef void *HKEY;
typedef uint8_t BYTE;
typedef uint8_t UCHAR;
typedef uint16_t USHORT;
typedef uint32_t DWORD;
typedef uint32_t ULONG;
typedef int BOOL;
typedef wchar_t WCHAR;
typedef struct _GUID { uint32_t Data1; uint16_t Data2,Data3; uint8_t Data4[8]; } GUID;
typedef struct _OVERLAPPED { uintptr_t Internal, InternalHigh; unsigned long Offset, OffsetHigh; HANDLE hEvent; } OVERLAPPED;
DWORD GetLastError(void);
HANDLE CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
BOOL CloseHandle(HANDLE);
HANDLE CreateEventA(void *, BOOL, BOOL, const char *);
DWORD WaitForSingleObject(HANDLE, DWORD);
BOOL CancelIoEx(HANDLE, OVERLAPPED *);
int MultiByteToWideChar(unsigned, DWORD, const char *, int, WCHAR *, int);
long RegQueryValueExA(HKEY, const char *, void *, DWORD *, BYTE *, DWORD *);
long RegCloseKey(HKEY);
#endif
