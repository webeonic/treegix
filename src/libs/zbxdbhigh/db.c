

#include "db.h"
#include "log.h"
#include "common.h"
#include "events.h"
#include "threads.h"
#include "trxserver.h"
#include "dbcache.h"
#include "trxalgo.h"

typedef struct
{
	trx_uint64_t	autoreg_hostid;
	trx_uint64_t	hostid;
	char		*host;
	char		*ip;
	char		*dns;
	char		*host_metadata;
	int		now;
	unsigned short	port;
	unsigned short	flag;
	unsigned int	connection_type;
}
trx_autoreg_host_t;

#if HAVE_POSTGRESQL
extern char	TRX_PG_ESCAPE_BACKSLASH;
#endif

static int	connection_failure;

void	DBclose(void)
{
	trx_db_close();
}

/******************************************************************************
 *                                                                            *
 * Function: DBconnect                                                        *
 *                                                                            *
 * Purpose: connect to the database                                           *
 *                                                                            *
 * Parameters: flag - TRX_DB_CONNECT_ONCE (try once and return the result),   *
 *                    TRX_DB_CONNECT_EXIT (exit on failure) or                *
 *                    TRX_DB_CONNECT_NORMAL (retry until connected)           *
 *                                                                            *
 * Return value: same as trx_db_connect()                                     *
 *                                                                            *
 ******************************************************************************/
int	DBconnect(int flag)
{
	int	err;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() flag:%d", __func__, flag);

	while (TRX_DB_OK != (err = trx_db_connect(CONFIG_DBHOST, CONFIG_DBUSER, CONFIG_DBPASSWORD,
			CONFIG_DBNAME, CONFIG_DBSCHEMA, CONFIG_DBSOCKET, CONFIG_DBPORT)))
	{
		if (TRX_DB_CONNECT_ONCE == flag)
			break;

		if (TRX_DB_FAIL == err || TRX_DB_CONNECT_EXIT == flag)
		{
			treegix_log(LOG_LEVEL_CRIT, "Cannot connect to the database. Exiting...");
			exit(EXIT_FAILURE);
		}

		treegix_log(LOG_LEVEL_ERR, "database is down: reconnecting in %d seconds", TRX_DB_WAIT_DOWN);
		connection_failure = 1;
		trx_sleep(TRX_DB_WAIT_DOWN);
	}

	if (0 != connection_failure)
	{
		treegix_log(LOG_LEVEL_ERR, "database connection re-established");
		connection_failure = 0;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, err);

	return err;
}

/******************************************************************************
 *                                                                            *
 * Function: DBinit                                                           *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
int	DBinit(char **error)
{
	return trx_db_init(CONFIG_DBNAME, db_schema, error);
}

void	DBdeinit(void)
{
	trx_db_deinit();
}

/******************************************************************************
 *                                                                            *
 * Function: DBtxn_operation                                                  *
 *                                                                            *
 * Purpose: helper function to loop transaction operation while DB is down    *
 *                                                                            *
 * Author: Eugene Grigorjev, Vladimir Levijev                                 *
 *                                                                            *
 ******************************************************************************/
