

#include "common.h"
#include "log.h"
#include "mutexs.h"
#include "threads.h"
#include "cfg.h"
#ifdef _WINDOWS
#	include "messages.h"
#	include "service.h"
#	include "sysinfo.h"
static HANDLE		system_log_handle = INVALID_HANDLE_VALUE;
#endif

static char		log_filename[MAX_STRING_LEN];
static int		log_type = LOG_TYPE_UNDEFINED;
static trx_mutex_t	log_access = TRX_MUTEX_NULL;
int			trx_log_level = LOG_LEVEL_WARNING;

#ifdef _WINDOWS
#	define LOCK_LOG		trx_mutex_lock(log_access)
#	define UNLOCK_LOG	trx_mutex_unlock(log_access)
#else
#	define LOCK_LOG		lock_log()
#	define UNLOCK_LOG	unlock_log()
#endif

#define TRX_MESSAGE_BUF_SIZE	1024

#ifdef _WINDOWS
#	define STDIN_FILENO	_fileno(stdin)
#	define STDOUT_FILENO	_fileno(stdout)
#	define STDERR_FILENO	_fileno(stderr)

#	define TRX_DEV_NULL	"NUL"

#	define dup2(fd1, fd2)	_dup2(fd1, fd2)
#else
#	define TRX_DEV_NULL	"/dev/null"
#endif

#ifndef _WINDOWS
const char	*treegix_get_log_level_string(void)
{
	switch (trx_log_level)
	{
		case LOG_LEVEL_EMPTY:
			return "0 (none)";
		case LOG_LEVEL_CRIT:
			return "1 (critical)";
		case LOG_LEVEL_ERR:
			return "2 (error)";
		case LOG_LEVEL_WARNING:
			return "3 (warning)";
		case LOG_LEVEL_DEBUG:
			return "4 (debug)";
		case LOG_LEVEL_TRACE:
			return "5 (trace)";
	}

	THIS_SHOULD_NEVER_HAPPEN;
	exit(EXIT_FAILURE);
}

int	treegix_increase_log_level(void)
{
	if (LOG_LEVEL_TRACE == trx_log_level)
		return FAIL;

	trx_log_level = trx_log_level + 1;

	return SUCCEED;
}

int	treegix_decrease_log_level(void)
{
	if (LOG_LEVEL_EMPTY == trx_log_level)
		return FAIL;

	trx_log_level = trx_log_level - 1;

	return SUCCEED;
}
#endif

int	trx_redirect_stdio(const char *filename)
{
	const char	default_file[] = TRX_DEV_NULL;
	int		open_flags = O_WRONLY, fd;

	if (NULL != filename && '\0' != *filename)
		open_flags |= O_CREAT | O_APPEND;
	else
		filename = default_file;

	if (-1 == (fd = open(filename, open_flags, 0666)))
	{
		trx_error("cannot open \"%s\": %s", filename, trx_strerror(errno));
		return FAIL;
	}

	fflush(stdout);
	if (-1 == dup2(fd, STDOUT_FILENO))
		trx_error("cannot redirect stdout to \"%s\": %s", filename, trx_strerror(errno));

	fflush(stderr);
	if (-1 == dup2(fd, STDERR_FILENO))
		trx_error("cannot redirect stderr to \"%s\": %s", filename, trx_strerror(errno));

	close(fd);

	if (-1 == (fd = open(default_file, O_RDONLY)))
	{
		trx_error("cannot open \"%s\": %s", default_file, trx_strerror(errno));
		return FAIL;
	}

	if (-1 == dup2(fd, STDIN_FILENO))
		trx_error("cannot redirect stdin to \"%s\": %s", default_file, trx_strerror(errno));

	close(fd);

	return SUCCEED;
}

