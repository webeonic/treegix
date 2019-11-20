
#ifndef TREEGIX_LOG_H
#define TREEGIX_LOG_H

#include "common.h"

#define LOG_LEVEL_EMPTY		0	/* printing nothing (if not LOG_LEVEL_INFORMATION set) */
#define LOG_LEVEL_CRIT		1
#define LOG_LEVEL_ERR		2
#define LOG_LEVEL_WARNING	3
#define LOG_LEVEL_DEBUG		4
#define LOG_LEVEL_TRACE		5

#define LOG_LEVEL_INFORMATION	127	/* printing in any case no matter what level set */

#define LOG_TYPE_UNDEFINED	0
#define LOG_TYPE_SYSTEM		1
#define LOG_TYPE_FILE		2
#define LOG_TYPE_CONSOLE	3

#define TRX_OPTION_LOGTYPE_SYSTEM	"system"
#define TRX_OPTION_LOGTYPE_FILE		"file"
#define TRX_OPTION_LOGTYPE_CONSOLE	"console"

#define LOG_ENTRY_INTERVAL_DELAY	60	/* seconds */

extern int	trx_log_level;
#define TRX_CHECK_LOG_LEVEL(level)			\
		((LOG_LEVEL_INFORMATION != (level) &&	\
		((level) > trx_log_level || LOG_LEVEL_EMPTY == (level))) ? FAIL : SUCCEED)

typedef enum
{
	ERR_Z3001 = 3001,
	ERR_Z3002,
	ERR_Z3003,
	ERR_Z3004,
	ERR_Z3005,
	ERR_Z3006,
	ERR_Z3007
}
trx_err_codes_t;

#ifdef HAVE___VA_ARGS__
#	define TRX_TREEGIX_LOG_CHECK
#	define treegix_log(level, ...)									\
													\
	do												\
	{												\
		if (SUCCEED == TRX_CHECK_LOG_LEVEL(level))						\
			__trx_treegix_log(level, __VA_ARGS__);						\
	}												\
	while (0)
#else
#	define treegix_log __trx_treegix_log
#endif

int		treegix_open_log(int type, int level, const char *filename, char **error);
void		__trx_treegix_log(int level, const char *fmt, ...) __trx_attr_format_printf(2, 3);
void		treegix_close_log(void);

#ifndef _WINDOWS
int		treegix_increase_log_level(void);
int		treegix_decrease_log_level(void);
const char	*treegix_get_log_level_string(void);
#endif

char		*trx_strerror(int errnum);
char		*strerror_from_system(unsigned long error);

#ifdef _WINDOWS
char		*strerror_from_module(unsigned long error, const wchar_t *module);
#endif

int		trx_redirect_stdio(const char *filename);

void		trx_handle_log(void);

int		trx_get_log_type(const char *logtype);
int		trx_validate_log_parameters(TRX_TASK_EX *task);

#endif
