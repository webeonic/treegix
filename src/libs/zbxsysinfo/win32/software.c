

#include "sysinfo.h"

int	SYSTEM_SW_ARCH(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);

	SYSTEM_INFO	si;
	const char	*arch;
	PGNSI		pGNSI;

	memset(&si, 0, sizeof(si));

	if (NULL != (pGNSI = (PGNSI)GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "GetNativeSystemInfo")))
		pGNSI(&si);
	else
		GetSystemInfo(&si);

	switch (si.wProcessorArchitecture)
	{
		case PROCESSOR_ARCHITECTURE_INTEL:
			arch = "x86";
			break;
		case PROCESSOR_ARCHITECTURE_AMD64:
			arch = "x64";
			break;
		case PROCESSOR_ARCHITECTURE_IA64:
			arch = "Intel Itanium-based";
			break;
		default:
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown processor architecture."));
			return SYSINFO_RET_FAIL;
	}

	SET_STR_RESULT(result, zbx_strdup(NULL, arch));

	return SYSINFO_RET_OK;
}