static void	rotate_log(const char *filename)
{
	trx_stat_t		buf;
	trx_uint64_t		new_size;
	static trx_uint64_t	old_size = TRX_MAX_UINT64; /* redirect stdout and stderr */

	if (0 != trx_stat(filename, &buf))
	{
		trx_redirect_stdio(filename);
		return;
	}

	new_size = buf.st_size;

	if (0 != CONFIG_LOG_FILE_SIZE && (trx_uint64_t)CONFIG_LOG_FILE_SIZE * TRX_MEBIBYTE < new_size)
	{
		char	filename_old[MAX_STRING_LEN];

		strscpy(filename_old, filename);
		trx_strlcat(filename_old, ".old", MAX_STRING_LEN);
		remove(filename_old);

		if (0 != rename(filename, filename_old))
		{
			FILE	*log_file = NULL;

			if (NULL != (log_file = fopen(filename, "w")))
			{
				long		milliseconds;
				struct tm	tm;

				trx_get_time(&tm, &milliseconds, NULL);

				fprintf(log_file, "%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld"
						" cannot rename log file \"%s\" to \"%s\": %s\n",
						trx_get_thread_id(),
						tm.tm_year + 1900,
						tm.tm_mon + 1,
						tm.tm_mday,
						tm.tm_hour,
						tm.tm_min,
						tm.tm_sec,
						milliseconds,
						filename,
						filename_old,
						trx_strerror(errno));

				fprintf(log_file, "%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld"
						" Logfile \"%s\" size reached configured limit"
						" LogFileSize but moving it to \"%s\" failed. The logfile"
						" was truncated.\n",
						trx_get_thread_id(),
						tm.tm_year + 1900,
						tm.tm_mon + 1,
						tm.tm_mday,
						tm.tm_hour,
						tm.tm_min,
						tm.tm_sec,
						milliseconds,
						filename,
						filename_old);

				trx_fclose(log_file);

				new_size = 0;
			}
		}
		else
			new_size = 0;
	}

	if (old_size > new_size)
		trx_redirect_stdio(filename);

	old_size = new_size;
}

#ifndef _WINDOWS
static sigset_t	orig_mask;

static void	lock_log(void)
{
	sigset_t	mask;

	/* block signals to prevent deadlock on log file mutex when signal handler attempts to lock log */
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGUSR2);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	sigaddset(&mask, SIGHUP);

	if (0 > sigprocmask(SIG_BLOCK, &mask, &orig_mask))
		trx_error("cannot set sigprocmask to block the user signal");

	trx_mutex_lock(log_access);
}

static void	unlock_log(void)
{
	trx_mutex_unlock(log_access);

	if (0 > sigprocmask(SIG_SETMASK, &orig_mask, NULL))
		trx_error("cannot restore sigprocmask");
}
#else
static void	lock_log(void)
{
#ifdef TREEGIX_AGENT
	if (0 == (TRX_MUTEX_LOGGING_DENIED & get_thread_global_mutex_flag()))
#endif
		LOCK_LOG;
}

static void	unlock_log(void)
{
#ifdef TREEGIX_AGENT
	if (0 == (TRX_MUTEX_LOGGING_DENIED & get_thread_global_mutex_flag()))
#endif
		UNLOCK_LOG;
}
#endif

void	trx_handle_log(void)
{
	if (LOG_TYPE_FILE != log_type)
		return;

	LOCK_LOG;

	rotate_log(log_filename);

	UNLOCK_LOG;
}

int	treegix_open_log(int type, int level, const char *filename, char **error)
{
	log_type = type;
	trx_log_level = level;

	if (LOG_TYPE_SYSTEM == type)
	{
#ifdef _WINDOWS
		wchar_t	*wevent_source;

		wevent_source = trx_utf8_to_unicode(TREEGIX_EVENT_SOURCE);
		system_log_handle = RegisterEventSource(NULL, wevent_source);
		trx_free(wevent_source);
#else
		openlog(syslog_app_name, LOG_PID, LOG_DAEMON);
#endif
	}
	else if (LOG_TYPE_FILE == type)
	{
		FILE	*log_file = NULL;

		if (MAX_STRING_LEN <= strlen(filename))
		{
			*error = trx_strdup(*error, "too long path for logfile");
			return FAIL;
		}

		if (SUCCEED != trx_mutex_create(&log_access, TRX_MUTEX_LOG, error))
			return FAIL;

		if (NULL == (log_file = fopen(filename, "a+")))
		{
			*error = trx_dsprintf(*error, "unable to open log file [%s]: %s", filename, trx_strerror(errno));
			return FAIL;
		}

		strscpy(log_filename, filename);
		trx_fclose(log_file);
	}
	else if (LOG_TYPE_CONSOLE == type || LOG_TYPE_UNDEFINED == type)
	{
		if (SUCCEED != trx_mutex_create(&log_access, TRX_MUTEX_LOG, error))
		{
			*error = trx_strdup(*error, "unable to create mutex for standard output");
			return FAIL;
		}

		fflush(stderr);
		if (-1 == dup2(STDOUT_FILENO, STDERR_FILENO))
			trx_error("cannot redirect stderr to stdout: %s", trx_strerror(errno));
	}
	else
	{
		*error = trx_strdup(*error, "unknown log type");
		return FAIL;
	}

	return SUCCEED;
}

void	treegix_close_log(void)
{
	if (LOG_TYPE_SYSTEM == log_type)
	{
#ifdef _WINDOWS
		if (NULL != system_log_handle)
			DeregisterEventSource(system_log_handle);
#else
		closelog();
#endif
	}
	else if (LOG_TYPE_FILE == log_type || LOG_TYPE_CONSOLE == log_type || LOG_TYPE_UNDEFINED == log_type)
	{
		trx_mutex_destroy(&log_access);
	}
}

