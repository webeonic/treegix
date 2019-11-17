

#ifndef TREEGIX_SYMBOLS_H
#define TREEGIX_SYMBOLS_H

#if defined(_WINDOWS)

/* some definitions which are not available on older MS Windows versions */
typedef enum {
	zbx_FileIdInfo	= 18	/* we need only one value, the rest of enumerated values are omitted here */
} ZBX_FILE_INFO_BY_HANDLE_CLASS;

typedef struct {
	ULONGLONG	LowPart;
	ULONGLONG	HighPart;
} ZBX_EXT_FILE_ID_128;

typedef struct {
	ULONGLONG		VolumeSerialNumber;
	ZBX_EXT_FILE_ID_128	FileId;
} ZBX_FILE_ID_INFO;

DWORD	(__stdcall *zbx_GetGuiResources)(HANDLE, DWORD);
BOOL	(__stdcall *zbx_GetProcessIoCounters)(HANDLE, PIO_COUNTERS);
BOOL	(__stdcall *zbx_GetPerformanceInfo)(PPERFORMANCE_INFORMATION, DWORD);
BOOL	(__stdcall *zbx_GlobalMemoryStatusEx)(LPMEMORYSTATUSEX);
BOOL	(__stdcall *zbx_GetFileInformationByHandleEx)(HANDLE, ZBX_FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD);

void	import_symbols(void);

#else
#	define import_symbols()
#endif

#endif