static void	DBtxn_operation(int (*txn_operation)(void))
{
	int	rc;

	rc = txn_operation();

	while (TRX_DB_DOWN == rc)
	{
		DBclose();
		DBconnect(TRX_DB_CONNECT_NORMAL);

		if (TRX_DB_DOWN == (rc = txn_operation()))
		{
			treegix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", TRX_DB_WAIT_DOWN);
			connection_failure = 1;
			sleep(TRX_DB_WAIT_DOWN);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: DBbegin                                                          *
 *                                                                            *
 * Purpose: start a transaction                                               *
 *                                                                            *
 * Author: Eugene Grigorjev, Vladimir Levijev                                 *
 *                                                                            *
 * Comments: do nothing if DB does not support transactions                   *
 *                                                                            *
 ******************************************************************************/
void	DBbegin(void)
{
	DBtxn_operation(trx_db_begin);
}

/******************************************************************************
 *                                                                            *
 * Function: DBcommit                                                         *
 *                                                                            *
 * Purpose: commit a transaction                                              *
 *                                                                            *
 * Author: Eugene Grigorjev, Vladimir Levijev                                 *
 *                                                                            *
 * Comments: do nothing if DB does not support transactions                   *
 *                                                                            *
 ******************************************************************************/
int	DBcommit(void)
{
	if (TRX_DB_OK > trx_db_commit())
	{
		treegix_log(LOG_LEVEL_DEBUG, "commit called on failed transaction, doing a rollback instead");
		DBrollback();
	}

	return trx_db_txn_end_error();
}

/******************************************************************************
 *                                                                            *
 * Function: DBrollback                                                       *
 *                                                                            *
 * Purpose: rollback a transaction                                            *
 *                                                                            *
 * Author: Eugene Grigorjev, Vladimir Levijev                                 *
 *                                                                            *
 * Comments: do nothing if DB does not support transactions                   *
 *                                                                            *
 ******************************************************************************/
void	DBrollback(void)
{
	if (TRX_DB_OK > trx_db_rollback())
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot perform transaction rollback, connection will be reset");

		DBclose();
		DBconnect(TRX_DB_CONNECT_NORMAL);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: DBend                                                            *
 *                                                                            *
 * Purpose: commit or rollback a transaction depending on a parameter value   *
 *                                                                            *
 * Comments: do nothing if DB does not support transactions                   *
 *                                                                            *
 ******************************************************************************/
int	DBend(int ret)
{
	if (SUCCEED == ret)
		return TRX_DB_OK == DBcommit() ? SUCCEED : FAIL;

	DBrollback();

	return FAIL;
}

#ifdef HAVE_ORACLE
/******************************************************************************
 *                                                                            *
 * Function: DBstatement_prepare                                              *
 *                                                                            *
 * Purpose: prepares a SQL statement for execution                            *
 *                                                                            *
 * Comments: retry until DB is up                                             *
 *                                                                            *
 ******************************************************************************/
void	DBstatement_prepare(const char *sql)
{
	int	rc;

	rc = trx_db_statement_prepare(sql);

	while (TRX_DB_DOWN == rc)
	{
		DBclose();
		DBconnect(TRX_DB_CONNECT_NORMAL);

		if (TRX_DB_DOWN == (rc = trx_db_statement_prepare(sql)))
		{
			treegix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", TRX_DB_WAIT_DOWN);
			connection_failure = 1;
			sleep(TRX_DB_WAIT_DOWN);
		}
	}
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: __trx_DBexecute                                                  *
 *                                                                            *
 * Purpose: execute a non-select statement                                    *
 *                                                                            *
 * Comments: retry until DB is up                                             *
 *                                                                            *
 ******************************************************************************/
int	DBexecute(const char *fmt, ...)
{
	va_list	args;
	int	rc;

	va_start(args, fmt);

	rc = trx_db_vexecute(fmt, args);

	while (TRX_DB_DOWN == rc)
	{
		DBclose();
		DBconnect(TRX_DB_CONNECT_NORMAL);

		if (TRX_DB_DOWN == (rc = trx_db_vexecute(fmt, args)))
		{
			treegix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", TRX_DB_WAIT_DOWN);
			connection_failure = 1;
			sleep(TRX_DB_WAIT_DOWN);
		}
	}

	va_end(args);

	return rc;
}

/******************************************************************************
 *                                                                            *
 * Function: __trx_DBexecute_once                                             *
 *                                                                            *
 * Purpose: execute a non-select statement                                    *
 *                                                                            *
 * Comments: don't retry if DB is down                                        *
 *                                                                            *
 ******************************************************************************/
int	DBexecute_once(const char *fmt, ...)
{
	va_list	args;
	int	rc;

	va_start(args, fmt);

	rc = trx_db_vexecute(fmt, args);

	va_end(args);

	return rc;
}

int	DBis_null(const char *field)
{
	return trx_db_is_null(field);
}

DB_ROW	DBfetch(DB_RESULT result)
{
	return trx_db_fetch(result);
}

/******************************************************************************
 *                                                                            *
 * Function: DBselect_once                                                    *
 *                                                                            *
 * Purpose: execute a select statement                                        *
 *                                                                            *
 ******************************************************************************/
DB_RESULT	DBselect_once(const char *fmt, ...)
{
	va_list		args;
	DB_RESULT	rc;

	va_start(args, fmt);

	rc = trx_db_vselect(fmt, args);

	va_end(args);

	return rc;
}

/******************************************************************************
 *                                                                            *
 * Function: DBselect                                                         *
 *                                                                            *
 * Purpose: execute a select statement                                        *
 *                                                                            *
 * Comments: retry until DB is up                                             *
 *                                                                            *
 ******************************************************************************/
DB_RESULT	DBselect(const char *fmt, ...)
{
	va_list		args;
	DB_RESULT	rc;

	va_start(args, fmt);

	rc = trx_db_vselect(fmt, args);

	while ((DB_RESULT)TRX_DB_DOWN == rc)
	{
		DBclose();
		DBconnect(TRX_DB_CONNECT_NORMAL);

		if ((DB_RESULT)TRX_DB_DOWN == (rc = trx_db_vselect(fmt, args)))
		{
			treegix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", TRX_DB_WAIT_DOWN);
			connection_failure = 1;
			sleep(TRX_DB_WAIT_DOWN);
		}
	}

	va_end(args);

	return rc;
}

/******************************************************************************
 *                                                                            *
 * Function: DBselectN                                                        *
 *                                                                            *
 * Purpose: execute a select statement and get the first N entries            *
 *                                                                            *
 * Comments: retry until DB is up                                             *
 *                                                                            *
 ******************************************************************************/
DB_RESULT	DBselectN(const char *query, int n)
{
	DB_RESULT	rc;

	rc = trx_db_select_n(query, n);

	while ((DB_RESULT)TRX_DB_DOWN == rc)
	{
		DBclose();
		DBconnect(TRX_DB_CONNECT_NORMAL);

		if ((DB_RESULT)TRX_DB_DOWN == (rc = trx_db_select_n(query, n)))
		{
			treegix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", TRX_DB_WAIT_DOWN);
			connection_failure = 1;
			sleep(TRX_DB_WAIT_DOWN);
		}
	}

	return rc;
}

int	DBget_row_count(const char *table_name)
{
	int		count = 0;
	DB_RESULT	result;
	DB_ROW		row;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() table_name:'%s'", __func__, table_name);

	result = DBselect("select count(*) from %s", table_name);

	if (NULL != (row = DBfetch(result)))
		count = atoi(row[0]);
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, count);

	return count;
}

int	DBget_proxy_lastaccess(const char *hostname, int *lastaccess, char **error)
{
	DB_RESULT	result;
	DB_ROW		row;
	char		*host_esc;
	int		ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	host_esc = DBdyn_escape_string(hostname);
	result = DBselect("select lastaccess from hosts where host='%s' and status in (%d,%d)",
			host_esc, HOST_STATUS_PROXY_ACTIVE, HOST_STATUS_PROXY_PASSIVE);
	trx_free(host_esc);

	if (NULL != (row = DBfetch(result)))
	{
		*lastaccess = atoi(row[0]);
		ret = SUCCEED;
	}
	else
		*error = trx_dsprintf(*error, "Proxy \"%s\" does not exist.", hostname);
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

#ifdef HAVE_MYSQL
static size_t	get_string_field_size(unsigned char type)
{
	switch(type)
	{
		case TRX_TYPE_LONGTEXT:
			return TRX_SIZE_T_MAX;
		case TRX_TYPE_CHAR:
		case TRX_TYPE_TEXT:
		case TRX_TYPE_SHORTTEXT:
			return 65535u;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
	}
}
#elif HAVE_ORACLE
static size_t	get_string_field_size(unsigned char type)
{
	switch(type)
	{
		case TRX_TYPE_LONGTEXT:
		case TRX_TYPE_TEXT:
			return TRX_SIZE_T_MAX;
		case TRX_TYPE_CHAR:
		case TRX_TYPE_SHORTTEXT:
			return 4000u;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
	}
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: DBdyn_escape_string_len                                          *
 *                                                                            *
 ******************************************************************************/
char	*DBdyn_escape_string_len(const char *src, size_t length)
{
#if HAVE_IBM_DB2	/* IBM DB2 fields are limited by bytes rather than characters */
	return trx_db_dyn_escape_string(src, length, TRX_SIZE_T_MAX, ESCAPE_SEQUENCE_ON);
#else
	return trx_db_dyn_escape_string(src, TRX_SIZE_T_MAX, length, ESCAPE_SEQUENCE_ON);
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: DBdyn_escape_string                                              *
 *                                                                            *
 ******************************************************************************/
char	*DBdyn_escape_string(const char *src)
{
	return trx_db_dyn_escape_string(src, TRX_SIZE_T_MAX, TRX_SIZE_T_MAX, ESCAPE_SEQUENCE_ON);
}

/******************************************************************************
 *                                                                            *
 * Function: DBdyn_escape_field_len                                           *
 *                                                                            *
 ******************************************************************************/
static char	*DBdyn_escape_field_len(const TRX_FIELD *field, const char *src, trx_escape_sequence_t flag)
{
	size_t	length;

	if (TRX_TYPE_LONGTEXT == field->type && 0 == field->length)
		length = TRX_SIZE_T_MAX;
	else
		length = field->length;

#if defined(HAVE_MYSQL) || defined(HAVE_ORACLE)
	return trx_db_dyn_escape_string(src, get_string_field_size(field->type), length, flag);
#elif HAVE_IBM_DB2	/* IBM DB2 fields are limited by bytes rather than characters */
	return trx_db_dyn_escape_string(src, length, TRX_SIZE_T_MAX, flag);
#else
	return trx_db_dyn_escape_string(src, TRX_SIZE_T_MAX, length, flag);
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: DBdyn_escape_field                                               *
 *                                                                            *
 ******************************************************************************/
char	*DBdyn_escape_field(const char *table_name, const char *field_name, const char *src)
{
	const TRX_TABLE	*table;
	const TRX_FIELD	*field;

	if (NULL == (table = DBget_table(table_name)) || NULL == (field = DBget_field(table, field_name)))
	{
		treegix_log(LOG_LEVEL_CRIT, "invalid table: \"%s\" field: \"%s\"", table_name, field_name);
		exit(EXIT_FAILURE);
	}

	return DBdyn_escape_field_len(field, src, ESCAPE_SEQUENCE_ON);
}

/******************************************************************************
 *                                                                            *
 * Function: DBdyn_escape_like_pattern                                        *
 *                                                                            *
 ******************************************************************************/
char	*DBdyn_escape_like_pattern(const char *src)
{
	return trx_db_dyn_escape_like_pattern(src);
}

const TRX_TABLE	*DBget_table(const char *tablename)
{
	int	t;

	for (t = 0; NULL != tables[t].table; t++)
	{
		if (0 == strcmp(tables[t].table, tablename))
			return &tables[t];
	}

	return NULL;
}

const TRX_FIELD	*DBget_field(const TRX_TABLE *table, const char *fieldname)
{
	int	f;

	for (f = 0; NULL != table->fields[f].name; f++)
	{
		if (0 == strcmp(table->fields[f].name, fieldname))
			return &table->fields[f];
	}

	return NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: DBget_nextid                                                     *
 *                                                                            *
 * Purpose: gets a new identifier(s) for a specified table                    *
 *                                                                            *
 * Parameters: tablename - [IN] the name of a table                           *
 *             num       - [IN] the number of reserved records                *
 *                                                                            *
 * Return value: first reserved identifier                                    *
 *                                                                            *
 ******************************************************************************/
static trx_uint64_t	DBget_nextid(const char *tablename, int num)
{
	DB_RESULT	result;
	DB_ROW		row;
	trx_uint64_t	ret1, ret2;
	trx_uint64_t	min = 0, max = TRX_DB_MAX_ID;
	int		found = FAIL, dbres;
	const TRX_TABLE	*table;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() tablename:'%s'", __func__, tablename);

	table = DBget_table(tablename);

	while (FAIL == found)
	{
		/* avoid eternal loop within failed transaction */
		if (0 < trx_db_txn_level() && 0 != trx_db_txn_error())
		{
			treegix_log(LOG_LEVEL_DEBUG, "End of %s() transaction failed", __func__);
			return 0;
		}

		result = DBselect("select nextid from ids where table_name='%s' and field_name='%s'",
				table->table, table->recid);

		if (NULL == (row = DBfetch(result)))
		{
			DBfree_result(result);

			result = DBselect("select max(%s) from %s where %s between " TRX_FS_UI64 " and " TRX_FS_UI64,
					table->recid, table->table, table->recid, min, max);

			if (NULL == (row = DBfetch(result)) || SUCCEED == DBis_null(row[0]))
			{
				ret1 = min;
			}
			else
			{
				TRX_STR2UINT64(ret1, row[0]);
				if (ret1 >= max)
				{
					treegix_log(LOG_LEVEL_CRIT, "maximum number of id's exceeded"
							" [table:%s, field:%s, id:" TRX_FS_UI64 "]",
							table->table, table->recid, ret1);
					exit(EXIT_FAILURE);
				}
			}

			DBfree_result(result);

			dbres = DBexecute("insert into ids (table_name,field_name,nextid)"
					" values ('%s','%s'," TRX_FS_UI64 ")",
					table->table, table->recid, ret1);

			if (TRX_DB_OK > dbres)
			{
				/* solving the problem of an invisible record created in a parallel transaction */
				DBexecute("update ids set nextid=nextid+1 where table_name='%s' and field_name='%s'",
						table->table, table->recid);
			}

			continue;
		}
		else
		{
			TRX_STR2UINT64(ret1, row[0]);
			DBfree_result(result);

			if (ret1 < min || ret1 >= max)
			{
				DBexecute("delete from ids where table_name='%s' and field_name='%s'",
						table->table, table->recid);
				continue;
			}

			DBexecute("update ids set nextid=nextid+%d where table_name='%s' and field_name='%s'",
					num, table->table, table->recid);

			result = DBselect("select nextid from ids where table_name='%s' and field_name='%s'",
					table->table, table->recid);

			if (NULL != (row = DBfetch(result)) && SUCCEED != DBis_null(row[0]))
			{
				TRX_STR2UINT64(ret2, row[0]);

				if (ret1 + num == ret2)
					found = SUCCEED;
			}
			else
				THIS_SHOULD_NEVER_HAPPEN;

			DBfree_result(result);
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():" TRX_FS_UI64 " table:'%s' recid:'%s'",
			__func__, ret2 - num + 1, table->table, table->recid);

	return ret2 - num + 1;
}

trx_uint64_t	DBget_maxid_num(const char *tablename, int num)
{
	if (0 == strcmp(tablename, "events") ||
			0 == strcmp(tablename, "event_tag") ||
			0 == strcmp(tablename, "problem_tag") ||
			0 == strcmp(tablename, "dservices") ||
			0 == strcmp(tablename, "dhosts") ||
			0 == strcmp(tablename, "alerts") ||
			0 == strcmp(tablename, "escalations") ||
			0 == strcmp(tablename, "autoreg_host"))
		return DCget_nextid(tablename, num);

	return DBget_nextid(tablename, num);
}

#define MAX_EXPRESSIONS	950
#define MIN_NUM_BETWEEN	5	/* minimum number of consecutive values for using "between <id1> and <idN>" */

#ifdef HAVE_ORACLE
/******************************************************************************
 *                                                                            *
 * Function: DBadd_condition_alloc_btw                                        *
 *                                                                            *
 * Purpose: Takes an initial part of SQL query and appends a generated        *
 *          WHERE condition. The WHERE condition is generated from the given  *
 *          list of values as a mix of <fieldname> BETWEEN <id1> AND <idN>"   *
 *                                                                            *
 * Parameters: sql        - [IN/OUT] buffer for SQL query construction        *
 *             sql_alloc  - [IN/OUT] size of the 'sql' buffer                 *
 *             sql_offset - [IN/OUT] current position in the 'sql' buffer     *
 *             fieldname  - [IN] field name to be used in SQL WHERE condition *
 *             values     - [IN] array of numerical values sorted in          *
 *                               ascending order to be included in WHERE      *
 *             num        - [IN] number of elements in 'values' array         *
 *             seq_len    - [OUT] - array of sequential chains                *
 *             seq_num    - [OUT] - length of seq_len                         *
 *             in_num     - [OUT] - number of id for 'IN'                     *
 *             between_num- [OUT] - number of sequential chains for 'BETWEEN' *
 *                                                                            *
 ******************************************************************************/
static void	DBadd_condition_alloc_btw(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *fieldname,
		const trx_uint64_t *values, const int num, int **seq_len, int *seq_num, int *in_num, int *between_num)
{
	int		i, len, first, start;
	trx_uint64_t	value;

	/* Store lengths of consecutive sequences of values in a temporary array 'seq_len'. */
	/* An isolated value is represented as a sequence with length 1. */
	*seq_len = (int *)trx_malloc(*seq_len, num * sizeof(int));

	for (i = 1, *seq_num = 0, value = values[0], len = 1; i < num; i++)
	{
		if (values[i] != ++value)
		{
			if (MIN_NUM_BETWEEN <= len)
				(*between_num)++;
			else
				*in_num += len;

			(*seq_len)[(*seq_num)++] = len;
			len = 1;
			value = values[i];
		}
		else
			len++;
	}

	if (MIN_NUM_BETWEEN <= len)
		(*between_num)++;
	else
		*in_num += len;

	(*seq_len)[(*seq_num)++] = len;

	if (MAX_EXPRESSIONS < *in_num || 1 < *between_num || (0 < *in_num && 0 < *between_num))
		trx_chrcpy_alloc(sql, sql_alloc, sql_offset, '(');

	/* compose "between"s */
	for (i = 0, first = 1, start = 0; i < *seq_num; i++)
	{
		if (MIN_NUM_BETWEEN <= (*seq_len)[i])
		{
			if (1 != first)
			{
					trx_strcpy_alloc(sql, sql_alloc, sql_offset, " or ");
			}
			else
				first = 0;

			trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s between " TRX_FS_UI64 " and " TRX_FS_UI64,
					fieldname, values[start], values[start + (*seq_len)[i] - 1]);
		}

		start += (*seq_len)[i];
	}

	if (0 < *in_num && 0 < *between_num)
	{
		trx_strcpy_alloc(sql, sql_alloc, sql_offset, " or ");
	}
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: DBadd_condition_alloc                                            *
 *                                                                            *
 * Purpose: Takes an initial part of SQL query and appends a generated        *
 *          WHERE condition. The WHERE condition is generated from the given  *
 *          list of values as a mix of <fieldname> BETWEEN <id1> AND <idN>"   *
 *          and "<fieldname> IN (<id1>,<id2>,...,<idN>)" elements.            *
 *                                                                            *
 * Parameters: sql        - [IN/OUT] buffer for SQL query construction        *
 *             sql_alloc  - [IN/OUT] size of the 'sql' buffer                 *
 *             sql_offset - [IN/OUT] current position in the 'sql' buffer     *
 *             fieldname  - [IN] field name to be used in SQL WHERE condition *
 *             values     - [IN] array of numerical values sorted in          *
 *                               ascending order to be included in WHERE      *
 *             num        - [IN] number of elements in 'values' array         *
 *                                                                            *
 ******************************************************************************/
void	DBadd_condition_alloc(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *fieldname,
		const trx_uint64_t *values, const int num)
{
#ifdef HAVE_ORACLE
	int		start, between_num = 0, in_num = 0, seq_num;
	int		*seq_len = NULL;
#endif
	int		i, in_cnt;
#if defined(HAVE_SQLITE3)
	int		expr_num, expr_cnt = 0;
#endif
	if (0 == num)
		return;

	trx_chrcpy_alloc(sql, sql_alloc, sql_offset, ' ');
#ifdef HAVE_ORACLE
	DBadd_condition_alloc_btw(sql, sql_alloc, sql_offset, fieldname, values, num, &seq_len, &seq_num, &in_num,
			&between_num);

	if (1 < in_num)
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s in (", fieldname);

	/* compose "in"s */
	for (i = 0, in_cnt = 0, start = 0; i < seq_num; i++)
	{
		if (MIN_NUM_BETWEEN > seq_len[i])
		{
			if (1 == in_num)
#else
	if (MAX_EXPRESSIONS < num)
		trx_chrcpy_alloc(sql, sql_alloc, sql_offset, '(');

#if	defined(HAVE_SQLITE3)
	expr_num = (num + MAX_EXPRESSIONS - 1) / MAX_EXPRESSIONS;

	if (MAX_EXPRESSIONS < expr_num)
		trx_chrcpy_alloc(sql, sql_alloc, sql_offset, '(');
#endif

	if (1 < num)
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s in (", fieldname);

	/* compose "in"s */
	for (i = 0, in_cnt = 0; i < num; i++)
	{
			if (1 == num)
#endif
			{
				trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s=" TRX_FS_UI64, fieldname,
#ifdef HAVE_ORACLE
						values[start]);
#else
						values[i]);
#endif
				break;
			}
			else
			{
#ifdef HAVE_ORACLE
				do
				{
#endif
					if (MAX_EXPRESSIONS == in_cnt)
					{
						in_cnt = 0;
						(*sql_offset)--;
#if defined(HAVE_SQLITE3)
						if (MAX_EXPRESSIONS == ++expr_cnt)
						{
							trx_snprintf_alloc(sql, sql_alloc, sql_offset, ")) or (%s in (",
									fieldname);
							expr_cnt = 0;
						}
						else
						{
#endif
							trx_snprintf_alloc(sql, sql_alloc, sql_offset, ") or %s in (",
									fieldname);
#if defined(HAVE_SQLITE3)
						}
#endif
					}

					in_cnt++;
					trx_snprintf_alloc(sql, sql_alloc, sql_offset, TRX_FS_UI64 ",",
#ifdef HAVE_ORACLE
							values[start++]);
				}
				while (0 != --seq_len[i]);
			}
		}
		else
			start += seq_len[i];
	}

	trx_free(seq_len);

	if (1 < in_num)
#else
							values[i]);
			}
	}

	if (1 < num)
#endif
	{
		(*sql_offset)--;
		trx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');
	}

#if defined(HAVE_SQLITE3)
	if (MAX_EXPRESSIONS < expr_num)
		trx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');
#endif
#ifdef HAVE_ORACLE
	if (MAX_EXPRESSIONS < in_num || 1 < between_num || (0 < in_num && 0 < between_num))
#else
	if (MAX_EXPRESSIONS < num)
#endif
		trx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');

#undef MAX_EXPRESSIONS
#undef MIN_NUM_BETWEEN
}

/******************************************************************************
 *                                                                            *
 * Function: DBadd_str_condition_alloc                                        *
 *                                                                            *
 * Purpose: This function is similar to DBadd_condition_alloc(), except it is *
 *          designed for generating WHERE conditions for strings. Hence, this *
 *          function is simpler, because only IN condition is possible.       *
 *                                                                            *
 * Parameters: sql        - [IN/OUT] buffer for SQL query construction        *
 *             sql_alloc  - [IN/OUT] size of the 'sql' buffer                 *
 *             sql_offset - [IN/OUT] current position in the 'sql' buffer     *
 *             fieldname  - [IN] field name to be used in SQL WHERE condition *
 *             values     - [IN] array of string values                       *
 *             num        - [IN] number of elements in 'values' array         *
 *                                                                            *
 * Comments: To support Oracle empty values are checked separately (is null   *
 *           for Oracle and ='' for the other databases).                     *
 *                                                                            *
 ******************************************************************************/
void	DBadd_str_condition_alloc(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *fieldname,
		const char **values, const int num)
{
#define MAX_EXPRESSIONS	950

	int	i, cnt = 0;
	char	*value_esc;
	int	values_num = 0, empty_num = 0;

	if (0 == num)
		return;

	trx_chrcpy_alloc(sql, sql_alloc, sql_offset, ' ');

	for (i = 0; i < num; i++)
	{
		if ('\0' == *values[i])
			empty_num++;
		else
			values_num++;
	}

	if (MAX_EXPRESSIONS < values_num || (0 != values_num && 0 != empty_num))
		trx_chrcpy_alloc(sql, sql_alloc, sql_offset, '(');

	if (0 != empty_num)
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s" TRX_SQL_STRCMP, fieldname, TRX_SQL_STRVAL_EQ(""));

		if (0 == values_num)
			return;

		trx_strcpy_alloc(sql, sql_alloc, sql_offset, " or ");
	}

	if (1 == values_num)
	{
		for (i = 0; i < num; i++)
		{
			if ('\0' == *values[i])
				continue;

			value_esc = DBdyn_escape_string(values[i]);
			trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s='%s'", fieldname, value_esc);
			trx_free(value_esc);
		}

		if (0 != empty_num)
			trx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');
		return;
	}

	trx_strcpy_alloc(sql, sql_alloc, sql_offset, fieldname);
	trx_strcpy_alloc(sql, sql_alloc, sql_offset, " in (");

	for (i = 0; i < num; i++)
	{
		if ('\0' == *values[i])
			continue;

		if (MAX_EXPRESSIONS == cnt)
		{
			cnt = 0;
			(*sql_offset)--;
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, ") or ");
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, fieldname);
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, " in (");
		}

		value_esc = DBdyn_escape_string(values[i]);
		trx_chrcpy_alloc(sql, sql_alloc, sql_offset, '\'');
		trx_strcpy_alloc(sql, sql_alloc, sql_offset, value_esc);
		trx_strcpy_alloc(sql, sql_alloc, sql_offset, "',");
		trx_free(value_esc);

		cnt++;
	}

	(*sql_offset)--;
	trx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');

	if (MAX_EXPRESSIONS < values_num || 0 != empty_num)
		trx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');

#undef MAX_EXPRESSIONS
}

static char	buf_string[640];

/******************************************************************************
 *                                                                            *
 * Function: trx_host_string                                                  *
 *                                                                            *
 * Return value: <host> or "???" if host not found                            *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
const char	*trx_host_string(trx_uint64_t hostid)
{
	DB_RESULT	result;
	DB_ROW		row;

	result = DBselect(
			"select host"
			" from hosts"
			" where hostid=" TRX_FS_UI64,
			hostid);

	if (NULL != (row = DBfetch(result)))
		trx_snprintf(buf_string, sizeof(buf_string), "%s", row[0]);
	else
		trx_snprintf(buf_string, sizeof(buf_string), "???");

	DBfree_result(result);

	return buf_string;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_host_key_string                                              *
 *                                                                            *
 * Return value: <host>:<key> or "???" if item not found                      *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
const char	*trx_host_key_string(trx_uint64_t itemid)
{
	DB_RESULT	result;
	DB_ROW		row;

	result = DBselect(
			"select h.host,i.key_"
			" from hosts h,items i"
			" where h.hostid=i.hostid"
				" and i.itemid=" TRX_FS_UI64,
			itemid);

	if (NULL != (row = DBfetch(result)))
		trx_snprintf(buf_string, sizeof(buf_string), "%s:%s", row[0], row[1]);
	else
		trx_snprintf(buf_string, sizeof(buf_string), "???");

	DBfree_result(result);

	return buf_string;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_check_user_permissions                                       *
 *                                                                            *
 * Purpose: check if user has access rights to information - full name,       *
 *          alias, Email, SMS, etc                                            *
 *                                                                            *
 * Parameters: userid           - [IN] user who owns the information          *
 *             recipient_userid - [IN] user who will receive the information  *
 *                                     can be NULL for remote command         *
 *                                                                            *
 * Return value: SUCCEED - if information receiving user has access rights    *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: Users has access rights or can view personal information only    *
 *           about themselves and other user who belong to their group.       *
 *           "Treegix Super Admin" can view and has access rights to           *
 *           information about any user.                                      *
 *                                                                            *
 ******************************************************************************/
int	trx_check_user_permissions(const trx_uint64_t *userid, const trx_uint64_t *recipient_userid)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		user_type = -1, ret = SUCCEED;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (NULL == recipient_userid || *userid == *recipient_userid)
		goto out;

	result = DBselect("select type from users where userid=" TRX_FS_UI64, *recipient_userid);

	if (NULL != (row = DBfetch(result)) && FAIL == DBis_null(row[0]))
		user_type = atoi(row[0]);
	DBfree_result(result);

	if (-1 == user_type)
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s() cannot check permissions", __func__);
		ret = FAIL;
		goto out;
	}

	if (USER_TYPE_SUPER_ADMIN != user_type)
	{
		/* check if users are from the same user group */
		result = DBselect(
				"select null"
				" from users_groups ug1"
				" where ug1.userid=" TRX_FS_UI64
					" and exists (select null"
						" from users_groups ug2"
						" where ug1.usrgrpid=ug2.usrgrpid"
							" and ug2.userid=" TRX_FS_UI64
					")",
				*userid, *recipient_userid);

		if (NULL == DBfetch(result))
			ret = FAIL;
		DBfree_result(result);
	}
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_user_string                                                  *
 *                                                                            *
 * Return value: "Name Surname (Alias)" or "unknown" if user not found        *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
const char	*trx_user_string(trx_uint64_t userid)
{
	DB_RESULT	result;
	DB_ROW		row;

	result = DBselect("select name,surname,alias from users where userid=" TRX_FS_UI64, userid);

	if (NULL != (row = DBfetch(result)))
		trx_snprintf(buf_string, sizeof(buf_string), "%s %s (%s)", row[0], row[1], row[2]);
	else
		trx_snprintf(buf_string, sizeof(buf_string), "unknown");

	DBfree_result(result);

	return buf_string;
}

/******************************************************************************
 *                                                                            *
 * Function: DBsql_id_cmp                                                     *
 *                                                                            *
 * Purpose: construct where condition                                         *
 *                                                                            *
 * Return value: "=<id>" if id not equal zero,                                *
 *               otherwise " is null"                                         *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: NB! Do not use this function more than once in same SQL query    *
 *                                                                            *
 ******************************************************************************/
const char	*DBsql_id_cmp(trx_uint64_t id)
{
	static char		buf[22];	/* 1 - '=', 20 - value size, 1 - '\0' */
	static const char	is_null[9] = " is null";

	if (0 == id)
		return is_null;

	trx_snprintf(buf, sizeof(buf), "=" TRX_FS_UI64, id);

	return buf;
}

/******************************************************************************
 *                                                                            *
 * Function: DBregister_host                                                  *
 *                                                                            *
 * Purpose: register unknown host and generate event                          *
 *                                                                            *
 * Parameters: host - host name                                               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
void	DBregister_host(trx_uint64_t proxy_hostid, const char *host, const char *ip, const char *dns,
		unsigned short port, unsigned int connection_type, const char *host_metadata, unsigned short flag,
		int now)
{
	trx_vector_ptr_t	autoreg_hosts;

	trx_vector_ptr_create(&autoreg_hosts);

	DBregister_host_prepare(&autoreg_hosts, host, ip, dns, port, connection_type, host_metadata, flag, now);
	DBregister_host_flush(&autoreg_hosts, proxy_hostid);

	DBregister_host_clean(&autoreg_hosts);
	trx_vector_ptr_destroy(&autoreg_hosts);
}

static int	DBregister_host_active(void)
{
	DB_RESULT	result;
	int		ret = SUCCEED;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select null"
			" from actions"
			" where eventsource=%d"
				" and status=%d",
			EVENT_SOURCE_AUTO_REGISTRATION,
			ACTION_STATUS_ACTIVE);

	if (NULL == DBfetch(result))
		ret = FAIL;

	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static void	autoreg_host_free(trx_autoreg_host_t *autoreg_host)
{
	trx_free(autoreg_host->host);
	trx_free(autoreg_host->ip);
	trx_free(autoreg_host->dns);
	trx_free(autoreg_host->host_metadata);
	trx_free(autoreg_host);
}

void	DBregister_host_prepare(trx_vector_ptr_t *autoreg_hosts, const char *host, const char *ip, const char *dns,
		unsigned short port, unsigned int connection_type, const char *host_metadata, unsigned short flag,
		int now)
{
	trx_autoreg_host_t	*autoreg_host;
	int 			i;

	for (i = 0; i < autoreg_hosts->values_num; i++)	/* duplicate check */
	{
		autoreg_host = (trx_autoreg_host_t *)autoreg_hosts->values[i];

		if (0 == strcmp(host, autoreg_host->host))
		{
			trx_vector_ptr_remove(autoreg_hosts, i);
			autoreg_host_free(autoreg_host);
			break;
		}
	}

	autoreg_host = (trx_autoreg_host_t *)trx_malloc(NULL, sizeof(trx_autoreg_host_t));
	autoreg_host->autoreg_hostid = autoreg_host->hostid = 0;
	autoreg_host->host = trx_strdup(NULL, host);
	autoreg_host->ip = trx_strdup(NULL, ip);
	autoreg_host->dns = trx_strdup(NULL, dns);
	autoreg_host->port = port;
	autoreg_host->connection_type = connection_type;
	autoreg_host->host_metadata = trx_strdup(NULL, host_metadata);
	autoreg_host->flag = flag;
	autoreg_host->now = now;

	trx_vector_ptr_append(autoreg_hosts, autoreg_host);
}

static void	autoreg_get_hosts(trx_vector_ptr_t *autoreg_hosts, trx_vector_str_t *hosts)
{
	int	i;

	for (i = 0; i < autoreg_hosts->values_num; i++)
	{
		trx_autoreg_host_t	*autoreg_host = (trx_autoreg_host_t *)autoreg_hosts->values[i];

		trx_vector_str_append(hosts, autoreg_host->host);
	}
}

static void	process_autoreg_hosts(trx_vector_ptr_t *autoreg_hosts, trx_uint64_t proxy_hostid)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_vector_str_t	hosts;
	trx_uint64_t		current_proxy_hostid;
	char			*sql = NULL;
	size_t			sql_alloc = 256, sql_offset;
	trx_autoreg_host_t	*autoreg_host;
	int			i;

	sql = (char *)trx_malloc(sql, sql_alloc);
	trx_vector_str_create(&hosts);

	if (0 != proxy_hostid)
	{
		autoreg_get_hosts(autoreg_hosts, &hosts);

		/* delete from vector if already exist in hosts table */
		sql_offset = 0;
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select h.host,h.hostid,h.proxy_hostid,a.host_metadata,a.listen_ip,a.listen_dns,"
					"a.listen_port,a.flags"
				" from hosts h"
				" left join autoreg_host a"
					" on a.proxy_hostid=h.proxy_hostid and a.host=h.host"
				" where");
		DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "h.host",
				(const char **)hosts.values, hosts.values_num);

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			for (i = 0; i < autoreg_hosts->values_num; i++)
			{
				autoreg_host = (trx_autoreg_host_t *)autoreg_hosts->values[i];

				if (0 != strcmp(autoreg_host->host, row[0]))
					continue;

				TRX_STR2UINT64(autoreg_host->hostid, row[1]);
				TRX_DBROW2UINT64(current_proxy_hostid, row[2]);

				if (current_proxy_hostid != proxy_hostid || SUCCEED == DBis_null(row[3]) ||
						0 != strcmp(autoreg_host->host_metadata, row[3]) ||
						autoreg_host->flag != atoi(row[7]))
				{
					break;
				}

				/* process with auto registration if the connection type was forced and */
				/* is different from the last registered connection type                */
				if (TRX_CONN_DEFAULT != autoreg_host->flag)
				{
					unsigned short	port;

					if (FAIL == is_ushort(row[6], &port) || port != autoreg_host->port)
						break;

					if (TRX_CONN_IP == autoreg_host->flag && 0 != strcmp(row[4], autoreg_host->ip))
						break;

					if (TRX_CONN_DNS == autoreg_host->flag && 0 != strcmp(row[5], autoreg_host->dns))
						break;
				}

				trx_vector_ptr_remove(autoreg_hosts, i);
				autoreg_host_free(autoreg_host);

				break;
			}

		}
		DBfree_result(result);

		hosts.values_num = 0;
	}

	if (0 != autoreg_hosts->values_num)
	{
		autoreg_get_hosts(autoreg_hosts, &hosts);

		/* update autoreg_id in vector if already exists in autoreg_host table */
		sql_offset = 0;
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select autoreg_hostid,host"
				" from autoreg_host"
				" where");
		DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "host",
				(const char **)hosts.values, hosts.values_num);

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			for (i = 0; i < autoreg_hosts->values_num; i++)
			{
				autoreg_host = (trx_autoreg_host_t *)autoreg_hosts->values[i];

				if (0 == autoreg_host->autoreg_hostid && 0 == strcmp(autoreg_host->host, row[1]))
				{
					TRX_STR2UINT64(autoreg_host->autoreg_hostid, row[0]);
					break;
				}
			}
		}
		DBfree_result(result);

		hosts.values_num = 0;
	}

	trx_vector_str_destroy(&hosts);
	trx_free(sql);
}