void	__trx_treegix_log(int level, const char *fmt, ...)
{
	char		message[MAX_BUFFER_LEN];
	va_list		args;
#ifdef _WINDOWS
	WORD		wType;
	wchar_t		thread_id[20], *strings[2];
#endif

#ifndef TRX_TREEGIX_LOG_CHECK
	if (SUCCEED != TRX_CHECK_LOG_LEVEL(level))
		return;
#endif
	if (LOG_TYPE_FILE == log_type)
	{
		FILE	*log_file;

		LOCK_LOG;

		if (0 != CONFIG_LOG_FILE_SIZE)
			rotate_log(log_filename);

		if (NULL != (log_file = fopen(log_filename, "a+")))
		{
			long		milliseconds;
			struct tm	tm;

			trx_get_time(&tm, &milliseconds, NULL);

			fprintf(log_file,
					"%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld ",
					trx_get_thread_id(),
					tm.tm_year + 1900,
					tm.tm_mon + 1,
					tm.tm_mday,
					tm.tm_hour,
					tm.tm_min,
					tm.tm_sec,
					milliseconds
					);

			va_start(args, fmt);
			vfprintf(log_file, fmt, args);
			va_end(args);

			fprintf(log_file, "\n");

			trx_fclose(log_file);
		}
		else
		{
			trx_error("failed to open log file: %s", trx_strerror(errno));

			va_start(args, fmt);
			trx_vsnprintf(message, sizeof(message), fmt, args);
			va_end(args);

			trx_error("failed to write [%s] into log file", message);
		}

		UNLOCK_LOG;

		return;
	}

	if (LOG_TYPE_CONSOLE == log_type)
	{
		long		milliseconds;
		struct tm	tm;

		LOCK_LOG;

		trx_get_time(&tm, &milliseconds, NULL);

		fprintf(stdout,
				"%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld ",
				trx_get_thread_id(),
				tm.tm_year + 1900,
				tm.tm_mon + 1,
				tm.tm_mday,
				tm.tm_hour,
				tm.tm_min,
				tm.tm_sec,
				milliseconds
				);

		va_start(args, fmt);
		vfprintf(stdout, fmt, args);
		va_end(args);

		fprintf(stdout, "\n");

		fflush(stdout);

		UNLOCK_LOG;

		return;
	}

	va_start(args, fmt);
	trx_vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);

	if (LOG_TYPE_SYSTEM == log_type)
	{
#ifdef _WINDOWS
		switch (level)
		{
			case LOG_LEVEL_CRIT:
			case LOG_LEVEL_ERR:
				wType = EVENTLOG_ERROR_TYPE;
				break;
			case LOG_LEVEL_WARNING:
				wType = EVENTLOG_WARNING_TYPE;
				break;
			default:
				wType = EVENTLOG_INFORMATION_TYPE;
				break;
		}

		StringCchPrintf(thread_id, ARRSIZE(thread_id), TEXT("[%li]: "), trx_get_thread_id());
		strings[0] = thread_id;
		strings[1] = trx_utf8_to_unicode(message);

		ReportEvent(
			system_log_handle,
			wType,
			0,
			MSG_TREEGIX_MESSAGE,
			NULL,
			sizeof(strings) / sizeof(*strings),
			0,
			strings,
			NULL);

		trx_free(strings[1]);

#else	/* not _WINDOWS */

		/* for nice printing into syslog */
		switch (level)
		{
			case LOG_LEVEL_CRIT:
				syslog(LOG_CRIT, "%s", message);
				break;
			case LOG_LEVEL_ERR:
				syslog(LOG_ERR, "%s", message);
				break;
			case LOG_LEVEL_WARNING:
				syslog(LOG_WARNING, "%s", message);
				break;
			case LOG_LEVEL_DEBUG:
			case LOG_LEVEL_TRACE:
				syslog(LOG_DEBUG, "%s", message);
				break;
			case LOG_LEVEL_INFORMATION:
				syslog(LOG_INFO, "%s", message);
				break;
			default:
				/* LOG_LEVEL_EMPTY - print nothing */
				break;
		}

#endif	/* _WINDOWS */
	}	/* LOG_TYPE_SYSLOG */
	else	/* LOG_TYPE_UNDEFINED == log_type */
	{
		LOCK_LOG;

		switch (level)
		{
			case LOG_LEVEL_CRIT:
				trx_error("ERROR: %s", message);
				break;
			case LOG_LEVEL_ERR:
				trx_error("Error: %s", message);
				break;
			case LOG_LEVEL_WARNING:
				trx_error("Warning: %s", message);
				break;
			case LOG_LEVEL_DEBUG:
				trx_error("DEBUG: %s", message);
				break;
			case LOG_LEVEL_TRACE:
				trx_error("TRACE: %s", message);
				break;
			default:
				trx_error("%s", message);
				break;
		}

		UNLOCK_LOG;
	}
}

