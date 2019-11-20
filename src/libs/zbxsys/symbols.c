

#include "common.h"
#include "symbols.h"

#include "log.h"

DWORD	(__stdcall *trx_GetGuiResources)(HANDLE, DWORD) = NULL;
BOOL	(__stdcall *trx_GetProcessIoCounters)(HANDLE, PIO_COUNTERS) = NULL;
BOOL	(__stdcall *trx_GetPerformanceInfo)(PPERFORMANCE_INFORMATION, DWORD) = NULL;
BOOL	(__stdcall *trx_GlobalMemoryStatusEx)(LPMEMORYSTATUSEX) = NULL;
BOOL	(__stdcall *trx_GetFileInformationByHandleEx)(HANDLE, TRX_FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD) = NULL;

static FARPROC	GetProcAddressAndLog(HMODULE hModule, const char *procName)
{
	FARPROC	ptr;

	if (NULL == (ptr = GetProcAddress(hModule, procName)))
		treegix_log(LOG_LEVEL_DEBUG, "unable to resolve symbol '%s'", procName);

	return ptr;
}

void	import_symbols(void)
{
	HMODULE	hModule;

	if (NULL != (hModule = GetModuleHandle(TEXT("USER32.DLL"))))
		trx_GetGuiResources = (DWORD (__stdcall *)(HANDLE, DWORD))GetProcAddressAndLog(hModule, "GetGuiResources");
	else
		treegix_log(LOG_LEVEL_DEBUG, "unable to get handle to USER32.DLL");

	if (NULL != (hModule = GetModuleHandle(TEXT("KERNEL32.DLL"))))
	{
		trx_GetProcessIoCounters = (BOOL (__stdcall *)(HANDLE, PIO_COUNTERS))GetProcAddressAndLog(hModule, "GetProcessIoCounters");
		trx_GlobalMemoryStatusEx = (BOOL (__stdcall *)(LPMEMORYSTATUSEX))GetProcAddressAndLog(hModule, "GlobalMemoryStatusEx");
		trx_GetFileInformationByHandleEx = (BOOL (__stdcall *)(HANDLE, TRX_FILE_INFO_BY_HANDLE_CLASS, LPVOID,
				DWORD))GetProcAddressAndLog(hModule, "GetFileInformationByHandleEx");
	}
	else
		treegix_log(LOG_LEVEL_DEBUG, "unable to get handle to KERNEL32.DLL");

	if (NULL != (hModule = GetModuleHandle(TEXT("PSAPI.DLL"))))
		trx_GetPerformanceInfo = (BOOL (__stdcall *)(PPERFORMANCE_INFORMATION, DWORD))GetProcAddressAndLog(hModule, "GetPerformanceInfo");
	else
		treegix_log(LOG_LEVEL_DEBUG, "unable to get handle to PSAPI.DLL");
}