static int	compare_autoreg_host_by_hostid(const void *d1, const void *d2)
{
	const trx_autoreg_host_t	*p1 = *(const trx_autoreg_host_t **)d1;
	const trx_autoreg_host_t	*p2 = *(const trx_autoreg_host_t **)d2;

	TRX_RETURN_IF_NOT_EQUAL(p1->hostid, p2->hostid);

	return 0;
}

void	DBregister_host_flush(trx_vector_ptr_t *autoreg_hosts, trx_uint64_t proxy_hostid)
{
	trx_autoreg_host_t	*autoreg_host;
	trx_uint64_t		autoreg_hostid;
	trx_db_insert_t		db_insert;
	int			i, create = 0, update = 0;
	char			*sql = NULL, *ip_esc, *dns_esc, *host_metadata_esc;
	size_t			sql_alloc = 256, sql_offset = 0;
	trx_timespec_t		ts = {0, 0};

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != DBregister_host_active())
		goto exit;

	process_autoreg_hosts(autoreg_hosts, proxy_hostid);

	for (i = 0; i < autoreg_hosts->values_num; i++)
	{
		autoreg_host = (trx_autoreg_host_t *)autoreg_hosts->values[i];

		if (0 == autoreg_host->autoreg_hostid)
			create++;
	}

	if (0 != create)
	{
		autoreg_hostid = DBget_maxid_num("autoreg_host", create);

		trx_db_insert_prepare(&db_insert, "autoreg_host", "autoreg_hostid", "proxy_hostid", "host", "listen_ip",
				"listen_dns", "listen_port", "tls_accepted", "host_metadata", "flags", NULL);
	}

	if (0 != (update = autoreg_hosts->values_num - create))
	{
		sql = (char *)trx_malloc(sql, sql_alloc);
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	trx_vector_ptr_sort(autoreg_hosts, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < autoreg_hosts->values_num; i++)
	{
		autoreg_host = (trx_autoreg_host_t *)autoreg_hosts->values[i];

		if (0 == autoreg_host->autoreg_hostid)
		{
			autoreg_host->autoreg_hostid = autoreg_hostid++;

			trx_db_insert_add_values(&db_insert, autoreg_host->autoreg_hostid, proxy_hostid,
					autoreg_host->host, autoreg_host->ip, autoreg_host->dns,
					(int)autoreg_host->port, (int)autoreg_host->connection_type,
					autoreg_host->host_metadata, autoreg_host->flag);
		}
		else
		{
			ip_esc = DBdyn_escape_string(autoreg_host->ip);
			dns_esc = DBdyn_escape_string(autoreg_host->dns);
			host_metadata_esc = DBdyn_escape_string(autoreg_host->host_metadata);

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"update autoreg_host"
					" set listen_ip='%s',"
						"listen_dns='%s',"
						"listen_port=%hu,"
						"host_metadata='%s',"
						"tls_accepted='%u',"
						"flags=%hu,"
						"proxy_hostid=%s"
					" where autoreg_hostid=" TRX_FS_UI64 ";\n",
				ip_esc, dns_esc, autoreg_host->port, host_metadata_esc, autoreg_host->connection_type,
				autoreg_host->flag, DBsql_id_ins(proxy_hostid), autoreg_host->autoreg_hostid);

			trx_free(host_metadata_esc);
			trx_free(dns_esc);
			trx_free(ip_esc);
		}
	}

	if (0 != create)
	{
		trx_db_insert_execute(&db_insert);
		trx_db_insert_clean(&db_insert);
	}

	if (0 != update)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);
		DBexecute("%s", sql);
		trx_free(sql);
	}

	trx_vector_ptr_sort(autoreg_hosts, compare_autoreg_host_by_hostid);

	for (i = 0; i < autoreg_hosts->values_num; i++)
	{
		autoreg_host = (trx_autoreg_host_t *)autoreg_hosts->values[i];

		ts.sec = autoreg_host->now;
		trx_add_event(EVENT_SOURCE_AUTO_REGISTRATION, EVENT_OBJECT_TREEGIX_ACTIVE, autoreg_host->autoreg_hostid,
				&ts, TRIGGER_VALUE_PROBLEM, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL, NULL);
	}

	trx_process_events(NULL, NULL);
	trx_clean_events();
