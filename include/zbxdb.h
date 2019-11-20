
#ifndef TREEGIX_TRXDB_H
#define TREEGIX_TRXDB_H

#include "common.h"

#define TRX_DB_OK	0
#define TRX_DB_FAIL	-1
#define TRX_DB_DOWN	-2

#define TRX_DB_WAIT_DOWN	10

#define TRX_MAX_SQL_SIZE	262144	/* 256KB */
#ifndef TRX_MAX_OVERFLOW_SQL_SIZE
#	ifdef HAVE_ORACLE
		/* Do not use "overflowing" (multi-statement) queries for Oracle. */
		/* Treegix benefits from cursor_sharing=force Oracle parameter */
		/* which doesn't apply to PL/SQL blocks. */
#		define TRX_MAX_OVERFLOW_SQL_SIZE	0
#	else
#		define TRX_MAX_OVERFLOW_SQL_SIZE	TRX_MAX_SQL_SIZE
#	endif
#elif 0 != TRX_MAX_OVERFLOW_SQL_SIZE && \
		(1024 > TRX_MAX_OVERFLOW_SQL_SIZE || TRX_MAX_OVERFLOW_SQL_SIZE > TRX_MAX_SQL_SIZE)
#error TRX_MAX_OVERFLOW_SQL_SIZE is out of range
#endif

typedef char	**DB_ROW;
typedef struct trx_db_result	*DB_RESULT;

/* database field value */
typedef union
{
	int		i32;
	trx_uint64_t	ui64;
	double		dbl;
	char		*str;
}
trx_db_value_t;

#ifdef HAVE_SQLITE3
	/* we have to put double % here for sprintf */
#	define TRX_SQL_MOD(x, y) #x "%%" #y
#else
#	define TRX_SQL_MOD(x, y) "mod(" #x "," #y ")"
#endif

#ifdef HAVE_SQLITE3
#	define TRX_FOR_UPDATE	""	/* SQLite3 does not support "select ... for update" */
#else
#	define TRX_FOR_UPDATE	" for update"
#endif

#ifdef HAVE_MULTIROW_INSERT
#	define TRX_ROW_DL	","
#else
#	define TRX_ROW_DL	";\n"
#endif

int	trx_db_init(const char *dbname, const char *const db_schema, char **error);
void	trx_db_deinit(void);

int	trx_db_connect(char *host, char *user, char *password, char *dbname, char *dbschema, char *dbsocket, int port);
void	trx_db_close(void);

int	trx_db_begin(void);
int	trx_db_commit(void);
int	trx_db_rollback(void);
int	trx_db_txn_level(void);
int	trx_db_txn_error(void);
int	trx_db_txn_end_error(void);
const char	*trx_db_last_strerr(void);

#ifdef HAVE_ORACLE

/* context for dynamic parameter binding */
typedef struct
{
	/* the parameter position, starting with 0 */
	int			position;
	/* the parameter type (TRX_TYPE_* ) */
	unsigned char		type;
	/* the maximum parameter size */
	size_t			size_max;
	/* the data to bind - array of rows, each row being an array of columns */
	trx_db_value_t		**rows;
	/* custom data, depending on column type */
	void			*data;
}
trx_db_bind_context_t;

int		trx_db_statement_prepare(const char *sql);
int		trx_db_bind_parameter_dyn(trx_db_bind_context_t *context, int position, unsigned char type,
				trx_db_value_t **rows, int rows_num);
void		trx_db_clean_bind_context(trx_db_bind_context_t *context);
int		trx_db_statement_execute(int iters);
#endif
int		trx_db_vexecute(const char *fmt, va_list args);
DB_RESULT	trx_db_vselect(const char *fmt, va_list args);
DB_RESULT	trx_db_select_n(const char *query, int n);

DB_ROW		trx_db_fetch(DB_RESULT result);
void		DBfree_result(DB_RESULT result);
int		trx_db_is_null(const char *field);

typedef enum
{
	ESCAPE_SEQUENCE_OFF,
	ESCAPE_SEQUENCE_ON
}
trx_escape_sequence_t;
char		*trx_db_dyn_escape_string(const char *src, size_t max_bytes, size_t max_chars,
		trx_escape_sequence_t flag);
#define TRX_SQL_LIKE_ESCAPE_CHAR '!'
char		*trx_db_dyn_escape_like_pattern(const char *src);

int		trx_db_strlen_n(const char *text, size_t maxlen);

#endif
