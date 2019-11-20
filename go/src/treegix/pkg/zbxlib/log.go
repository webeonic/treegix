

package trxlib

/*
#cgo CFLAGS: -I${SRCDIR}/../../../../../include

#include "common.h"
#include "log.h"

int trx_log_level = LOG_LEVEL_WARNING;

int	trx_agent_pid;

void handleTreegixLog(int level, const char *message);

void __trx_treegix_log(int level, const char *format, ...)
{
	if (trx_agent_pid == getpid())
	{
		va_list	args;
		char *message = NULL;
		size_t size;

		va_start(args, format);
		size = vsnprintf(NULL, 0, format, args) + 2;
		va_end(args);
		message = (char *)trx_malloc(NULL, size);
		va_start(args, format);
		vsnprintf(message, size, format, args);
		va_end(args);

		handleTreegixLog(level, message);
		trx_free(message);
	}
}

#define TRX_MESSAGE_BUF_SIZE	1024

char	*trx_strerror(int errnum)
{
	static __thread char	utf8_string[TRX_MESSAGE_BUF_SIZE];
	trx_snprintf(utf8_string, sizeof(utf8_string), "[%d] %s", errnum, strerror(errnum));
	return utf8_string;
}

void	trx_handle_log(void)
{
	// rotation is handled by go logger backend
}

char	*strerror_from_system(unsigned long error)
{
	return trx_strerror(errno);
}

int	trx_redirect_stdio(const char *filename)
{
	// rotation is handled by go logger backend
	return FAIL;
}

*/
import "C"

func SetLogLevel(level int) {
	C.trx_log_level = C.int(level)
}

func init() {
	C.trx_agent_pid = C.getpid()
}
