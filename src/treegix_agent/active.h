

#ifndef TREEGIX_ACTIVE_H
#define TREEGIX_ACTIVE_H

#include "threads.h"

extern char	*CONFIG_SOURCE_IP;
extern char	*CONFIG_HOSTNAME;
extern char	*CONFIG_HOST_METADATA;
extern char	*CONFIG_HOST_METADATA_ITEM;
extern char	*CONFIG_HOST_INTERFACE;
extern char	*CONFIG_HOST_INTERFACE_ITEM;
extern int	CONFIG_REFRESH_ACTIVE_CHECKS;
extern int	CONFIG_BUFFER_SEND;
extern int	CONFIG_BUFFER_SIZE;
extern int	CONFIG_MAX_LINES_PER_SECOND;
extern char	*CONFIG_LISTEN_IP;
extern int	CONFIG_LISTEN_PORT;

#define HOST_METADATA_LEN	255	/* UTF-8 characters, not bytes */
#define HOST_INTERFACE_LEN	255	/* UTF-8 characters, not bytes */

/* Windows event types for `eventlog' check */
#ifdef _WINDOWS
#	ifndef INFORMATION_TYPE
#		define INFORMATION_TYPE	"Information"
#	endif
#	ifndef WARNING_TYPE
#		define WARNING_TYPE	"Warning"
#	endif
#	ifndef ERROR_TYPE
#		define ERROR_TYPE	"Error"
#	endif
#	ifndef AUDIT_FAILURE
#		define AUDIT_FAILURE	"Failure Audit"
#	endif
#	ifndef AUDIT_SUCCESS
#		define AUDIT_SUCCESS	"Success Audit"
#	endif
#	ifndef CRITICAL_TYPE
#		define CRITICAL_TYPE	"Critical"
#	endif
#	ifndef VERBOSE_TYPE
#		define VERBOSE_TYPE	"Verbose"
#	endif
#endif	/* _WINDOWS */

typedef struct
{
	char		*host;
	unsigned short	port;
}
TRX_THREAD_ACTIVECHK_ARGS;

typedef struct
{
	char		*host;
	char		*key;
	char		*value;
	unsigned char	state;
	trx_uint64_t	lastlogsize;
	int		timestamp;
	char		*source;
	int		severity;
	trx_timespec_t	ts;
	int		logeventid;
	int		mtime;
	unsigned char	flags;
	trx_uint64_t	id;
}
TRX_ACTIVE_BUFFER_ELEMENT;

typedef struct
{
	TRX_ACTIVE_BUFFER_ELEMENT	*data;
	int				count;
	int				pcount;
	int				lastsent;
	int				first_error;
}
TRX_ACTIVE_BUFFER;

TRX_THREAD_ENTRY(active_checks_thread, args);

#endif	/* TREEGIX_ACTIVE_H */
