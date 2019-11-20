

#ifndef TREEGIX_TYPES_H
#define TREEGIX_TYPES_H

#if defined(_WINDOWS)
#	define TRX_THREAD_LOCAL __declspec(thread)
#else
#	if defined(__GNUC__) || defined(__clang__)
#		define TRX_THREAD_LOCAL __thread
#	else
#		define TRX_THREAD_LOCAL
#	endif
#endif

#define	TRX_FS_DBL		"%lf"
#define	TRX_FS_DBL_EXT(p)	"%." #p "lf"

#define TRX_PTR_SIZE		sizeof(void *)

#if defined(_WINDOWS)
#	include <strsafe.h>

#	define trx_stat(path, buf)		__trx_stat(path, buf)
#	define trx_open(pathname, flags)	__trx_open(pathname, flags | O_BINARY)

#	ifndef __UINT64_C
#		define __UINT64_C(x)	x
#	endif

#	ifndef __INT64_C
#		define __INT64_C(x)	x
#	endif

#	define trx_uint64_t	unsigned __int64
#	define TRX_FS_UI64	"%I64u"
#	define TRX_FS_UO64	"%I64o"
#	define TRX_FS_UX64	"%I64x"

#	define trx_int64_t	__int64
#	define TRX_FS_I64	"%I64d"
#	define TRX_FS_O64	"%I64o"
#	define TRX_FS_X64	"%I64x"

#	define snprintf		_snprintf

#	define alloca		_alloca

#	ifndef uint32_t
typedef unsigned __int32	trx_uint32_t;
#	else
typedef uint32_t		trx_uint32_t;
#	endif

#	ifndef PATH_SEPARATOR
#		define PATH_SEPARATOR	'\\'
#	endif

#	define strcasecmp	lstrcmpiA

typedef __int64	trx_offset_t;
#	define trx_lseek(fd, offset, whence)	_lseeki64(fd, (trx_offset_t)(offset), whence)

#else	/* _WINDOWS */

#	define trx_stat(path, buf)		stat(path, buf)
#	define trx_open(pathname, flags)	open(pathname, flags)

#	ifndef __UINT64_C
#		ifdef UINT64_C
#			define __UINT64_C(c)	(UINT64_C(c))
#		else
#			define __UINT64_C(c)	(c ## ULL)
#		endif
#	endif

#	ifndef __INT64_C
#		ifdef INT64_C
#			define __INT64_C(c)	(INT64_C(c))
#		else
#			define __INT64_C(c)	(c ## LL)
#		endif
#	endif

#	define trx_uint64_t	uint64_t
#	if __WORDSIZE == 64
#		if defined(__APPLE__) && defined(__MACH__)	/* OS X */
#			define TRX_FS_UI64	"%llu"
#			define TRX_FS_UO64	"%llo"
#			define TRX_FS_UX64	"%llx"
#		else
#			define TRX_FS_UI64	"%lu"
#			define TRX_FS_UO64	"%lo"
#			define TRX_FS_UX64	"%lx"
#		endif
#	else
#		ifdef HAVE_LONG_LONG_QU
#			define TRX_FS_UI64	"%qu"
#			define TRX_FS_UO64	"%qo"
#			define TRX_FS_UX64	"%qx"
#		else
#			define TRX_FS_UI64	"%llu"
#			define TRX_FS_UO64	"%llo"
#			define TRX_FS_UX64	"%llx"
#		endif
#	endif

#	define trx_int64_t	int64_t
#	if __WORDSIZE == 64
#		if defined(__APPLE__) && defined(__MACH__)	/* OS X */
#			define TRX_FS_I64	"%lld"
#			define TRX_FS_O64	"%llo"
#			define TRX_FS_X64	"%llx"
#		else
#			define TRX_FS_I64	"%ld"
#			define TRX_FS_O64	"%lo"
#			define TRX_FS_X64	"%lx"
#		endif
#	else
#		ifdef HAVE_LONG_LONG_QU
#			define TRX_FS_I64	"%qd"
#			define TRX_FS_O64	"%qo"
#			define TRX_FS_X64	"%qx"
#		else
#			define TRX_FS_I64	"%lld"
#			define TRX_FS_O64	"%llo"
#			define TRX_FS_X64	"%llx"
#		endif
#	endif

typedef uint32_t	trx_uint32_t;

#	ifndef PATH_SEPARATOR
#		define PATH_SEPARATOR	'/'
#	endif

typedef off_t	trx_offset_t;
#	define trx_lseek(fd, offset, whence)	lseek(fd, (trx_offset_t)(offset), whence)

#endif	/* _WINDOWS */

#define TRX_FS_SIZE_T		TRX_FS_UI64
#define TRX_FS_SSIZE_T		TRX_FS_I64
#define TRX_FS_TIME_T		TRX_FS_I64
#define trx_fs_size_t		trx_uint64_t	/* use this type only in calls to printf() for formatting size_t */
#define trx_fs_ssize_t		trx_int64_t	/* use this type only in calls to printf() for formatting ssize_t */
#define trx_fs_time_t		trx_int64_t	/* use this type only in calls to printf() for formatting time_t */

#ifndef S_ISREG
#	define S_ISREG(x) (((x) & S_IFMT) == S_IFREG)
#endif

#ifndef S_ISDIR
#	define S_ISDIR(x) (((x) & S_IFMT) == S_IFDIR)
#endif

#define TRX_STR2UINT64(uint, string) is_uint64(string, &uint)
#define TRX_OCT2UINT64(uint, string) sscanf(string, TRX_FS_UO64, &uint)
#define TRX_HEX2UINT64(uint, string) sscanf(string, TRX_FS_UX64, &uint)

#define TRX_STR2UCHAR(var, string) var = (unsigned char)atoi(string)

#define TRX_CONST_STRING(str) "" str
#define TRX_CONST_STRLEN(str) (sizeof(TRX_CONST_STRING(str)) - 1)

typedef struct
{
	trx_uint64_t	lo;
	trx_uint64_t	hi;
}
trx_uint128_t;

#define TRX_SIZE_T_ALIGN8(size)	(((size) + 7) & ~(size_t)7)

/* macro to test if a signed value has been assigned to unsigned type (char, short, int, long long) */
#define TRX_IS_TOP_BIT_SET(x)	(0 != ((__UINT64_C(1) << ((sizeof(x) << 3) - 1)) & (x)))

#endif
