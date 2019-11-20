

#ifndef TREEGIX_SYMBOLS_H
#define TREEGIX_SYMBOLS_H

#if defined(_WINDOWS)

/* some definitions which are not available on older MS Windows versions */
typedef enum {
	trx_FileIdInfo	= 18	/* we need only one value, the rest of enumerated values are omitted here */
} TRX_FILE_INFO_BY_HANDLE_CLASS;

typedef struct {
	ULONGLONG	LowPart;
	ULONGLONG	HighPart;
} TRX_EXT_FILE_ID_128;

typedef struct {
	ULONGLONG		VolumeSerialNumber;
	TRX_EXT_FILE_ID_128	FileId;
} TRX_FILE_ID_INFO;

DWORD	(__stdcall *trx_GetGuiResources)(HANDLE, DWORD);
BOOL	(__stdcall *trx_GetProcessIoCounters)(HANDLE, PIO_COUNTERS);
BOOL	(__stdcall *trx_GetPerformanceInfo)(PPERFORMANCE_INFORMATION, DWORD);
BOOL	(__stdcall *trx_GlobalMemoryStatusEx)(LPMEMORYSTATUSEX);
BOOL	(__stdcall *trx_GetFileInformationByHandleEx)(HANDLE, TRX_FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD);

void	import_symbols(void);

#else
#	define import_symbols()
#endif

#endif