int	trx_get_log_type(const char *logtype)
{
	const char	*logtypes[] = {TRX_OPTION_LOGTYPE_SYSTEM, TRX_OPTION_LOGTYPE_FILE, TRX_OPTION_LOGTYPE_CONSOLE};
	int		i;

	for (i = 0; i < (int)ARRSIZE(logtypes); i++)
	{
		if (0 == strcmp(logtype, logtypes[i]))
			return i + 1;
	}

	return LOG_TYPE_UNDEFINED;
}

int	trx_validate_log_parameters(TRX_TASK_EX *task)
{
	if (LOG_TYPE_UNDEFINED == CONFIG_LOG_TYPE)
	{
		treegix_log(LOG_LEVEL_CRIT, "invalid \"LogType\" configuration parameter: '%s'", CONFIG_LOG_TYPE_STR);
		return FAIL;
	}

	if (LOG_TYPE_CONSOLE == CONFIG_LOG_TYPE && 0 == (task->flags & TRX_TASK_FLAG_FOREGROUND) &&
			TRX_TASK_START == task->task)
	{
		treegix_log(LOG_LEVEL_CRIT, "\"LogType\" \"console\" parameter can only be used with the"
				" -f (--foreground) command line option");
		return FAIL;
	}

	if (LOG_TYPE_FILE == CONFIG_LOG_TYPE && (NULL == CONFIG_LOG_FILE || '\0' == *CONFIG_LOG_FILE))
	{
		treegix_log(LOG_LEVEL_CRIT, "\"LogType\" \"file\" parameter requires \"LogFile\" parameter to be set");
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Comments: replace strerror to print also the error number                  *
 *                                                                            *
 ******************************************************************************/
char	*trx_strerror(int errnum)
{
	/* !!! Attention: static !!! Not thread-safe for Win32 */
	static char	utf8_string[TRX_MESSAGE_BUF_SIZE];

	trx_snprintf(utf8_string, sizeof(utf8_string), "[%d] %s", errnum, strerror(errnum));

	return utf8_string;
}

char	*strerror_from_system(unsigned long error)
{
#ifdef _WINDOWS
	size_t		offset = 0;
	wchar_t		wide_string[TRX_MESSAGE_BUF_SIZE];
	/* !!! Attention: static !!! Not thread-safe for Win32 */
	static char	utf8_string[TRX_MESSAGE_BUF_SIZE];

	offset += trx_snprintf(utf8_string, sizeof(utf8_string), "[0x%08lX] ", error);

	/* we don't know the inserts so we pass NULL and enable appropriate flag */
	if (0 == FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), wide_string, TRX_MESSAGE_BUF_SIZE, NULL))
	{
		trx_snprintf(utf8_string + offset, sizeof(utf8_string) - offset,
				"unable to find message text [0x%08lX]", GetLastError());

		return utf8_string;
	}

	trx_unicode_to_utf8_static(wide_string, utf8_string + offset, (int)(sizeof(utf8_string) - offset));

	trx_rtrim(utf8_string, "\r\n ");

	return utf8_string;
#else	/* not _WINDOWS */
	TRX_UNUSED(error);

	return trx_strerror(errno);
#endif	/* _WINDOWS */
}

#ifdef _WINDOWS
char	*strerror_from_module(unsigned long error, const wchar_t *module)
{
	size_t		offset = 0;
	wchar_t		wide_string[TRX_MESSAGE_BUF_SIZE];
	HMODULE		hmodule;
	/* !!! Attention: static !!! not thread-safe for Win32 */
	static char	utf8_string[TRX_MESSAGE_BUF_SIZE];

	*utf8_string = '\0';
	hmodule = GetModuleHandle(module);

	offset += trx_snprintf(utf8_string, sizeof(utf8_string), "[0x%08lX] ", error);

	/* we don't know the inserts so we pass NULL and enable appropriate flag */
	if (0 == FormatMessage(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, hmodule, error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), wide_string, sizeof(wide_string), NULL))
	{
		trx_snprintf(utf8_string + offset, sizeof(utf8_string) - offset,
				"unable to find message text: %s", strerror_from_system(GetLastError()));

		return utf8_string;
	}

	trx_unicode_to_utf8_static(wide_string, utf8_string + offset, (int)(sizeof(utf8_string) - offset));

	trx_rtrim(utf8_string, "\r\n ");

	return utf8_string;
}
#endif	/* _WINDOWS */