exit:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

void	DBregister_host_clean(trx_vector_ptr_t *autoreg_hosts)
{
	trx_vector_ptr_clear_ext(autoreg_hosts, (trx_mem_free_func_t)autoreg_host_free);
}

/******************************************************************************
 *                                                                            *
 * Function: DBproxy_register_host                                            *
 *                                                                            *
 * Purpose: register unknown host                                             *
 *                                                                            *
 * Parameters: host - host name                                               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
void	DBproxy_register_host(const char *host, const char *ip, const char *dns, unsigned short port,
		unsigned int connection_type, const char *host_metadata, unsigned short flag)
{
	char	*host_esc, *ip_esc, *dns_esc, *host_metadata_esc;

	host_esc = DBdyn_escape_field("proxy_autoreg_host", "host", host);
	ip_esc = DBdyn_escape_field("proxy_autoreg_host", "listen_ip", ip);
	dns_esc = DBdyn_escape_field("proxy_autoreg_host", "listen_dns", dns);
	host_metadata_esc = DBdyn_escape_field("proxy_autoreg_host", "host_metadata", host_metadata);

	DBexecute("insert into proxy_autoreg_host"
			" (clock,host,listen_ip,listen_dns,listen_port,tls_accepted,host_metadata,flags)"
			" values"
			" (%d,'%s','%s','%s',%d,%u,'%s',%d)",
			(int)time(NULL), host_esc, ip_esc, dns_esc, (int)port, connection_type, host_metadata_esc,
			(int)flag);

	trx_free(host_metadata_esc);
	trx_free(dns_esc);
	trx_free(ip_esc);
	trx_free(host_esc);
}

/******************************************************************************
 *                                                                            *
 * Function: DBexecute_overflowed_sql                                         *
 *                                                                            *
 * Purpose: execute a set of SQL statements IF it is big enough               *
 *                                                                            *
 * Author: Dmitry Borovikov                                                   *
 *                                                                            *
 ******************************************************************************/
