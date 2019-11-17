

#include "common.h"
#include "symbols.h"

#include "log.h"

DWORD	(__stdcall *zbx_GetGuiResources)(HANDLE, DWORD) = NULL;
BOOL	(__stdcall *zbx_GetProcessIoCounters)(HANDLE, PIO_COUNTERS) = NULL;
BOOL	(__stdcall *zbx_GetPerformanceInfo)(PPERFORMANCE_INFORMATION, DWORD) = NULL;
BOOL	(__stdcall *zbx_GlobalMemoryStatusEx)(LPMEMORYSTATUSEX) = NULL;
BOOL	(__stdcall *zbx_GetFileInformationByHandleEx)(HANDLE, TRX_FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD) = NULL;

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
		zbx_GetGuiResources = (DWORD (__stdcall *)(HANDLE, DWORD))GetProcAddressAndLog(hModule, "GetGuiResources");
	else
		treegix_log(LOG_LEVEL_DEBUG, "unable to get handle to USER32.DLL");

	if (NULL != (hModule = GetModuleHandle(TEXT("KERNEL32.DLL"))))
	{
		zbx_GetProcessIoCounters = (BOOL (__stdcall *)(HANDLE, PIO_COUNTERS))GetProcAddressAndLog(hModule, "GetProcessIoCounters");
		zbx_GlobalMemoryStatusEx = (BOOL (__stdcall *)(LPMEMORYSTATUSEX))GetProcAddressAndLog(hModule, "GlobalMemoryStatusEx");
		zbx_GetFileInformationByHandleEx = (BOOL (__stdcall *)(HANDLE, TRX_FILE_INFO_BY_HANDLE_CLASS, LPVOID,
				DWORD))GetProcAddressAndLog(hModule, "GetFileInformationByHandleEx");
	}
	else
		treegix_log(LOG_LEVEL_DEBUG, "unable to get handle to KERNEL32.DLL");

	if (NULL != (hModule = GetModuleHandle(TEXT("PSAPI.DLL"))))
		zbx_GetPerformanceInfo = (BOOL (__stdcall *)(PPERFORMANCE_INFORMATION, DWORD))GetProcAddressAndLog(hModule, "GetPerformanceInfo");
	else
		treegix_log(LOG_LEVEL_DEBUG, "unable to get handle to PSAPI.DLL");
}
