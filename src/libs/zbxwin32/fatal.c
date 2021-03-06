

#include "common.h"
#include "log.h"

#include <excpt.h>
#include <DbgHelp.h>

#pragma comment(lib, "DbgHelp.lib")

#define STACKWALK_MAX_NAMELEN	4096

#define TRX_LSHIFT(value, bits)	(((unsigned __int64)value) << bits)

extern const char	*progname;

#ifdef _M_X64

#define TRX_IMAGE_FILE_MACHINE	IMAGE_FILE_MACHINE_AMD64

static void	print_register(const char *name, unsigned __int64 value)
{
	treegix_log(LOG_LEVEL_CRIT, "%-7s = %16I64x = %20I64u = %20I64d", name, value, value, value);
}

static void	print_fatal_info(CONTEXT *pctx)
{
	treegix_log(LOG_LEVEL_CRIT, "====== Fatal information: ======");

	treegix_log(LOG_LEVEL_CRIT, "Program counter: 0x%08lx", pctx->Rip);
	treegix_log(LOG_LEVEL_CRIT, "=== Registers: ===");

	print_register("r8", pctx->R8);
	print_register("r9", pctx->R9);
	print_register("r10", pctx->R10);
	print_register("r11", pctx->R11);
	print_register("r12", pctx->R12);
	print_register("r13", pctx->R13);
	print_register("r14", pctx->R14);
	print_register("r15", pctx->R15);

	print_register("rdi", pctx->Rdi);
	print_register("rsi", pctx->Rsi);
	print_register("rbp", pctx->Rbp);

	print_register("rbx", pctx->Rbx);
	print_register("rdx", pctx->Rdx);
	print_register("rax", pctx->Rax);
	print_register("rcx", pctx->Rcx);

	print_register("rsp", pctx->Rsp);
	print_register("efl", pctx->EFlags);
	print_register("csgsfs", TRX_LSHIFT(pctx->SegCs, 24) | TRX_LSHIFT(pctx->SegGs, 16) | TRX_LSHIFT(pctx->SegFs, 8));
}

#else

#define TRX_IMAGE_FILE_MACHINE	IMAGE_FILE_MACHINE_I386

static void	print_register(const char *name, unsigned __int32 value)
{
	treegix_log(LOG_LEVEL_CRIT, "%-7s = %16lx = %20lu = %20ld", name, value, value, value);
}

static void	print_fatal_info(CONTEXT *pctx)
{
	treegix_log(LOG_LEVEL_CRIT, "====== Fatal information: ======");

	treegix_log(LOG_LEVEL_CRIT, "Program counter: 0x%04x", pctx->Eip);
	treegix_log(LOG_LEVEL_CRIT, "=== Registers: ===");

	print_register("edi", pctx->Edi);
	print_register("esi", pctx->Esi);
	print_register("ebp", pctx->Ebp);

	print_register("ebx", pctx->Ebx);
	print_register("edx", pctx->Edx);
	print_register("eax", pctx->Eax);
	print_register("ecx", pctx->Ecx);

	print_register("esp", pctx->Esp);
	print_register("efl", pctx->EFlags);
	print_register("csgsfs", TRX_LSHIFT(pctx->SegCs, 24) | TRX_LSHIFT(pctx->SegGs, 16) | TRX_LSHIFT(pctx->SegFs, 8));
}

#endif

typedef BOOL (WINAPI *SymGetLineFromAddrW64_func_t)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
typedef BOOL (WINAPI *SymFromAddr_func_t)(HANDLE a, DWORD64 b , PDWORD64 c, PSYMBOL_INFO d);

void	trx_backtrace(void)
{
}