int	DBexecute_overflowed_sql(char **sql, size_t *sql_alloc, size_t *sql_offset)
{
	int	ret = SUCCEED;

	if (TRX_MAX_OVERFLOW_SQL_SIZE < *sql_offset)
	{
#ifdef HAVE_MULTIROW_INSERT
		if (',' == (*sql)[*sql_offset - 1])
		{
			(*sql_offset)--;
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, ";\n");
		}
#endif
#if defined(HAVE_ORACLE) && 0 == TRX_MAX_OVERFLOW_SQL_SIZE
		/* make sure we are not called twice without */
		/* putting a new sql into the buffer first */
		if (*sql_offset <= TRX_SQL_EXEC_FROM)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			return ret;
		}

		/* Oracle fails with ORA-00911 if it encounters ';' w/o PL/SQL block */
		trx_rtrim(*sql, TRX_WHITESPACE ";");
#else
		DBend_multiple_update(sql, sql_alloc, sql_offset);
#endif
		/* For Oracle with max_overflow_sql_size == 0, jump over "begin\n" */
		/* before execution. TRX_SQL_EXEC_FROM is 0 for all other cases. */
		if (TRX_DB_OK > DBexecute("%s", *sql + TRX_SQL_EXEC_FROM))
			ret = FAIL;

		*sql_offset = 0;

		DBbegin_multiple_update(sql, sql_alloc, sql_offset);
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBget_unique_hostname_by_sample                                  *
 *                                                                            *
 * Purpose: construct a unique host name by the given sample                  *
 *                                                                            *
 * Parameters: host_name_sample - a host name to start constructing from      *
 *             field_name       - field name for host or host visible name    *
 *                                                                            *
 * Return value: unique host name which does not exist in the database        *
 *                                                                            *
 * Author: Dmitry Borovikov                                                   *
 *                                                                            *
 * Comments: the sample cannot be empty                                       *
 *           constructs new by adding "_$(number+1)", where "number"          *
 *           shows count of the sample itself plus already constructed ones   *
 *           host_name_sample is not modified, allocates new memory!          *
 *                                                                            *
 ******************************************************************************/
char	*DBget_unique_hostname_by_sample(const char *host_name_sample, const char *field_name)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			full_match = 0, i;
	char			*host_name_temp = NULL, *host_name_sample_esc;
	trx_vector_uint64_t	nums;
	trx_uint64_t		num = 2;	/* produce alternatives starting from "2" */
	size_t			sz;

	assert(host_name_sample && *host_name_sample);

	treegix_log(LOG_LEVEL_DEBUG, "In %s() sample:'%s'", __func__, host_name_sample);

	trx_vector_uint64_create(&nums);
	trx_vector_uint64_reserve(&nums, 8);

	sz = strlen(host_name_sample);
	host_name_sample_esc = DBdyn_escape_like_pattern(host_name_sample);

	result = DBselect(
			"select %s"
			" from hosts"
			" where %s like '%s%%' escape '%c'"
				" and flags<>%d"
				" and status in (%d,%d,%d)",
				field_name, field_name, host_name_sample_esc, TRX_SQL_LIKE_ESCAPE_CHAR,
			TRX_FLAG_DISCOVERY_PROTOTYPE,
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, HOST_STATUS_TEMPLATE);

	trx_free(host_name_sample_esc);

	while (NULL != (row = DBfetch(result)))
	{
		trx_uint64_t	n;
		const char	*p;

		if (0 != strncmp(row[0], host_name_sample, sz))
			continue;

		p = row[0] + sz;

		if ('\0' == *p)
		{
			full_match = 1;
			continue;
		}

		if ('_' != *p || FAIL == is_uint64(p + 1, &n))
			continue;

		trx_vector_uint64_append(&nums, n);
	}
	DBfree_result(result);

	trx_vector_uint64_sort(&nums, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	if (0 == full_match)
	{
		host_name_temp = trx_strdup(host_name_temp, host_name_sample);
		goto clean;
	}

	for (i = 0; i < nums.values_num; i++)
	{
		if (num > nums.values[i])
			continue;

		if (num < nums.values[i])	/* found, all other will be bigger */
			break;

		num++;
	}

	host_name_temp = trx_dsprintf(host_name_temp, "%s_" TRX_FS_UI64, host_name_sample, num);
clean:
	trx_vector_uint64_destroy(&nums);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():'%s'", __func__, host_name_temp);

	return host_name_temp;
}

/******************************************************************************
 *                                                                            *
 * Function: DBsql_id_ins                                                     *
 *                                                                            *
 * Purpose: construct insert statement                                        *
 *                                                                            *
 * Return value: "<id>" if id not equal zero,                                 *
 *               otherwise "null"                                             *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
const char	*DBsql_id_ins(trx_uint64_t id)
{
	static unsigned char	n = 0;
	static char		buf[4][21];	/* 20 - value size, 1 - '\0' */
	static const char	null[5] = "null";

	if (0 == id)
		return null;

	n = (n + 1) & 3;

	trx_snprintf(buf[n], sizeof(buf[n]), TRX_FS_UI64, id);

	return buf[n];
}

/******************************************************************************
 *                                                                            *
 * Function: DBget_inventory_field                                            *
 *                                                                            *
 * Purpose: get corresponding host_inventory field name                       *
 *                                                                            *
 * Parameters: inventory_link - [IN] field link 1..HOST_INVENTORY_FIELD_COUNT *
 *                                                                            *
 * Return value: field name or NULL if value of inventory_link is incorrect   *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
const char	*DBget_inventory_field(unsigned char inventory_link)
{
	static const char	*inventory_fields[HOST_INVENTORY_FIELD_COUNT] =
	{
		"type", "type_full", "name", "alias", "os", "os_full", "os_short", "serialno_a", "serialno_b", "tag",
		"asset_tag", "macaddress_a", "macaddress_b", "hardware", "hardware_full", "software", "software_full",
		"software_app_a", "software_app_b", "software_app_c", "software_app_d", "software_app_e", "contact",
		"location", "location_lat", "location_lon", "notes", "chassis", "model", "hw_arch", "vendor",
		"contract_number", "installer_name", "deployment_status", "url_a", "url_b", "url_c", "host_networks",
		"host_netmask", "host_router", "oob_ip", "oob_netmask", "oob_router", "date_hw_purchase",
		"date_hw_install", "date_hw_expiry", "date_hw_decomm", "site_address_a", "site_address_b",
		"site_address_c", "site_city", "site_state", "site_country", "site_zip", "site_rack", "site_notes",
		"poc_1_name", "poc_1_email", "poc_1_phone_a", "poc_1_phone_b", "poc_1_cell", "poc_1_screen",
		"poc_1_notes", "poc_2_name", "poc_2_email", "poc_2_phone_a", "poc_2_phone_b", "poc_2_cell",
		"poc_2_screen", "poc_2_notes"
	};

	if (1 > inventory_link || inventory_link > HOST_INVENTORY_FIELD_COUNT)
		return NULL;

	return inventory_fields[inventory_link - 1];
}

int	DBtxn_status(void)
{
	return 0 == trx_db_txn_error() ? SUCCEED : FAIL;
}

int	DBtxn_ongoing(void)
{
	return 0 == trx_db_txn_level() ? FAIL : SUCCEED;
}

int	DBtable_exists(const char *table_name)
{
	char		*table_name_esc;
#ifdef HAVE_POSTGRESQL
	char		*table_schema_esc;
#endif
	DB_RESULT	result;
	int		ret;

	table_name_esc = DBdyn_escape_string(table_name);

#if defined(HAVE_IBM_DB2)
	/* publib.boulder.ibm.com/infocenter/db2luw/v9r7/topic/com.ibm.db2.luw.admin.cmd.doc/doc/r0001967.html */
	result = DBselect(
			"select 1"
			" from syscat.tables"
			" where tabschema=user"
				" and lower(tabname)='%s'",
			table_name_esc);
#elif defined(HAVE_MYSQL)
	result = DBselect("show tables like '%s'", table_name_esc);
#elif defined(HAVE_ORACLE)
	result = DBselect(
			"select 1"
			" from tab"
			" where tabtype='TABLE'"
				" and lower(tname)='%s'",
			table_name_esc);
#elif defined(HAVE_POSTGRESQL)
	table_schema_esc = DBdyn_escape_string(NULL == CONFIG_DBSCHEMA || '\0' == *CONFIG_DBSCHEMA ?
			"public" : CONFIG_DBSCHEMA);

	result = DBselect(
			"select 1"
			" from information_schema.tables"
			" where table_name='%s'"
				" and table_schema='%s'",
			table_name_esc, table_schema_esc);

	trx_free(table_schema_esc);

#elif defined(HAVE_SQLITE3)
	result = DBselect(
			"select 1"
			" from sqlite_master"
			" where tbl_name='%s'"
				" and type='table'",
			table_name_esc);
#endif

	trx_free(table_name_esc);

	ret = (NULL == DBfetch(result) ? FAIL : SUCCEED);

	DBfree_result(result);

	return ret;
}

