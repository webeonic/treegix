
#ifndef TREEGIX_MODULE_H
#define TREEGIX_MODULE_H

#include "trxtypes.h"

#define TRX_MODULE_OK	0
#define TRX_MODULE_FAIL	-1

/* trx_module_api_version() MUST return this constant */
#define TRX_MODULE_API_VERSION	2

/* old name alias is kept for source compatibility only, SHOULD NOT be used */
#define TRX_MODULE_API_VERSION_ONE	TRX_MODULE_API_VERSION

/* HINT: For conditional compilation with different module.h versions modules can use: */
/* #if TRX_MODULE_API_VERSION == X                                                     */
/*         ...                                                                         */
/* #endif                                                                              */

#define get_rkey(request)		(request)->key
#define get_rparams_num(request)	(request)->nparam
#define get_rparam(request, num)	((request)->nparam > num ? (request)->params[num] : NULL)

/* flags for command */
#define CF_HAVEPARAMS		0x01	/* item accepts either optional or mandatory parameters */
#define CF_MODULE		0x02	/* item is defined in a loadable module */
#define CF_USERPARAMETER	0x04	/* item is defined as user parameter */

/* agent request structure */
typedef struct
{
	char		*key;
	int		nparam;
	char		**params;
	trx_uint64_t	lastlogsize;
	int		mtime;
}
AGENT_REQUEST;

typedef struct
{
	char		*value;
	char		*source;
	int		timestamp;
	int		severity;
	int		logeventid;
}
trx_log_t;

/* agent result types */
#define AR_UINT64	0x01
#define AR_DOUBLE	0x02
#define AR_STRING	0x04
#define AR_TEXT		0x08
#define AR_LOG		0x10
#define AR_MESSAGE	0x20
#define AR_META		0x40

/* agent return structure */
typedef struct
{
	trx_uint64_t	lastlogsize;	/* meta information */
	trx_uint64_t	ui64;
	double		dbl;
	char		*str;
	char		*text;
	char		*msg;		/* possible error message */
	trx_log_t	*log;
	int	 	type;		/* flags: see AR_* above */
	int		mtime;		/* meta information */
}
AGENT_RESULT;

typedef struct
{
	char		*key;
	unsigned	flags;
	int		(*function)(AGENT_REQUEST *request, AGENT_RESULT *result);
	char		*test_param;	/* item test parameters; user parameter items keep command here */
}
TRX_METRIC;

/* SET RESULT */

#define SET_UI64_RESULT(res, val)		\
(						\
	(res)->type |= AR_UINT64,		\
	(res)->ui64 = (trx_uint64_t)(val)	\
)

#define SET_DBL_RESULT(res, val)		\
(						\
	(res)->type |= AR_DOUBLE,		\
	(res)->dbl = (double)(val)		\
)

/* NOTE: always allocate new memory for val! DON'T USE STATIC OR STACK MEMORY!!! */
#define SET_STR_RESULT(res, val)		\
(						\
	(res)->type |= AR_STRING,		\
	(res)->str = (char *)(val)		\
)

/* NOTE: always allocate new memory for val! DON'T USE STATIC OR STACK MEMORY!!! */
#define SET_TEXT_RESULT(res, val)		\
(						\
	(res)->type |= AR_TEXT,			\
	(res)->text = (char *)(val)		\
)

/* NOTE: always allocate new memory for val! DON'T USE STATIC OR STACK MEMORY!!! */
#define SET_LOG_RESULT(res, val)		\
(						\
	(res)->type |= AR_LOG,			\
	(res)->log = (trx_log_t *)(val)		\
)

/* NOTE: always allocate new memory for val! DON'T USE STATIC OR STACK MEMORY!!! */
#define SET_MSG_RESULT(res, val)		\
(						\
	(res)->type |= AR_MESSAGE,		\
	(res)->msg = (char *)(val)		\
)

#define SYSINFO_RET_OK		0
#define SYSINFO_RET_FAIL	1

typedef struct
{
	trx_uint64_t	itemid;
	int		clock;
	int		ns;
	double		value;
}
TRX_HISTORY_FLOAT;

typedef struct
{
	trx_uint64_t	itemid;
	int		clock;
	int		ns;
	trx_uint64_t	value;
}
TRX_HISTORY_INTEGER;

typedef struct
{
	trx_uint64_t	itemid;
	int		clock;
	int		ns;
	const char	*value;
}
TRX_HISTORY_STRING;

typedef struct
{
	trx_uint64_t	itemid;
	int		clock;
	int		ns;
	const char	*value;
}
TRX_HISTORY_TEXT;

typedef struct
{
	trx_uint64_t	itemid;
	int		clock;
	int		ns;
	const char	*value;
	const char	*source;
	int		timestamp;
	int		logeventid;
	int		severity;
}
TRX_HISTORY_LOG;

typedef struct
{
	void	(*history_float_cb)(const TRX_HISTORY_FLOAT *history, int history_num);
	void	(*history_integer_cb)(const TRX_HISTORY_INTEGER *history, int history_num);
	void	(*history_string_cb)(const TRX_HISTORY_STRING *history, int history_num);
	void	(*history_text_cb)(const TRX_HISTORY_TEXT *history, int history_num);
	void	(*history_log_cb)(const TRX_HISTORY_LOG *history, int history_num);
}
TRX_HISTORY_WRITE_CBS;

int	trx_module_api_version(void);
int	trx_module_init(void);
int	trx_module_uninit(void);
void	trx_module_item_timeout(int timeout);
TRX_METRIC	*trx_module_item_list(void);
TRX_HISTORY_WRITE_CBS	trx_module_history_write_cbs(void);

#endif