static void	print_backtrace(CONTEXT *pctx)
{
	SymGetLineFromAddrW64_func_t	trx_SymGetLineFromAddrW64 = NULL;
	SymFromAddr_func_t		trx_SymFromAddr	= NULL;

	CONTEXT			ctx, ctxcount;
	STACKFRAME64		s, scount;
	PSYMBOL_INFO		pSym = NULL;
	HMODULE			hModule;
	HANDLE			hProcess, hThread;
	DWORD64			offset;
	wchar_t			szProcessName[MAX_PATH];
	char			*process_name = NULL, *process_path = NULL, *frame = NULL;
	size_t			frame_alloc = 0, frame_offset;
	int			nframes = 0;

	ctx = *pctx;

	treegix_log(LOG_LEVEL_CRIT, "=== Backtrace: ===");

	memset(&s, 0, sizeof(s));

	s.AddrPC.Mode = AddrModeFlat;
	s.AddrFrame.Mode = AddrModeFlat;
	s.AddrStack.Mode = AddrModeFlat;

#ifdef _M_X64
	s.AddrPC.Offset = ctx.Rip;
	s.AddrFrame.Offset = ctx.Rbp;
	s.AddrStack.Offset = ctx.Rsp;
#else
	s.AddrPC.Offset = ctx.Eip;
	s.AddrFrame.Offset = ctx.Ebp;
	s.AddrStack.Offset = ctx.Esp;
#endif
	hProcess = GetCurrentProcess();
	hThread = GetCurrentThread();

	if (0 != GetModuleFileNameEx(hProcess, NULL, szProcessName, ARRSIZE(szProcessName)))
	{
		char	*ptr;
		size_t	path_alloc = 0, path_offset = 0;

		process_name = trx_unicode_to_utf8(szProcessName);

		if (NULL != (ptr = strstr(process_name, progname)))
			trx_strncpy_alloc(&process_path, &path_alloc, &path_offset, process_name, ptr - process_name);
	}

	if (NULL != (hModule = GetModuleHandle(TEXT("DbgHelp.DLL"))))
	{
		trx_SymGetLineFromAddrW64 = (SymGetLineFromAddrW64_func_t)GetProcAddress(hModule,
				"SymGetLineFromAddr64");
		trx_SymFromAddr = (SymFromAddr_func_t)GetProcAddress(hModule, "SymFromAddr");
	}

	if (NULL != trx_SymFromAddr || NULL != trx_SymGetLineFromAddrW64)
	{
		SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES);

		if (FALSE != SymInitialize(hProcess, process_path, TRUE))
		{
			pSym = (PSYMBOL_INFO) trx_malloc(NULL, sizeof(SYMBOL_INFO) + MAX_SYM_NAME);
			memset(pSym, 0, sizeof(SYMBOL_INFO) + MAX_SYM_NAME);
			pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
			pSym->MaxNameLen = MAX_SYM_NAME;
		}
	}

	scount = s;
	ctxcount = ctx;

	/* get number of frames, ctxcount may be modified during StackWalk64() calls */
	while (TRUE == StackWalk64(TRX_IMAGE_FILE_MACHINE, hProcess, hThread, &scount, &ctxcount, NULL, NULL, NULL,
			NULL))
	{
		if (0 == scount.AddrReturn.Offset)
			break;
		nframes++;
	}

	while (TRUE == StackWalk64(TRX_IMAGE_FILE_MACHINE, hProcess, hThread, &s, &ctx, NULL, NULL, NULL, NULL))
	{
		frame_offset = 0;
		trx_snprintf_alloc(&frame, &frame_alloc, &frame_offset, "%d: %s", nframes--,
				NULL == process_name ? "(unknown)" : process_name);

		if (NULL != pSym)
		{
			DWORD		dwDisplacement;
			IMAGEHLP_LINE64	line = {sizeof(IMAGEHLP_LINE64)};

			trx_chrcpy_alloc(&frame, &frame_alloc, &frame_offset, '(');
			if (NULL != trx_SymFromAddr &&
					TRUE == trx_SymFromAddr(hProcess, s.AddrPC.Offset, &offset, pSym))
			{
				trx_snprintf_alloc(&frame, &frame_alloc, &frame_offset, "%s+0x%lx", pSym->Name, offset);
			}

			if (NULL != trx_SymGetLineFromAddrW64 && TRUE == trx_SymGetLineFromAddrW64(hProcess,
					s.AddrPC.Offset, &dwDisplacement, &line))
			{
				trx_snprintf_alloc(&frame, &frame_alloc, &frame_offset, " %s:%d", line.FileName,
						line.LineNumber);
			}
			trx_chrcpy_alloc(&frame, &frame_alloc, &frame_offset, ')');
		}

		treegix_log(LOG_LEVEL_CRIT, "%s [0x%lx]", frame, s.AddrPC.Offset);

		if (0 == s.AddrReturn.Offset)
			break;
	}

	SymCleanup(hProcess);

	trx_free(frame);
	trx_free(process_path);
	trx_free(process_name);
	trx_free(pSym);
}

int	trx_win_exception_filter(unsigned int code, struct _EXCEPTION_POINTERS *ep)
{
	treegix_log(LOG_LEVEL_CRIT, "Unhandled exception %x detected at 0x%p. Crashing ...", code,
			ep->ExceptionRecord->ExceptionAddress);

	print_fatal_info(ep->ContextRecord);
	print_backtrace(ep->ContextRecord);

	treegix_log(LOG_LEVEL_CRIT, "================================");

	return EXCEPTION_CONTINUE_SEARCH;
}