int	DBfield_exists(const char *table_name, const char *field_name)
{
	DB_RESULT	result;
#if defined(HAVE_IBM_DB2)
	char		*table_name_esc, *field_name_esc;
	int		ret;
#elif defined(HAVE_MYSQL)
	char		*field_name_esc;
	int		ret;
#elif defined(HAVE_ORACLE)
	char		*table_name_esc, *field_name_esc;
	int		ret;
#elif defined(HAVE_POSTGRESQL)
	char		*table_name_esc, *field_name_esc, *table_schema_esc;
	int		ret;
#elif defined(HAVE_SQLITE3)
	char		*table_name_esc;
	DB_ROW		row;
	int		ret = FAIL;
#endif

#if defined(HAVE_IBM_DB2)
	table_name_esc = DBdyn_escape_string(table_name);
	field_name_esc = DBdyn_escape_string(field_name);

	result = DBselect(
			"select 1"
			" from syscat.columns"
			" where tabschema=user"
				" and lower(tabname)='%s'"
				" and lower(colname)='%s'",
			table_name_esc, field_name_esc);

	trx_free(field_name_esc);
	trx_free(table_name_esc);

	ret = (NULL == DBfetch(result) ? FAIL : SUCCEED);

	DBfree_result(result);
#elif defined(HAVE_MYSQL)
	field_name_esc = DBdyn_escape_string(field_name);

	result = DBselect("show columns from %s like '%s'",
			table_name, field_name_esc);

	trx_free(field_name_esc);

	ret = (NULL == DBfetch(result) ? FAIL : SUCCEED);

	DBfree_result(result);
#elif defined(HAVE_ORACLE)
	table_name_esc = DBdyn_escape_string(table_name);
	field_name_esc = DBdyn_escape_string(field_name);

	result = DBselect(
			"select 1"
			" from col"
			" where lower(tname)='%s'"
				" and lower(cname)='%s'",
			table_name_esc, field_name_esc);

	trx_free(field_name_esc);
	trx_free(table_name_esc);

	ret = (NULL == DBfetch(result) ? FAIL : SUCCEED);

	DBfree_result(result);
#elif defined(HAVE_POSTGRESQL)
	table_schema_esc = DBdyn_escape_string(NULL == CONFIG_DBSCHEMA || '\0' == *CONFIG_DBSCHEMA ?
			"public" : CONFIG_DBSCHEMA);
	table_name_esc = DBdyn_escape_string(table_name);
	field_name_esc = DBdyn_escape_string(field_name);

	result = DBselect(
			"select 1"
			" from information_schema.columns"
			" where table_name='%s'"
				" and column_name='%s'"
				" and table_schema='%s'",
			table_name_esc, field_name_esc, table_schema_esc);

	trx_free(field_name_esc);
	trx_free(table_name_esc);
	trx_free(table_schema_esc);

	ret = (NULL == DBfetch(result) ? FAIL : SUCCEED);

	DBfree_result(result);
#elif defined(HAVE_SQLITE3)
	table_name_esc = DBdyn_escape_string(table_name);

	result = DBselect("PRAGMA table_info('%s')", table_name_esc);

	trx_free(table_name_esc);

	while (NULL != (row = DBfetch(result)))
	{
		if (0 != strcmp(field_name, row[1]))
			continue;

		ret = SUCCEED;
		break;
	}
	DBfree_result(result);
#endif

	return ret;
}

#ifndef HAVE_SQLITE3
int	DBindex_exists(const char *table_name, const char *index_name)
{
	char		*table_name_esc, *index_name_esc;
#if defined(HAVE_POSTGRESQL)
	char		*table_schema_esc;
#endif
	DB_RESULT	result;
	int		ret;

	table_name_esc = DBdyn_escape_string(table_name);
	index_name_esc = DBdyn_escape_string(index_name);

#if defined(HAVE_IBM_DB2)
	result = DBselect(
			"select 1"
			" from syscat.indexes"
			" where tabschema=user"
				" and lower(tabname)='%s'"
				" and lower(indname)='%s'",
			table_name_esc, index_name_esc);
#elif defined(HAVE_MYSQL)
	result = DBselect(
			"show index from %s"
			" where key_name='%s'",
			table_name_esc, index_name_esc);
#elif defined(HAVE_ORACLE)
	result = DBselect(
			"select 1"
			" from user_indexes"
			" where lower(table_name)='%s'"
				" and lower(index_name)='%s'",
			table_name_esc, index_name_esc);
#elif defined(HAVE_POSTGRESQL)
	table_schema_esc = DBdyn_escape_string(NULL == CONFIG_DBSCHEMA || '\0' == *CONFIG_DBSCHEMA ?
				"public" : CONFIG_DBSCHEMA);

	result = DBselect(
			"select 1"
			" from pg_indexes"
			" where tablename='%s'"
				" and indexname='%s'"
				" and schemaname='%s'",
			table_name_esc, index_name_esc, table_schema_esc);

	trx_free(table_schema_esc);
#endif

	ret = (NULL == DBfetch(result) ? FAIL : SUCCEED);

	DBfree_result(result);

	trx_free(table_name_esc);
	trx_free(index_name_esc);

	return ret;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: DBselect_uint64                                                  *
 *                                                                            *
 * Parameters: sql - [IN] sql statement                                       *
 *             ids - [OUT] sorted list of selected uint64 values              *
 *                                                                            *
 ******************************************************************************/
void	DBselect_uint64(const char *sql, trx_vector_uint64_t *ids)
{
	DB_RESULT	result;
	DB_ROW		row;
	trx_uint64_t	id;

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(id, row[0]);

		trx_vector_uint64_append(ids, id);
	}
	DBfree_result(result);

	trx_vector_uint64_sort(ids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
}

int	DBexecute_multiple_query(const char *query, const char *field_name, trx_vector_uint64_t *ids)
{
#define TRX_MAX_IDS	950
	char	*sql = NULL;
	size_t	sql_alloc = TRX_KIBIBYTE, sql_offset = 0;
	int	i, ret = SUCCEED;

	sql = (char *)trx_malloc(sql, sql_alloc);

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	for (i = 0; i < ids->values_num; i += TRX_MAX_IDS)
	{
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, query);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, field_name,
				&ids->values[i], MIN(TRX_MAX_IDS, ids->values_num - i));
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");

		if (SUCCEED != (ret = DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset)))
			break;
	}

	if (SUCCEED == ret && sql_offset > 16)	/* in ORACLE always present begin..end; */
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (TRX_DB_OK > DBexecute("%s", sql))
			ret = FAIL;
	}

	trx_free(sql);

	return ret;
}

#ifdef HAVE_ORACLE
/******************************************************************************
 *                                                                            *
 * Function: trx_db_format_values                                             *
 *                                                                            *
 * Purpose: format bulk operation (insert, update) value list                 *
 *                                                                            *
 * Parameters: fields     - [IN] the field list                               *
 *             values     - [IN] the corresponding value list                 *
 *             values_num - [IN] the number of values to format               *
 *                                                                            *
 * Return value: the formatted value list <value1>,<value2>...                *
 *                                                                            *
 * Comments: The returned string is allocated by this function and must be    *
 *           freed by the caller later.                                       *
 *                                                                            *
 ******************************************************************************/
static char	*trx_db_format_values(TRX_FIELD **fields, const trx_db_value_t *values, int values_num)
{
	int	i;
	char	*str = NULL;
	size_t	str_alloc = 0, str_offset = 0;

	for (i = 0; i < values_num; i++)
	{
		TRX_FIELD		*field = fields[i];
		const trx_db_value_t	*value = &values[i];

		if (0 < i)
			trx_chrcpy_alloc(&str, &str_alloc, &str_offset, ',');

		switch (field->type)
		{
			case TRX_TYPE_CHAR:
			case TRX_TYPE_TEXT:
			case TRX_TYPE_SHORTTEXT:
			case TRX_TYPE_LONGTEXT:
				trx_snprintf_alloc(&str, &str_alloc, &str_offset, "'%s'", value->str);
				break;
			case TRX_TYPE_FLOAT:
				trx_snprintf_alloc(&str, &str_alloc, &str_offset, TRX_FS_DBL, value->dbl);
				break;
			case TRX_TYPE_ID:
			case TRX_TYPE_UINT:
				trx_snprintf_alloc(&str, &str_alloc, &str_offset, TRX_FS_UI64, value->ui64);
				break;
			case TRX_TYPE_INT:
				trx_snprintf_alloc(&str, &str_alloc, &str_offset, "%d", value->i32);
				break;
			default:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "(unknown type)");
				break;
		}
	}

	return str;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_db_insert_clean                                              *
 *                                                                            *
 * Purpose: releases resources allocated by bulk insert operations            *
 *                                                                            *
 * Parameters: self        - [IN] the bulk insert data                        *
 *                                                                            *
 ******************************************************************************/
void	trx_db_insert_clean(trx_db_insert_t *self)
{
	int	i, j;

	for (i = 0; i < self->rows.values_num; i++)
	{
		trx_db_value_t	*row = (trx_db_value_t *)self->rows.values[i];

		for (j = 0; j < self->fields.values_num; j++)
		{
			TRX_FIELD	*field = (TRX_FIELD *)self->fields.values[j];

			switch (field->type)
			{
				case TRX_TYPE_CHAR:
				case TRX_TYPE_TEXT:
				case TRX_TYPE_SHORTTEXT:
				case TRX_TYPE_LONGTEXT:
					trx_free(row[j].str);
			}
		}

		trx_free(row);
	}

	trx_vector_ptr_destroy(&self->rows);

	trx_vector_ptr_destroy(&self->fields);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_insert_prepare_dyn                                        *
 *                                                                            *
 * Purpose: prepare for database bulk insert operation                        *
 *                                                                            *
 * Parameters: self        - [IN] the bulk insert data                        *
 *             table       - [IN] the target table name                       *
 *             fields      - [IN] names of the fields to insert               *
 *             fields_num  - [IN] the number of items in fields array         *
 *                                                                            *
 * Comments: The operation fails if the target table does not have the        *
 *           specified fields defined in its schema.                          *
 *                                                                            *
 *           Usage example:                                                   *
 *             trx_db_insert_t ins;                                           *
 *                                                                            *
 *             trx_db_insert_prepare(&ins, "history", "id", "value");         *
 *             trx_db_insert_add_values(&ins, (trx_uint64_t)1, 1.0);          *
 *             trx_db_insert_add_values(&ins, (trx_uint64_t)2, 2.0);          *
 *               ...                                                          *
 *             trx_db_insert_execute(&ins);                                   *
 *             trx_db_insert_clean(&ins);                                     *
 *                                                                            *
 ******************************************************************************/
void	trx_db_insert_prepare_dyn(trx_db_insert_t *self, const TRX_TABLE *table, const TRX_FIELD **fields, int fields_num)
{
	int	i;

	if (0 == fields_num)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	self->autoincrement = -1;

	trx_vector_ptr_create(&self->fields);
	trx_vector_ptr_create(&self->rows);

	self->table = table;

	for (i = 0; i < fields_num; i++)
		trx_vector_ptr_append(&self->fields, (TRX_FIELD *)fields[i]);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_insert_prepare                                            *
 *                                                                            *
 * Purpose: prepare for database bulk insert operation                        *
 *                                                                            *
 * Parameters: self  - [IN] the bulk insert data                              *
 *             table - [IN] the target table name                             *
 *             ...   - [IN] names of the fields to insert                     *
 *             NULL  - [IN] terminating NULL pointer                          *
 *                                                                            *
 * Comments: This is a convenience wrapper for trx_db_insert_prepare_dyn()    *
 *           function.                                                        *
 *                                                                            *
 ******************************************************************************/
void	trx_db_insert_prepare(trx_db_insert_t *self, const char *table, ...)
{
	trx_vector_ptr_t	fields;
	va_list			args;
	char			*field;
	const TRX_TABLE		*ptable;
	const TRX_FIELD		*pfield;

	/* find the table and fields in database schema */
	if (NULL == (ptable = DBget_table(table)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	va_start(args, table);

	trx_vector_ptr_create(&fields);

	while (NULL != (field = va_arg(args, char *)))
	{
		if (NULL == (pfield = DBget_field(ptable, field)))
		{
			treegix_log(LOG_LEVEL_ERR, "Cannot locate table \"%s\" field \"%s\" in database schema",
					table, field);
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}
		trx_vector_ptr_append(&fields, (TRX_FIELD *)pfield);
	}

	va_end(args);

	trx_db_insert_prepare_dyn(self, ptable, (const TRX_FIELD **)fields.values, fields.values_num);

	trx_vector_ptr_destroy(&fields);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_insert_add_values_dyn                                     *
 *                                                                            *
 * Purpose: adds row values for database bulk insert operation                *
 *                                                                            *
 * Parameters: self        - [IN] the bulk insert data                        *
 *             values      - [IN] the values to insert                        *
 *             fields_num  - [IN] the number of items in values array         *
 *                                                                            *
 * Comments: The values must be listed in the same order as the field names   *
 *           for insert preparation functions.                                *
 *                                                                            *
 ******************************************************************************/
void	trx_db_insert_add_values_dyn(trx_db_insert_t *self, const trx_db_value_t **values, int values_num)
{
	int		i;
	trx_db_value_t	*row;

	if (values_num != self->fields.values_num)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	row = (trx_db_value_t *)trx_malloc(NULL, self->fields.values_num * sizeof(trx_db_value_t));

	for (i = 0; i < self->fields.values_num; i++)
	{
		TRX_FIELD		*field = (TRX_FIELD *)self->fields.values[i];
		const trx_db_value_t	*value = values[i];

		switch (field->type)
		{
			case TRX_TYPE_LONGTEXT:
			case TRX_TYPE_CHAR:
			case TRX_TYPE_TEXT:
			case TRX_TYPE_SHORTTEXT:
#ifdef HAVE_ORACLE
				row[i].str = DBdyn_escape_field_len(field, value->str, ESCAPE_SEQUENCE_OFF);
#else
				row[i].str = DBdyn_escape_field_len(field, value->str, ESCAPE_SEQUENCE_ON);
#endif
				break;
			default:
				row[i] = *value;
				break;
		}
	}

	trx_vector_ptr_append(&self->rows, row);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_insert_add_values                                         *
 *                                                                            *
 * Purpose: adds row values for database bulk insert operation                *
 *                                                                            *
 * Parameters: self - [IN] the bulk insert data                               *
 *             ...  - [IN] the values to insert                               *
 *                                                                            *
 * Comments: This is a convenience wrapper for trx_db_insert_add_values_dyn() *
 *           function.                                                        *
 *           Note that the types of the passed values must conform to the     *
 *           corresponding field types.                                       *
 *                                                                            *
 ******************************************************************************/
void	trx_db_insert_add_values(trx_db_insert_t *self, ...)
{
	trx_vector_ptr_t	values;
	va_list			args;
	int			i;
	TRX_FIELD		*field;
	trx_db_value_t		*value;

	va_start(args, self);

	trx_vector_ptr_create(&values);

	for (i = 0; i < self->fields.values_num; i++)
	{
		field = (TRX_FIELD *)self->fields.values[i];

		value = (trx_db_value_t *)trx_malloc(NULL, sizeof(trx_db_value_t));

		switch (field->type)
		{
			case TRX_TYPE_CHAR:
			case TRX_TYPE_TEXT:
			case TRX_TYPE_SHORTTEXT:
			case TRX_TYPE_LONGTEXT:
				value->str = va_arg(args, char *);
				break;
			case TRX_TYPE_INT:
				value->i32 = va_arg(args, int);
				break;
			case TRX_TYPE_FLOAT:
				value->dbl = va_arg(args, double);
				break;
			case TRX_TYPE_UINT:
			case TRX_TYPE_ID:
				value->ui64 = va_arg(args, trx_uint64_t);
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				exit(EXIT_FAILURE);
		}

		trx_vector_ptr_append(&values, value);
	}

	va_end(args);

	trx_db_insert_add_values_dyn(self, (const trx_db_value_t **)values.values, values.values_num);

	trx_vector_ptr_clear_ext(&values, trx_ptr_free);
	trx_vector_ptr_destroy(&values);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_insert_execute                                            *
 *                                                                            *
 * Purpose: executes the prepared database bulk insert operation              *
 *                                                                            *
 * Parameters: self - [IN] the bulk insert data                               *
 *                                                                            *
 * Return value: Returns SUCCEED if the operation completed successfully or   *
 *               FAIL otherwise.                                              *
 *                                                                            *
 ******************************************************************************/
int	trx_db_insert_execute(trx_db_insert_t *self)
{
	int		ret = FAIL, i, j;
	const TRX_FIELD	*field;
	char		*sql_command, delim[2] = {',', '('};
	size_t		sql_command_alloc = 512, sql_command_offset = 0;

#ifndef HAVE_ORACLE
	char		*sql;
	size_t		sql_alloc = 16 * TRX_KIBIBYTE, sql_offset = 0;

#	ifdef HAVE_MYSQL
	char		*sql_values = NULL;
	size_t		sql_values_alloc = 0, sql_values_offset = 0;
#	endif
#else
	trx_db_bind_context_t	*contexts;
	int			rc, tries = 0;
#endif

	if (0 == self->rows.values_num)
		return SUCCEED;

	/* process the auto increment field */
	if (-1 != self->autoincrement)
	{
		trx_uint64_t	id;

		id = DBget_maxid_num(self->table->table, self->rows.values_num);

		for (i = 0; i < self->rows.values_num; i++)
		{
			trx_db_value_t	*values = (trx_db_value_t *)self->rows.values[i];

			values[self->autoincrement].ui64 = id++;
		}
	}

#ifndef HAVE_ORACLE
	sql = (char *)trx_malloc(NULL, sql_alloc);
#endif
	sql_command = (char *)trx_malloc(NULL, sql_command_alloc);

	/* create sql insert statement command */

	trx_strcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, "insert into ");
	trx_strcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, self->table->table);
	trx_chrcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, ' ');

	for (i = 0; i < self->fields.values_num; i++)
	{
		field = (TRX_FIELD *)self->fields.values[i];

		trx_chrcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, delim[0 == i]);
		trx_strcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, field->name);
	}

#ifdef HAVE_MYSQL
	/* MySQL workaround - explicitly add missing text fields with '' default value */
	for (field = (const TRX_FIELD *)self->table->fields; NULL != field->name; field++)
	{
		switch (field->type)
		{
			case TRX_TYPE_BLOB:
			case TRX_TYPE_TEXT:
			case TRX_TYPE_SHORTTEXT:
			case TRX_TYPE_LONGTEXT:
				if (FAIL != trx_vector_ptr_search(&self->fields, (void *)field,
						TRX_DEFAULT_PTR_COMPARE_FUNC))
				{
					continue;
				}

				trx_chrcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, ',');
				trx_strcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, field->name);

				trx_strcpy_alloc(&sql_values, &sql_values_alloc, &sql_values_offset, ",''");
				break;
		}
	}
#endif
	trx_strcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, ") values ");

#ifdef HAVE_ORACLE
	for (i = 0; i < self->fields.values_num; i++)
	{
		trx_chrcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, delim[0 == i]);
		trx_snprintf_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, ":%d", i + 1);
	}
	trx_chrcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, ')');

	contexts = (trx_db_bind_context_t *)trx_malloc(NULL, sizeof(trx_db_bind_context_t) * self->fields.values_num);

retry_oracle:
	DBstatement_prepare(sql_command);

	for (j = 0; j < self->fields.values_num; j++)
	{
		field = (TRX_FIELD *)self->fields.values[j];

		if (TRX_DB_OK > trx_db_bind_parameter_dyn(&contexts[j], j, field->type,
				(trx_db_value_t **)self->rows.values, self->rows.values_num))
		{
			for (i = 0; i < j; i++)
				trx_db_clean_bind_context(&contexts[i]);

			goto out;
		}
	}

	if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		for (i = 0; i < self->rows.values_num; i++)
		{
			trx_db_value_t	*values = (trx_db_value_t *)self->rows.values[i];
			char	*str;

			str = trx_db_format_values((TRX_FIELD **)self->fields.values, values, self->fields.values_num);
			treegix_log(LOG_LEVEL_DEBUG, "insert [txnlev:%d] [%s]", trx_db_txn_level(), str);
			trx_free(str);
		}
	}

	rc = trx_db_statement_execute(self->rows.values_num);

	for (j = 0; j < self->fields.values_num; j++)
		trx_db_clean_bind_context(&contexts[j]);

	if (TRX_DB_DOWN == rc)
	{
		if (0 < tries++)
		{
			treegix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", TRX_DB_WAIT_DOWN);
			connection_failure = 1;
			sleep(TRX_DB_WAIT_DOWN);
		}

		DBclose();
		DBconnect(TRX_DB_CONNECT_NORMAL);

		goto retry_oracle;
	}

	ret = (TRX_DB_OK <= rc ? SUCCEED : FAIL);

#else
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	for (i = 0; i < self->rows.values_num; i++)
	{
		trx_db_value_t	*values = (trx_db_value_t *)self->rows.values[i];

#	ifdef HAVE_MULTIROW_INSERT
		if (16 > sql_offset)
			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, sql_command);
#	else
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, sql_command);
#	endif
		for (j = 0; j < self->fields.values_num; j++)
		{
			const trx_db_value_t	*value = &values[j];

			field = (const TRX_FIELD *)self->fields.values[j];

			trx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, delim[0 == j]);

			switch (field->type)
			{
				case TRX_TYPE_CHAR:
				case TRX_TYPE_TEXT:
				case TRX_TYPE_SHORTTEXT:
				case TRX_TYPE_LONGTEXT:
					trx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, '\'');
					trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, value->str);
					trx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, '\'');
					break;
				case TRX_TYPE_INT:
					trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%d", value->i32);
					break;
				case TRX_TYPE_FLOAT:
					trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, TRX_FS_DBL,
							value->dbl);
					break;
				case TRX_TYPE_UINT:
					trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, TRX_FS_UI64,
							value->ui64);
					break;
				case TRX_TYPE_ID:
					trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
							DBsql_id_ins(value->ui64));
					break;
				default:
					THIS_SHOULD_NEVER_HAPPEN;
					exit(EXIT_FAILURE);
			}
		}
#	ifdef HAVE_MYSQL
		if (NULL != sql_values)
			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, sql_values);
#	endif

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ")" TRX_ROW_DL);

		if (SUCCEED != (ret = DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset)))
			goto out;
	}

	if (16 < sql_offset)
	{
#	ifdef HAVE_MULTIROW_INSERT
		if (',' == sql[sql_offset - 1])
		{
			sql_offset--;
			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
		}
#	endif
		DBend_multiple_update(sql, sql_alloc, sql_offset);

		if (TRX_DB_OK > DBexecute("%s", sql))
			ret = FAIL;
	}
#endif

out:
	trx_free(sql_command);

#ifndef HAVE_ORACLE
	trx_free(sql);

#	ifdef HAVE_MYSQL
	trx_free(sql_values);
#	endif
#else
	trx_free(contexts);
#endif
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_insert_autoincrement                                      *
 *                                                                            *
 * Purpose: executes the prepared database bulk insert operation              *
 *                                                                            *
 * Parameters: self - [IN] the bulk insert data                               *
 *                                                                            *
 ******************************************************************************/
void	trx_db_insert_autoincrement(trx_db_insert_t *self, const char *field_name)
{
	int	i;

	for (i = 0; i < self->fields.values_num; i++)
	{
		TRX_FIELD	*field = (TRX_FIELD *)self->fields.values[i];

		if (TRX_TYPE_ID == field->type && 0 == strcmp(field_name, field->name))
		{
			self->autoincrement = i;
			return;
		}
	}

	THIS_SHOULD_NEVER_HAPPEN;
	exit(EXIT_FAILURE);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_get_database_type                                         *
 *                                                                            *
 * Purpose: determine is it a server or a proxy database                      *
 *                                                                            *
 * Return value: TRX_DB_SERVER - server database                              *
 *               TRX_DB_PROXY - proxy database                                *
 *               TRX_DB_UNKNOWN - an error occurred                           *
 *                                                                            *
 ******************************************************************************/
int	trx_db_get_database_type(void)
{
	const char	*result_string;
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = TRX_DB_UNKNOWN;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	DBconnect(TRX_DB_CONNECT_NORMAL);

	if (NULL == (result = DBselectN("select userid from users", 1)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "cannot select records from \"users\" table");
		goto out;
	}

	if (NULL != (row = DBfetch(result)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "there is at least 1 record in \"users\" table");
		ret = TRX_DB_SERVER;
	}
	else
	{
		treegix_log(LOG_LEVEL_DEBUG, "no records in \"users\" table");
		ret = TRX_DB_PROXY;
	}

	DBfree_result(result);
out:
	DBclose();

	switch (ret)
	{
		case TRX_DB_SERVER:
			result_string = "TRX_DB_SERVER";
			break;
		case TRX_DB_PROXY:
			result_string = "TRX_DB_PROXY";
			break;
		case TRX_DB_UNKNOWN:
			result_string = "TRX_DB_UNKNOWN";
			break;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, result_string);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBlock_record                                                    *
 *                                                                            *
 * Purpose: locks a record in a table by its primary key and an optional      *
 *          constraint field                                                  *
 *                                                                            *
 * Parameters: table     - [IN] the target table                              *
 *             id        - [IN] primary key value                             *
 *             add_field - [IN] additional constraint field name (optional)   *
 *             add_id    - [IN] constraint field value                        *
 *                                                                            *
 * Return value: SUCCEED - the record was successfully locked                 *
 *               FAIL    - the table does not contain the specified record    *
 *                                                                            *
 ******************************************************************************/
int	DBlock_record(const char *table, trx_uint64_t id, const char *add_field, trx_uint64_t add_id)
{
	DB_RESULT	result;
	const TRX_TABLE	*t;
	int		ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == trx_db_txn_level())
		treegix_log(LOG_LEVEL_DEBUG, "%s() called outside of transaction", __func__);

	t = DBget_table(table);

	if (NULL == add_field)
	{
		result = DBselect("select null from %s where %s=" TRX_FS_UI64 TRX_FOR_UPDATE, table, t->recid, id);
	}
	else
	{
		result = DBselect("select null from %s where %s=" TRX_FS_UI64 " and %s=" TRX_FS_UI64 TRX_FOR_UPDATE,
				table, t->recid, id, add_field, add_id);
	}

	if (NULL == DBfetch(result))
		ret = FAIL;
	else
		ret = SUCCEED;

	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBlock_records                                                   *
 *                                                                            *
 * Purpose: locks a records in a table by its primary key                     *
 *                                                                            *
 * Parameters: table     - [IN] the target table                              *
 *             ids       - [IN] primary key values                            *
 *                                                                            *
 * Return value: SUCCEED - one or more of the specified records were          *
 *                         successfully locked                                *
 *               FAIL    - the table does not contain any of the specified    *
 *                         records                                            *
 *                                                                            *
 ******************************************************************************/
int	DBlock_records(const char *table, const trx_vector_uint64_t *ids)
{
	DB_RESULT	result;
	const TRX_TABLE	*t;
	int		ret;
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == trx_db_txn_level())
		treegix_log(LOG_LEVEL_DEBUG, "%s() called outside of transaction", __func__);

	t = DBget_table(table);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select null from %s where", table);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, t->recid, ids->values, ids->values_num);

	result = DBselect("%s" TRX_FOR_UPDATE, sql);

	trx_free(sql);

	if (NULL == DBfetch(result))
		ret = FAIL;
	else
		ret = SUCCEED;

	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBlock_ids                                                       *
 *                                                                            *
 * Purpose: locks a records in a table by field name                          *
 *                                                                            *
 * Parameters: table      - [IN] the target table                             *
 *             field_name - [IN] field name                                   *
 *             ids        - [IN/OUT] IN - sorted array of IDs to lock         *
 *                                   OUT - resulting array of locked IDs      *
 *                                                                            *
 * Return value: SUCCEED - one or more of the specified records were          *
 *                         successfully locked                                *
 *               FAIL    - no records were locked                             *
 *                                                                            *
 ******************************************************************************/
int	DBlock_ids(const char *table_name, const char *field_name, trx_vector_uint64_t *ids)
{
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;
	trx_uint64_t	id;
	int		i;
	DB_RESULT	result;
	DB_ROW		row;

	if (0 == ids->values_num)
		return FAIL;

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select %s from %s where", field_name, table_name);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, field_name, ids->values, ids->values_num);
	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " order by %s" TRX_FOR_UPDATE, field_name);
	result = DBselect("%s", sql);
	trx_free(sql);

	for (i = 0; NULL != (row = DBfetch(result)); i++)
	{
		TRX_STR2UINT64(id, row[0]);

		while (id != ids->values[i])
			trx_vector_uint64_remove(ids, i);
	}
	DBfree_result(result);

	while (i != ids->values_num)
		trx_vector_uint64_remove_noorder(ids, i);

	return (0 != ids->values_num ? SUCCEED : FAIL);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_sql_add_host_availability                                    *
 *                                                                            *
 * Purpose: adds host availability update to sql statement                    *
 *                                                                            *
 * Parameters: sql        - [IN/OUT] the sql statement                        *
 *             sql_alloc  - [IN/OUT] the number of bytes allocated for sql    *
 *                                   statement                                *
 *             sql_offset - [IN/OUT] the number of bytes used in sql          *
 *                                   statement                                *
 *             ha           [IN] the host availability data                   *
 *                                                                            *
 ******************************************************************************/
int	trx_sql_add_host_availability(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const trx_host_availability_t *ha)
{
	const char	*field_prefix[TRX_AGENT_MAX] = {"", "snmp_", "ipmi_", "jmx_"};
	char		delim = ' ';
	int		i;

	if (FAIL == trx_host_availability_is_set(ha))
		return FAIL;

	trx_strcpy_alloc(sql, sql_alloc, sql_offset, "update hosts set");

	for (i = 0; i < TRX_AGENT_MAX; i++)
	{
		if (0 != (ha->agents[i].flags & TRX_FLAGS_AGENT_STATUS_AVAILABLE))
		{
			trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%c%savailable=%d", delim, field_prefix[i],
					(int)ha->agents[i].available);
			delim = ',';
		}

		if (0 != (ha->agents[i].flags & TRX_FLAGS_AGENT_STATUS_ERROR))
		{
			char	*error_esc;

			error_esc = DBdyn_escape_field("hosts", "error", ha->agents[i].error);
			trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%c%serror='%s'", delim, field_prefix[i],
					error_esc);
			trx_free(error_esc);
			delim = ',';
		}

		if (0 != (ha->agents[i].flags & TRX_FLAGS_AGENT_STATUS_ERRORS_FROM))
		{
			trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%c%serrors_from=%d", delim, field_prefix[i],
					ha->agents[i].errors_from);
			delim = ',';
		}

		if (0 != (ha->agents[i].flags & TRX_FLAGS_AGENT_STATUS_DISABLE_UNTIL))
		{
			trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%c%sdisable_until=%d", delim, field_prefix[i],
					ha->agents[i].disable_until);
			delim = ',';
		}
	}

	trx_snprintf_alloc(sql, sql_alloc, sql_offset, " where hostid=" TRX_FS_UI64, ha->hostid);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: DBget_user_by_active_session                                     *
 *                                                                            *
 * Purpose: validate that session is active and get associated user data      *
 *                                                                            *
 * Parameters: sessionid - [IN] the session id to validate                    *
 *             user      - [OUT] user information                             *
 *                                                                            *
 * Return value:  SUCCEED - session is active and user data was retrieved     *
 *                FAIL    - otherwise                                         *
 *                                                                            *
 ******************************************************************************/
int	DBget_user_by_active_session(const char *sessionid, trx_user_t *user)
{
	char		*sessionid_esc;
	int		ret = FAIL;
	DB_RESULT	result;
	DB_ROW		row;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() sessionid:%s", __func__, sessionid);

	sessionid_esc = DBdyn_escape_string(sessionid);

	if (NULL == (result = DBselect(
			"select u.userid,u.type"
				" from sessions s,users u"
			" where s.userid=u.userid"
				" and s.sessionid='%s'"
				" and s.status=%d",
			sessionid_esc, TRX_SESSION_ACTIVE)))
	{
		goto out;
	}

	if (NULL == (row = DBfetch(result)))
		goto out;

	TRX_STR2UINT64(user->userid, row[0]);
	user->type = atoi(row[1]);

	ret = SUCCEED;
out:
	DBfree_result(result);
	trx_free(sessionid_esc);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_mock_field_init                                           *
 *                                                                            *
 * Purpose: initializes mock field                                            *
 *                                                                            *
 * Parameters: field      - [OUT] the field data                              *
 *             field_type - [IN] the field type in database schema            *
 *             field_len  - [IN] the field size in database schema            *
 *                                                                            *
 ******************************************************************************/
void	trx_db_mock_field_init(trx_db_mock_field_t *field, int field_type, int field_len)
{
	switch (field_type)
	{
		case TRX_TYPE_CHAR:
#if defined(HAVE_ORACLE)
			field->chars_num = field_len;
			field->bytes_num = 4000;
#elif defined(HAVE_IBM_DB2)
			field->chars_num = -1;
			field->bytes_num = field_len;
#else
			field->chars_num = field_len;
			field->bytes_num = -1;
#endif
			return;
	}

	THIS_SHOULD_NEVER_HAPPEN;

	field->chars_num = 0;
	field->bytes_num = 0;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_mock_field_append                                         *
 *                                                                            *
 * Purpose: 'appends' text to the field, if successful the character/byte     *
 *           limits are updated                                               *
 *                                                                            *
 * Parameters: field - [IN/OUT] the mock field                                *
 *             text  - [IN] the text to append                                *
 *                                                                            *
 * Return value: SUCCEED - the field had enough space to append the text      *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	trx_db_mock_field_append(trx_db_mock_field_t *field, const char *text)
{
	int	bytes_num, chars_num;

	if (-1 != field->bytes_num)
	{
		bytes_num = strlen(text);
		if (bytes_num > field->bytes_num)
			return FAIL;
	}
	else
		bytes_num = 0;

	if (-1 != field->chars_num)
	{
		chars_num = trx_strlen_utf8(text);
		if (chars_num > field->chars_num)
			return FAIL;
	}
	else
		chars_num = 0;

	field->bytes_num -= bytes_num;
	field->chars_num -= chars_num;

	return SUCCEED;
}
