

#include "common.h"

#include "trxdb.h"

#if defined(HAVE_IBM_DB2)
#	include <sqlcli1.h>
#elif defined(HAVE_MYSQL)
#	include "mysql.h"
#	include "errmsg.h"
#	include "mysqld_error.h"
#elif defined(HAVE_ORACLE)
#	include "oci.h"
#elif defined(HAVE_POSTGRESQL)
#	include <libpq-fe.h>
#elif defined(HAVE_SQLITE3)
#	include <sqlite3.h>
#endif

#include "dbschema.h"
#include "log.h"
#if defined(HAVE_SQLITE3)
#	include "mutexs.h"
#endif

struct trx_db_result
{
#if defined(HAVE_IBM_DB2)
	SQLHANDLE	hstmt;
	SQLSMALLINT	nalloc;
	SQLSMALLINT	ncolumn;
	DB_ROW		values;
	DB_ROW		values_cli;
	SQLINTEGER	*values_len;
#elif defined(HAVE_MYSQL)
	MYSQL_RES	*result;
#elif defined(HAVE_ORACLE)
	OCIStmt		*stmthp;	/* the statement handle for select operations */
	int 		ncolumn;
	DB_ROW		values;
	ub4		*values_alloc;
	OCILobLocator	**clobs;
#elif defined(HAVE_POSTGRESQL)
	PGresult	*pg_result;
	int		row_num;
	int		fld_num;
	int		cursor;
	DB_ROW		values;
#elif defined(HAVE_SQLITE3)
	int		curow;
	char		**data;
	int		nrow;
	int		ncolumn;
	DB_ROW		values;
#endif
};

static int	txn_level = 0;	/* transaction level, nested transactions are not supported */
static int	txn_error = TRX_DB_OK;	/* failed transaction */
static int	txn_end_error = TRX_DB_OK;	/* transaction result */

static char	*last_db_strerror = NULL;	/* last database error message */

extern int	CONFIG_LOG_SLOW_QUERIES;

#if defined(HAVE_IBM_DB2)
typedef struct
{
	SQLHANDLE	henv;
	SQLHANDLE	hdbc;
}
trx_ibm_db2_handle_t;

static trx_ibm_db2_handle_t	ibm_db2;

static int	IBM_DB2server_status(void);
static int	trx_ibm_db2_success(SQLRETURN ret);
static int	trx_ibm_db2_success_ext(SQLRETURN ret);
static void	trx_ibm_db2_log_errors(SQLSMALLINT htype, SQLHANDLE hndl, trx_err_codes_t err, const char *context);
#elif defined(HAVE_MYSQL)
static MYSQL			*conn = NULL;
#elif defined(HAVE_ORACLE)
#include "trxalgo.h"

typedef struct
{
	OCIEnv			*envhp;
	OCIError		*errhp;
	OCISvcCtx		*svchp;
	OCIServer		*srvhp;
	OCIStmt			*stmthp;	/* the statement handle for execute operations */
	trx_vector_ptr_t	db_results;
}
trx_oracle_db_handle_t;

static trx_oracle_db_handle_t	oracle;

static ub4	OCI_DBserver_status(void);

#elif defined(HAVE_POSTGRESQL)
static PGconn			*conn = NULL;
static unsigned int		TRX_PG_BYTEAOID = 0;
static int			TRX_PG_SVERSION = 0;
char				TRX_PG_ESCAPE_BACKSLASH = 1;
#elif defined(HAVE_SQLITE3)
static sqlite3			*conn = NULL;
static trx_mutex_t		sqlite_access = TRX_MUTEX_NULL;
#endif

#if defined(HAVE_ORACLE)
static void	OCI_DBclean_result(DB_RESULT result);
#endif

static void	trx_db_errlog(trx_err_codes_t trx_errno, int db_errno, const char *db_error, const char *context)
{
	char	*s;

	if (NULL != db_error)
		last_db_strerror = trx_strdup(last_db_strerror, db_error);
	else
		last_db_strerror = trx_strdup(last_db_strerror, "");

	switch (trx_errno)
	{
		case ERR_Z3001:
			s = trx_dsprintf(NULL, "connection to database '%s' failed: [%d] %s", context, db_errno,
					last_db_strerror);
			break;
		case ERR_Z3002:
			s = trx_dsprintf(NULL, "cannot create database '%s': [%d] %s", context, db_errno,
					last_db_strerror);
			break;
		case ERR_Z3003:
			s = trx_strdup(NULL, "no connection to the database");
			break;
		case ERR_Z3004:
			s = trx_dsprintf(NULL, "cannot close database: [%d] %s", db_errno, last_db_strerror);
			break;
		case ERR_Z3005:
			s = trx_dsprintf(NULL, "query failed: [%d] %s [%s]", db_errno, last_db_strerror, context);
			break;
		case ERR_Z3006:
			s = trx_dsprintf(NULL, "fetch failed: [%d] %s", db_errno, last_db_strerror);
			break;
		case ERR_Z3007:
			s = trx_dsprintf(NULL, "query failed: [%d] %s", db_errno, last_db_strerror);
			break;
		default:
			s = trx_strdup(NULL, "unknown error");
	}

	treegix_log(LOG_LEVEL_ERR, "[Z%04d] %s", (int)trx_errno, s);

	trx_free(s);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_last_strerr                                               *
 *                                                                            *
 * Purpose: get last error set by database                                    *
 *                                                                            *
 * Return value: last database error message                                  *
 *                                                                            *
 ******************************************************************************/

const char	*trx_db_last_strerr(void)
{
	return last_db_strerror;
}

#if defined(HAVE_ORACLE)
static const char	*trx_oci_error(sword status, sb4 *err)
{
	static char	errbuf[512];
	sb4		errcode, *perrcode;

	perrcode = (NULL == err ? &errcode : err);

	errbuf[0] = '\0';
	*perrcode = 0;

	switch (status)
	{
		case OCI_SUCCESS_WITH_INFO:
			OCIErrorGet((dvoid *)oracle.errhp, (ub4)1, (text *)NULL, perrcode,
					(text *)errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR);
			break;
		case OCI_NEED_DATA:
			trx_snprintf(errbuf, sizeof(errbuf), "%s", "OCI_NEED_DATA");
			break;
		case OCI_NO_DATA:
			trx_snprintf(errbuf, sizeof(errbuf), "%s", "OCI_NODATA");
			break;
		case OCI_ERROR:
			OCIErrorGet((dvoid *)oracle.errhp, (ub4)1, (text *)NULL, perrcode,
					(text *)errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR);
			break;
		case OCI_INVALID_HANDLE:
			trx_snprintf(errbuf, sizeof(errbuf), "%s", "OCI_INVALID_HANDLE");
			break;
		case OCI_STILL_EXECUTING:
			trx_snprintf(errbuf, sizeof(errbuf), "%s", "OCI_STILL_EXECUTING");
			break;
		case OCI_CONTINUE:
			trx_snprintf(errbuf, sizeof(errbuf), "%s", "OCI_CONTINUE");
			break;
	}

	trx_rtrim(errbuf, TRX_WHITESPACE);

	return errbuf;
}

/******************************************************************************
 *                                                                            *
 * Function: OCI_handle_sql_error                                             *
 *                                                                            *
 * Purpose: handles Oracle prepare/bind/execute/select operation error        *
 *                                                                            *
 * Parameters: zerrcode   - [IN] the Treegix errorcode for the failed database *
 *                               operation                                    *
 *             oci_error  - [IN] the return code from failed Oracle operation *
 *             sql        - [IN] the failed sql statement (can be NULL)       *
 *                                                                            *
 * Return value: TRX_DB_DOWN - database connection is down                    *
 *               TRX_DB_FAIL - otherwise                                      *
 *                                                                            *
 * Comments: This function logs the error description and checks the          *
 *           database connection status.                                      *
 *                                                                            *
 ******************************************************************************/
static int	OCI_handle_sql_error(int zerrcode, sword oci_error, const char *sql)
{
	sb4	errcode;
	int	ret = TRX_DB_DOWN;

	trx_db_errlog(zerrcode, oci_error, trx_oci_error(oci_error, &errcode), sql);

	/* after ORA-02396 (and consequent ORA-01012) errors the OCI_SERVER_NORMAL server status is still returned */
	switch (errcode)
	{
		case 1012:	/* ORA-01012: not logged on */
		case 2396:	/* ORA-02396: exceeded maximum idle time */
			goto out;
	}

	if (OCI_SERVER_NORMAL == OCI_DBserver_status())
		ret = TRX_DB_FAIL;
out:
	return ret;
}

#endif	/* HAVE_ORACLE */

#ifdef HAVE_POSTGRESQL
static void	trx_postgresql_error(char **error, const PGresult *pg_result)
{
	char	*result_error_msg;
	size_t	error_alloc = 0, error_offset = 0;

	trx_snprintf_alloc(error, &error_alloc, &error_offset, "%s", PQresStatus(PQresultStatus(pg_result)));

	result_error_msg = PQresultErrorMessage(pg_result);

	if ('\0' != *result_error_msg)
		trx_snprintf_alloc(error, &error_alloc, &error_offset, ":%s", result_error_msg);
}
#endif /*HAVE_POSTGRESQL*/

__trx_attr_format_printf(1, 2)
static int	trx_db_execute(const char *fmt, ...)
{
	va_list	args;
	int	ret;

	va_start(args, fmt);
	ret = trx_db_vexecute(fmt, args);
	va_end(args);

	return ret;
}

__trx_attr_format_printf(1, 2)
static DB_RESULT	trx_db_select(const char *fmt, ...)
{
	va_list		args;
	DB_RESULT	result;

	va_start(args, fmt);
	result = trx_db_vselect(fmt, args);
	va_end(args);

	return result;
}

#if defined(HAVE_MYSQL)
static int	is_recoverable_mysql_error(void)
{
	switch (mysql_errno(conn))
	{
		case CR_CONN_HOST_ERROR:
		case CR_SERVER_GONE_ERROR:
		case CR_CONNECTION_ERROR:
		case CR_SERVER_LOST:
		case CR_UNKNOWN_HOST:
		case CR_COMMANDS_OUT_OF_SYNC:
		case ER_SERVER_SHUTDOWN:
		case ER_ACCESS_DENIED_ERROR:		/* wrong user or password */
		case ER_ILLEGAL_GRANT_FOR_TABLE:	/* user without any privileges */
		case ER_TABLEACCESS_DENIED_ERROR:	/* user without some privilege */
		case ER_UNKNOWN_ERROR:
		case ER_UNKNOWN_COM_ERROR:
		case ER_LOCK_DEADLOCK:
		case ER_LOCK_WAIT_TIMEOUT:
#ifdef ER_CONNECTION_KILLED
		case ER_CONNECTION_KILLED:
#endif
			return SUCCEED;
	}

	return FAIL;
}
#elif defined(HAVE_POSTGRESQL)
static int	is_recoverable_postgresql_error(const PGconn *pg_conn, const PGresult *pg_result)
{
	if (CONNECTION_OK != PQstatus(pg_conn))
		return SUCCEED;

	if (0 == trx_strcmp_null(PQresultErrorField(pg_result, PG_DIAG_SQLSTATE), "40P01"))
		return SUCCEED;

	return FAIL;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_db_connect                                                   *
 *                                                                            *
 * Purpose: connect to the database                                           *
 *                                                                            *
 * Return value: TRX_DB_OK - successfully connected                           *
 *               TRX_DB_DOWN - database is down                               *
 *               TRX_DB_FAIL - failed to connect                              *
 *                                                                            *
 ******************************************************************************/
int	trx_db_connect(char *host, char *user, char *password, char *dbname, char *dbschema, char *dbsocket, int port)
{
	int		ret = TRX_DB_OK, last_txn_error, last_txn_level;
#if defined(HAVE_IBM_DB2)
	char		*connect = NULL;
#elif defined(HAVE_MYSQL)
#if LIBMYSQL_VERSION_ID >= 80000	/* my_bool type is removed in MySQL 8.0 */
	bool		mysql_reconnect = 1;
#else
	my_bool		mysql_reconnect = 1;
#endif
#elif defined(HAVE_ORACLE)
	char		*connect = NULL;
	sword		err = OCI_SUCCESS;
	static ub2	csid = 0;
#elif defined(HAVE_POSTGRESQL)
	int		rc;
	char		*cport = NULL;
	DB_RESULT	result;
	DB_ROW		row;
#elif defined(HAVE_SQLITE3)
	char		*p, *path = NULL;
#endif

#ifndef HAVE_MYSQL
	TRX_UNUSED(dbsocket);
#endif
	/* Allow executing statements during a connection initialization. Make sure to mark transaction as failed. */
	if (0 != txn_level)
		txn_error = TRX_DB_DOWN;

	last_txn_error = txn_error;
	last_txn_level = txn_level;

	txn_error = TRX_DB_OK;
	txn_level = 0;

#if defined(HAVE_IBM_DB2)
	connect = trx_strdup(connect, "PROTOCOL=TCPIP;");
	if ('\0' != *host)
		connect = trx_strdcatf(connect, "HOSTNAME=%s;", host);
	if (NULL != dbname && '\0' != *dbname)
		connect = trx_strdcatf(connect, "DATABASE=%s;", dbname);
	if (0 != port)
		connect = trx_strdcatf(connect, "PORT=%d;", port);
	if (NULL != user && '\0' != *user)
		connect = trx_strdcatf(connect, "UID=%s;", user);
	if (NULL != password && '\0' != *password)
		connect = trx_strdcatf(connect, "PWD=%s;", password);

	memset(&ibm_db2, 0, sizeof(ibm_db2));

	/* allocate an environment handle */
	if (SUCCEED != trx_ibm_db2_success(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &ibm_db2.henv)))
		ret = TRX_DB_FAIL;

	/* set attribute to enable application to run as ODBC 3.0 application; */
	/* recommended for pure IBM DB2 CLI, but not required */
	if (TRX_DB_OK == ret && SUCCEED != trx_ibm_db2_success(SQLSetEnvAttr(ibm_db2.henv, SQL_ATTR_ODBC_VERSION,
			(void *)SQL_OV_ODBC3, 0)))
	{
		ret = TRX_DB_FAIL;
	}

	/* allocate a database connection handle */
	if (TRX_DB_OK == ret && SUCCEED != trx_ibm_db2_success(SQLAllocHandle(SQL_HANDLE_DBC, ibm_db2.henv,
			&ibm_db2.hdbc)))
	{
		ret = TRX_DB_FAIL;
	}

	/* set codepage to utf-8 */
	if (TRX_DB_OK == ret && SUCCEED != trx_ibm_db2_success(SQLSetConnectAttr(ibm_db2.hdbc, SQL_ATTR_CLIENT_CODEPAGE,
			(SQLPOINTER)(SQLUINTEGER)1208, SQL_IS_UINTEGER)))
	{
		ret = TRX_DB_FAIL;
	}

	/* connect to the database */
	if (TRX_DB_OK == ret && SUCCEED != trx_ibm_db2_success(SQLDriverConnect(ibm_db2.hdbc, NULL, (SQLCHAR *)connect,
			SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT)))
	{
		ret = TRX_DB_FAIL;
	}

	/* set autocommit on */
	if (TRX_DB_OK == ret && SUCCEED != trx_ibm_db2_success(SQLSetConnectAttr(ibm_db2.hdbc, SQL_ATTR_AUTOCOMMIT,
			(SQLPOINTER)SQL_AUTOCOMMIT_ON, SQL_NTS)))
	{
		ret = TRX_DB_DOWN;
	}

	/* we do not generate vendor escape clause sequences */
	if (TRX_DB_OK == ret && SUCCEED != trx_ibm_db2_success(SQLSetConnectAttr(ibm_db2.hdbc, SQL_ATTR_NOSCAN,
			(SQLPOINTER)SQL_NOSCAN_ON, SQL_NTS)))
	{
		ret = TRX_DB_DOWN;
	}

	/* set current schema */
	if (NULL != dbschema && '\0' != *dbschema && TRX_DB_OK == ret)
	{
		char	*dbschema_esc;

		dbschema_esc = trx_db_dyn_escape_string(dbschema, TRX_SIZE_T_MAX, TRX_SIZE_T_MAX, ESCAPE_SEQUENCE_ON);
		if (0 < (ret = trx_db_execute("set current schema='%s'", dbschema_esc)))
			ret = TRX_DB_OK;
		trx_free(dbschema_esc);
	}

	trx_free(connect);

	/* output error information */
	if (TRX_DB_OK != ret)
	{
		trx_ibm_db2_log_errors(SQL_HANDLE_ENV, ibm_db2.henv, ERR_Z3001, dbname);
		trx_ibm_db2_log_errors(SQL_HANDLE_DBC, ibm_db2.hdbc, ERR_Z3001, dbname);
	}
#elif defined(HAVE_MYSQL)
	TRX_UNUSED(dbschema);

	if (NULL == (conn = mysql_init(NULL)))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot allocate or initialize MYSQL database connection object");
		exit(EXIT_FAILURE);
	}

	if (NULL == mysql_real_connect(conn, host, user, password, dbname, port, dbsocket, CLIENT_MULTI_STATEMENTS))
	{
		trx_db_errlog(ERR_Z3001, mysql_errno(conn), mysql_error(conn), dbname);
		ret = TRX_DB_FAIL;
	}

	/* The RECONNECT option setting is placed here, AFTER the connection	*/
	/* is made, due to a bug in MySQL versions prior to 5.1.6 where it	*/
	/* reset the options value to the default, regardless of what it was	*/
	/* set to prior to the connection. MySQL allows changing connection	*/
	/* options on an open connection, so setting it here is safe.		*/

	if (TRX_DB_OK == ret && 0 != mysql_options(conn, MYSQL_OPT_RECONNECT, &mysql_reconnect))
		treegix_log(LOG_LEVEL_WARNING, "Cannot set MySQL reconnect option.");

	/* in contrast to "set names utf8" results of this call will survive auto-reconnects */
	if (TRX_DB_OK == ret && 0 != mysql_set_character_set(conn, "utf8"))
		treegix_log(LOG_LEVEL_WARNING, "cannot set MySQL character set to \"utf8\"");

	if (TRX_DB_OK == ret && 0 != mysql_autocommit(conn, 1))
	{
		trx_db_errlog(ERR_Z3001, mysql_errno(conn), mysql_error(conn), dbname);
		ret = TRX_DB_FAIL;
	}

	if (TRX_DB_OK == ret && 0 != mysql_select_db(conn, dbname))
	{
		trx_db_errlog(ERR_Z3001, mysql_errno(conn), mysql_error(conn), dbname);
		ret = TRX_DB_FAIL;
	}

	if (TRX_DB_FAIL == ret && SUCCEED == is_recoverable_mysql_error())
		ret = TRX_DB_DOWN;

#elif defined(HAVE_ORACLE)
	TRX_UNUSED(dbschema);

	memset(&oracle, 0, sizeof(oracle));

	trx_vector_ptr_create(&oracle.db_results);

	/* connection string format: [//]host[:port][/service name] */

	if ('\0' != *host)
	{
		connect = trx_strdcatf(connect, "//%s", host);
		if (0 != port)
			connect = trx_strdcatf(connect, ":%d", port);
		if (NULL != dbname && '\0' != *dbname)
			connect = trx_strdcatf(connect, "/%s", dbname);
	}
	else
		ret = TRX_DB_FAIL;

	while (TRX_DB_OK == ret)
	{
		/* initialize environment */
		if (OCI_SUCCESS == (err = OCIEnvNlsCreate((OCIEnv **)&oracle.envhp, (ub4)OCI_DEFAULT, (dvoid *)0,
				(dvoid * (*)(dvoid *,size_t))0, (dvoid * (*)(dvoid *, dvoid *, size_t))0,
				(void (*)(dvoid *, dvoid *))0, (size_t)0, (dvoid **)0, csid, csid)))
		{
			if (0 != csid)
				break;	/* environment with UTF8 character set successfully created */

			/* try to find out the id of UTF8 character set */
			if (0 == (csid = OCINlsCharSetNameToId(oracle.envhp, (const oratext *)"UTF8")))
			{
				treegix_log(LOG_LEVEL_WARNING, "Cannot find out the ID of \"UTF8\" character set."
						" Relying on current \"NLS_LANG\" settings.");
				break;	/* use default environment with character set derived from NLS_LANG */
			}

			/* get rid of this environment to create a better one on the next iteration */
			OCIHandleFree((dvoid *)oracle.envhp, OCI_HTYPE_ENV);
			oracle.envhp = NULL;
		}
		else
		{
			trx_db_errlog(ERR_Z3001, err, trx_oci_error(err, NULL), connect);
			ret = TRX_DB_FAIL;
		}
	}

	if (TRX_DB_OK == ret)
	{
		/* allocate an error handle */
		(void)OCIHandleAlloc((dvoid *)oracle.envhp, (dvoid **)&oracle.errhp, OCI_HTYPE_ERROR,
				(size_t)0, (dvoid **)0);

		/* get the session */
		err = OCILogon2(oracle.envhp, oracle.errhp, &oracle.svchp,
				(text *)user, (ub4)(NULL != user ? strlen(user) : 0),
				(text *)password, (ub4)(NULL != password ? strlen(password) : 0),
				(text *)connect, (ub4)strlen(connect),
				OCI_DEFAULT);

		switch (err)
		{
			case OCI_SUCCESS_WITH_INFO:
				treegix_log(LOG_LEVEL_WARNING, "%s", trx_oci_error(err, NULL));
				/* break; is not missing here */
			case OCI_SUCCESS:
				err = OCIAttrGet((void *)oracle.svchp, OCI_HTYPE_SVCCTX, (void *)&oracle.srvhp,
						(ub4 *)0, OCI_ATTR_SERVER, oracle.errhp);
		}

		if (OCI_SUCCESS != err)
		{
			trx_db_errlog(ERR_Z3001, err, trx_oci_error(err, NULL), connect);
			ret = TRX_DB_DOWN;
		}
	}

	if (TRX_DB_OK == ret)
	{
		/* initialize statement handle */
		err = OCIHandleAlloc((dvoid *)oracle.envhp, (dvoid **)&oracle.stmthp, OCI_HTYPE_STMT,
				(size_t)0, (dvoid **)0);

		if (OCI_SUCCESS != err)
		{
			trx_db_errlog(ERR_Z3001, err, trx_oci_error(err, NULL), connect);
			ret = TRX_DB_DOWN;
		}
	}

	if (TRX_DB_OK == ret)
	{
		if (0 < (ret = trx_db_execute("alter session set nls_numeric_characters='. '")))
			ret = TRX_DB_OK;
	}

	trx_free(connect);
#elif defined(HAVE_POSTGRESQL)
	if (0 != port)
		cport = trx_dsprintf(cport, "%d", port);

	conn = PQsetdbLogin(host, cport, NULL, NULL, dbname, user, password);

	trx_free(cport);

	/* check to see that the backend connection was successfully made */
	if (CONNECTION_OK != PQstatus(conn))
	{
		trx_db_errlog(ERR_Z3001, 0, PQerrorMessage(conn), dbname);
		ret = TRX_DB_DOWN;
		goto out;
	}

	if (NULL != dbschema && '\0' != *dbschema)
	{
		char	*dbschema_esc;

		dbschema_esc = trx_db_dyn_escape_string(dbschema, TRX_SIZE_T_MAX, TRX_SIZE_T_MAX, ESCAPE_SEQUENCE_ON);
		if (TRX_DB_DOWN == (rc = trx_db_execute("set schema '%s'", dbschema_esc)) || TRX_DB_FAIL == rc)
			ret = rc;
		trx_free(dbschema_esc);
	}

	if (TRX_DB_FAIL == ret || TRX_DB_DOWN == ret)
		goto out;

	result = trx_db_select("select oid from pg_type where typname='bytea'");

	if ((DB_RESULT)TRX_DB_DOWN == result || NULL == result)
	{
		ret = (NULL == result) ? TRX_DB_FAIL : TRX_DB_DOWN;
		goto out;
	}

	if (NULL != (row = trx_db_fetch(result)))
		TRX_PG_BYTEAOID = atoi(row[0]);
	DBfree_result(result);

	TRX_PG_SVERSION = PQserverVersion(conn);
	treegix_log(LOG_LEVEL_DEBUG, "PostgreSQL Server version: %d", TRX_PG_SVERSION);

	/* disable "nonstandard use of \' in a string literal" warning */
	if (0 < (ret = trx_db_execute("set escape_string_warning to off")))
		ret = TRX_DB_OK;

	if (TRX_DB_OK != ret)
		goto out;

	result = trx_db_select("show standard_conforming_strings");

	if ((DB_RESULT)TRX_DB_DOWN == result || NULL == result)
	{
		ret = (NULL == result) ? TRX_DB_FAIL : TRX_DB_DOWN;
		goto out;
	}

	if (NULL != (row = trx_db_fetch(result)))
		TRX_PG_ESCAPE_BACKSLASH = (0 == strcmp(row[0], "off"));
	DBfree_result(result);

	if (90000 <= TRX_PG_SVERSION)
	{
		/* change the output format for values of type bytea from hex (the default) to escape */
		if (0 < (ret = trx_db_execute("set bytea_output=escape")))
			ret = TRX_DB_OK;
	}
out:
#elif defined(HAVE_SQLITE3)
	TRX_UNUSED(host);
	TRX_UNUSED(user);
	TRX_UNUSED(password);
	TRX_UNUSED(dbschema);
	TRX_UNUSED(port);
#ifdef HAVE_FUNCTION_SQLITE3_OPEN_V2
	if (SQLITE_OK != sqlite3_open_v2(dbname, &conn, SQLITE_OPEN_READWRITE, NULL))
#else
	if (SQLITE_OK != sqlite3_open(dbname, &conn))
#endif
	{
		trx_db_errlog(ERR_Z3001, 0, sqlite3_errmsg(conn), dbname);
		ret = TRX_DB_DOWN;
		goto out;
	}

	/* do not return SQLITE_BUSY immediately, wait for N ms */
	sqlite3_busy_timeout(conn, SEC_PER_MIN * 1000);

	if (0 < (ret = trx_db_execute("pragma synchronous=0")))
		ret = TRX_DB_OK;

	if (TRX_DB_OK != ret)
		goto out;

	if (0 < (ret = trx_db_execute("pragma temp_store=2")))
		ret = TRX_DB_OK;

	if (TRX_DB_OK != ret)
		goto out;

	path = trx_strdup(NULL, dbname);

	if (NULL != (p = strrchr(path, '/')))
		*++p = '\0';
	else
		*path = '\0';

	if (0 < (ret = trx_db_execute("pragma temp_store_directory='%s'", path)))
		ret = TRX_DB_OK;

	trx_free(path);
out:
#endif	/* HAVE_SQLITE3 */
	if (TRX_DB_OK != ret)
		trx_db_close();

	txn_error = last_txn_error;
	txn_level = last_txn_level;

	return ret;
}

int	trx_db_init(const char *dbname, const char *const dbschema, char **error)
{
#ifdef HAVE_SQLITE3
	trx_stat_t	buf;

	if (0 != trx_stat(dbname, &buf))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot open database file \"%s\": %s", dbname, trx_strerror(errno));
		treegix_log(LOG_LEVEL_WARNING, "creating database ...");

		if (SQLITE_OK != sqlite3_open(dbname, &conn))
		{
			trx_db_errlog(ERR_Z3002, 0, sqlite3_errmsg(conn), dbname);
			*error = trx_strdup(*error, "cannot open database");
			return FAIL;
		}

		if (SUCCEED != trx_mutex_create(&sqlite_access, TRX_MUTEX_SQLITE3, error))
			return FAIL;

		trx_db_execute("%s", dbschema);
		trx_db_close();
		return SUCCEED;
	}

	return trx_mutex_create(&sqlite_access, TRX_MUTEX_SQLITE3, error);
#else	/* not HAVE_SQLITE3 */
	TRX_UNUSED(dbname);
	TRX_UNUSED(dbschema);
	TRX_UNUSED(error);

	return SUCCEED;
#endif	/* HAVE_SQLITE3 */
}

void	trx_db_deinit(void)
{
#ifdef HAVE_SQLITE3
	trx_mutex_destroy(&sqlite_access);
#endif
}

void	trx_db_close(void)
{
#if defined(HAVE_IBM_DB2)
	if (ibm_db2.hdbc)
	{
		SQLDisconnect(ibm_db2.hdbc);
		SQLFreeHandle(SQL_HANDLE_DBC, ibm_db2.hdbc);
	}

	if (ibm_db2.henv)
		SQLFreeHandle(SQL_HANDLE_ENV, ibm_db2.henv);

	memset(&ibm_db2, 0, sizeof(ibm_db2));
#elif defined(HAVE_MYSQL)
	if (NULL != conn)
	{
		mysql_close(conn);
		conn = NULL;
	}
#elif defined(HAVE_ORACLE)
	if (0 != oracle.db_results.values_num)
	{
		int	i;

		treegix_log(LOG_LEVEL_WARNING, "cannot process queries: database is closed");

		for (i = 0; i < oracle.db_results.values_num; i++)
		{
			/* deallocate all handles before environment is deallocated */
			OCI_DBclean_result(oracle.db_results.values[i]);
		}
	}

	/* deallocate statement handle */
	if (NULL != oracle.stmthp)
	{
		OCIHandleFree((dvoid *)oracle.stmthp, OCI_HTYPE_STMT);
		oracle.stmthp = NULL;
	}

	if (NULL != oracle.svchp)
	{
		OCILogoff(oracle.svchp, oracle.errhp);
		oracle.svchp = NULL;
	}

	if (NULL != oracle.errhp)
	{
		OCIHandleFree(oracle.errhp, OCI_HTYPE_ERROR);
		oracle.errhp = NULL;
	}

	if (NULL != oracle.srvhp)
	{
		OCIHandleFree(oracle.srvhp, OCI_HTYPE_SERVER);
		oracle.srvhp = NULL;
	}

	if (NULL != oracle.envhp)
	{
		/* delete the environment handle, which deallocates all other handles associated with it */
		OCIHandleFree((dvoid *)oracle.envhp, OCI_HTYPE_ENV);
		oracle.envhp = NULL;
	}

	trx_vector_ptr_destroy(&oracle.db_results);
#elif defined(HAVE_POSTGRESQL)
	if (NULL != conn)
	{
		PQfinish(conn);
		conn = NULL;
	}
#elif defined(HAVE_SQLITE3)
	if (NULL != conn)
	{
		sqlite3_close(conn);
		conn = NULL;
	}
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_begin                                                     *
 *                                                                            *
 * Purpose: start transaction                                                 *
 *                                                                            *
 * Comments: do nothing if DB does not support transactions                   *
 *                                                                            *
 ******************************************************************************/
int	trx_db_begin(void)
{
	int	rc = TRX_DB_OK;

	if (txn_level > 0)
	{
		treegix_log(LOG_LEVEL_CRIT, "ERROR: nested transaction detected. Please report it to Treegix Team.");
		assert(0);
	}

	txn_level++;

#if defined(HAVE_IBM_DB2)
	if (SUCCEED != trx_ibm_db2_success(SQLSetConnectAttr(ibm_db2.hdbc, SQL_ATTR_AUTOCOMMIT,
			(SQLPOINTER)SQL_AUTOCOMMIT_OFF, SQL_NTS)))
	{
		rc = TRX_DB_DOWN;
	}

	if (TRX_DB_OK == rc)
	{
		/* create savepoint for correct rollback on DB2 */
		if (0 <= (rc = trx_db_execute("savepoint trx_begin_savepoint unique on rollback retain cursors;")))
			rc = TRX_DB_OK;
	}

	if (TRX_DB_OK != rc)
	{
		trx_ibm_db2_log_errors(SQL_HANDLE_DBC, ibm_db2.hdbc, ERR_Z3005, "<begin>");
		rc = (SQL_CD_TRUE == IBM_DB2server_status() ? TRX_DB_FAIL : TRX_DB_DOWN);
	}

#elif defined(HAVE_MYSQL) || defined(HAVE_POSTGRESQL)
	rc = trx_db_execute("begin;");
#elif defined(HAVE_SQLITE3)
	trx_mutex_lock(sqlite_access);
	rc = trx_db_execute("begin;");
#endif

	if (TRX_DB_DOWN == rc)
		txn_level--;

	return rc;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_commit                                                    *
 *                                                                            *
 * Purpose: commit transaction                                                *
 *                                                                            *
 * Comments: do nothing if DB does not support transactions                   *
 *                                                                            *
 ******************************************************************************/
int	trx_db_commit(void)
{
	int	rc = TRX_DB_OK;
#ifdef HAVE_ORACLE
	sword	err;
#endif

	if (0 == txn_level)
	{
		treegix_log(LOG_LEVEL_CRIT, "ERROR: commit without transaction."
				" Please report it to Treegix Team.");
		assert(0);
	}

	if (TRX_DB_OK != txn_error)
		return TRX_DB_FAIL; /* commit called on failed transaction */

#if defined(HAVE_IBM_DB2)
	if (SUCCEED != trx_ibm_db2_success(SQLEndTran(SQL_HANDLE_DBC, ibm_db2.hdbc, SQL_COMMIT)))
		rc = TRX_DB_DOWN;
	if (SUCCEED != trx_ibm_db2_success(SQLSetConnectAttr(ibm_db2.hdbc, SQL_ATTR_AUTOCOMMIT,
			(SQLPOINTER)SQL_AUTOCOMMIT_ON, SQL_NTS)))
	{
		rc = TRX_DB_DOWN;
	}
	if (TRX_DB_OK != rc)
	{
		trx_ibm_db2_log_errors(SQL_HANDLE_DBC, ibm_db2.hdbc, ERR_Z3005, "<commit>");
		rc = (SQL_CD_TRUE == IBM_DB2server_status() ? TRX_DB_FAIL : TRX_DB_DOWN);
	}
#elif defined(HAVE_ORACLE)
	if (OCI_SUCCESS != (err = OCITransCommit(oracle.svchp, oracle.errhp, OCI_DEFAULT)))
		rc = OCI_handle_sql_error(ERR_Z3005, err, "commit failed");
#elif defined(HAVE_MYSQL) || defined(HAVE_POSTGRESQL) || defined(HAVE_SQLITE3)
	rc = trx_db_execute("commit;");
#endif

	if (TRX_DB_OK > rc) { /* commit failed */
		txn_error = rc;
		return rc;
	}

#ifdef HAVE_SQLITE3
	trx_mutex_unlock(sqlite_access);
#endif

	txn_level--;
	txn_end_error = TRX_DB_OK;

	return rc;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_rollback                                                  *
 *                                                                            *
 * Purpose: rollback transaction                                              *
 *                                                                            *
 * Comments: do nothing if DB does not support transactions                   *
 *                                                                            *
 ******************************************************************************/
int	trx_db_rollback(void)
{
	int	rc = TRX_DB_OK, last_txn_error;
#ifdef HAVE_ORACLE
	sword	err;
#endif

	if (0 == txn_level)
	{
		treegix_log(LOG_LEVEL_CRIT, "ERROR: rollback without transaction."
				" Please report it to Treegix Team.");
		assert(0);
	}

	last_txn_error = txn_error;

	/* allow rollback of failed transaction */
	txn_error = TRX_DB_OK;

#if defined(HAVE_IBM_DB2)

	/* Rollback to begin that is marked with savepoint. This move undo all transactions. */
	if (0 <= (rc = trx_db_execute("rollback to savepoint trx_begin_savepoint;")))
		rc = TRX_DB_OK;

	if (SUCCEED != trx_ibm_db2_success(SQLSetConnectAttr(ibm_db2.hdbc, SQL_ATTR_AUTOCOMMIT,
			(SQLPOINTER)SQL_AUTOCOMMIT_ON, SQL_NTS)))
	{
		rc = TRX_DB_DOWN;
	}

	if (TRX_DB_OK != rc)
	{
		trx_ibm_db2_log_errors(SQL_HANDLE_DBC, ibm_db2.hdbc, ERR_Z3005, "<rollback>");
		rc = (SQL_CD_TRUE == IBM_DB2server_status() ? TRX_DB_FAIL : TRX_DB_DOWN);
	}
#elif defined(HAVE_MYSQL) || defined(HAVE_POSTGRESQL)
	rc = trx_db_execute("rollback;");
#elif defined(HAVE_ORACLE)
	if (OCI_SUCCESS != (err = OCITransRollback(oracle.svchp, oracle.errhp, OCI_DEFAULT)))
		rc = OCI_handle_sql_error(ERR_Z3005, err, "rollback failed");
#elif defined(HAVE_SQLITE3)
	rc = trx_db_execute("rollback;");
	trx_mutex_unlock(sqlite_access);
#endif

	/* There is no way to recover from rollback errors, so there is no need to preserve transaction level / error. */
	txn_level = 0;
	txn_error = TRX_DB_OK;

	if (TRX_DB_FAIL == rc)
		txn_end_error = TRX_DB_FAIL;
	else
		txn_end_error = last_txn_error;	/* error that caused rollback */

	return rc;
}

int	trx_db_txn_level(void)
{
	return txn_level;
}

int	trx_db_txn_error(void)
{
	return txn_error;
}

int	trx_db_txn_end_error(void)
{
	return txn_end_error;
}

#ifdef HAVE_ORACLE
static sword	trx_oracle_statement_prepare(const char *sql)
{
	return OCIStmtPrepare(oracle.stmthp, oracle.errhp, (text *)sql, (ub4)strlen((char *)sql), (ub4)OCI_NTV_SYNTAX,
			(ub4)OCI_DEFAULT);
}

static sword	trx_oracle_statement_execute(ub4 iters, ub4 *nrows)
{
	sword	err;

	if (OCI_SUCCESS == (err = OCIStmtExecute(oracle.svchp, oracle.stmthp, oracle.errhp, iters, (ub4)0,
			(CONST OCISnapshot *)NULL, (OCISnapshot *)NULL,
			0 == txn_level ? OCI_COMMIT_ON_SUCCESS : OCI_DEFAULT)))
	{
		err = OCIAttrGet((void *)oracle.stmthp, OCI_HTYPE_STMT, nrows, (ub4 *)0, OCI_ATTR_ROW_COUNT,
				oracle.errhp);
	}

	return err;
}
#endif

#ifdef HAVE_ORACLE
int	trx_db_statement_prepare(const char *sql)
{
	sword	err;
	int	ret = TRX_DB_OK;

	if (0 == txn_level)
		treegix_log(LOG_LEVEL_DEBUG, "query without transaction detected");

	if (TRX_DB_OK != txn_error)
	{
		treegix_log(LOG_LEVEL_DEBUG, "ignoring query [txnlev:%d] within failed transaction", txn_level);
		return TRX_DB_FAIL;
	}

	treegix_log(LOG_LEVEL_DEBUG, "query [txnlev:%d] [%s]", txn_level, sql);

	if (OCI_SUCCESS != (err = trx_oracle_statement_prepare(sql)))
		ret = OCI_handle_sql_error(ERR_Z3005, err, sql);

	if (TRX_DB_FAIL == ret && 0 < txn_level)
	{
		treegix_log(LOG_LEVEL_DEBUG, "query [%s] failed, setting transaction as failed", sql);
		txn_error = TRX_DB_FAIL;
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: db_bind_dynamic_cb                                               *
 *                                                                            *
 * Purpose: callback function used by dynamic parameter binding               *
 *                                                                            *
 ******************************************************************************/
static sb4 db_bind_dynamic_cb(dvoid *ctxp, OCIBind *bindp, ub4 iter, ub4 index, dvoid **bufpp, ub4 *alenp, ub1 *piecep,
		dvoid **indpp)
{
	trx_db_bind_context_t	*context = (trx_db_bind_context_t *)ctxp;

	TRX_UNUSED(bindp);
	TRX_UNUSED(index);

	switch (context->type)
	{
		case TRX_TYPE_ID: /* handle 0 -> NULL conversion */
			if (0 == context->rows[iter][context->position].ui64)
			{
				*bufpp = NULL;
				*alenp = 0;
				break;
			}
			/* break; is not missing here */
		case TRX_TYPE_UINT:
			*bufpp = &((OCINumber *)context->data)[iter];
			*alenp = sizeof(OCINumber);
			break;
		case TRX_TYPE_INT:
			*bufpp = &context->rows[iter][context->position].i32;
			*alenp = sizeof(int);
			break;
		case TRX_TYPE_FLOAT:
			*bufpp = &context->rows[iter][context->position].dbl;
			*alenp = sizeof(double);
			break;
		case TRX_TYPE_CHAR:
		case TRX_TYPE_TEXT:
		case TRX_TYPE_SHORTTEXT:
		case TRX_TYPE_LONGTEXT:
			*bufpp = context->rows[iter][context->position].str;
			*alenp = ((size_t *)context->data)[iter];
			break;
		default:
			return FAIL;
	}

	*indpp = NULL;
	*piecep = OCI_ONE_PIECE;

	return OCI_CONTINUE;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_bind_parameter_dyn                                        *
 *                                                                            *
 * Purpose: performs dynamic parameter binding, converting value if necessary *
 *                                                                            *
 * Parameters: context  - [OUT] the bind context                              *
 *             position - [IN] the parameter position                         *
 *             type     - [IN] the parameter type (TRX_TYPE_* )               *
 *             rows     - [IN] the data to bind - array of rows,              *
 *                             each row being an array of columns             *
 *             rows_num - [IN] the number of rows in the data                 *
 *                                                                            *
 ******************************************************************************/
int	trx_db_bind_parameter_dyn(trx_db_bind_context_t *context, int position, unsigned char type,
		trx_db_value_t **rows, int rows_num)
{
	int		i, ret = TRX_DB_OK;
	size_t		*sizes;
	sword		err;
	OCINumber	*values;
	ub2		data_type;
	OCIBind		*bindhp = NULL;

	context->position = position;
	context->rows = rows;
	context->data = NULL;
	context->type = type;

	switch (type)
	{
		case TRX_TYPE_ID:
		case TRX_TYPE_UINT:
			values = (OCINumber *)trx_malloc(NULL, sizeof(OCINumber) * rows_num);

			for (i = 0; i < rows_num; i++)
			{
				err = OCINumberFromInt(oracle.errhp, &rows[i][position].ui64, sizeof(trx_uint64_t),
						OCI_NUMBER_UNSIGNED, &values[i]);

				if (OCI_SUCCESS != err)
				{
					ret = OCI_handle_sql_error(ERR_Z3007, err, NULL);
					goto out;
				}
			}

			context->data = (OCINumber *)values;
			context->size_max = sizeof(OCINumber);
			data_type = SQLT_VNU;
			break;
		case TRX_TYPE_INT:
			context->size_max = sizeof(int);
			data_type = SQLT_INT;
			break;
		case TRX_TYPE_FLOAT:
			context->size_max = sizeof(double);
			data_type = SQLT_FLT;
			break;
		case TRX_TYPE_CHAR:
		case TRX_TYPE_TEXT:
		case TRX_TYPE_SHORTTEXT:
		case TRX_TYPE_LONGTEXT:
			sizes = (size_t *)trx_malloc(NULL, sizeof(size_t) * rows_num);
			context->size_max = 0;

			for (i = 0; i < rows_num; i++)
			{
				sizes[i] = strlen(rows[i][position].str);

				if (sizes[i] > context->size_max)
					context->size_max = sizes[i];
			}

			context->data = sizes;
			data_type = SQLT_LNG;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
	}

	err = OCIBindByPos(oracle.stmthp, &bindhp, oracle.errhp, context->position + 1, NULL, context->size_max,
			data_type, NULL, NULL, NULL, 0, NULL, (ub4)OCI_DATA_AT_EXEC);

	if (OCI_SUCCESS != err)
	{
		ret = OCI_handle_sql_error(ERR_Z3007, err, NULL);

		if (TRX_DB_FAIL == ret && 0 < txn_level)
		{
			treegix_log(LOG_LEVEL_DEBUG, "query failed, setting transaction as failed");
			txn_error = TRX_DB_FAIL;
		}

		goto out;
	}

	err = OCIBindDynamic(bindhp, oracle.errhp, (dvoid *)context, db_bind_dynamic_cb, NULL, NULL);

	if (OCI_SUCCESS != err)
	{
		ret = OCI_handle_sql_error(ERR_Z3007, err, NULL);

		if (TRX_DB_FAIL == ret && 0 < txn_level)
		{
			treegix_log(LOG_LEVEL_DEBUG, "query failed, setting transaction as failed");
			txn_error = TRX_DB_FAIL;
		}

		goto out;
	}
out:
	if (ret != TRX_DB_OK)
		trx_db_clean_bind_context(context);

	return ret;
}

void	trx_db_clean_bind_context(trx_db_bind_context_t *context)
{
	trx_free(context->data);
}

int	trx_db_statement_execute(int iters)
{
	sword	err;
	ub4	nrows;
	int	ret;

	if (TRX_DB_OK != txn_error)
	{
		treegix_log(LOG_LEVEL_DEBUG, "ignoring query [txnlev:%d] within failed transaction", txn_level);
		ret = TRX_DB_FAIL;
		goto out;
	}

	if (OCI_SUCCESS != (err = trx_oracle_statement_execute(iters, &nrows)))
		ret = OCI_handle_sql_error(ERR_Z3007, err, NULL);
	else
		ret = (int)nrows;

	if (TRX_DB_FAIL == ret && 0 < txn_level)
	{
		treegix_log(LOG_LEVEL_DEBUG, "query failed, setting transaction as failed");
		txn_error = TRX_DB_FAIL;
	}
out:
	treegix_log(LOG_LEVEL_DEBUG, "%s():%d", __func__, ret);

	return ret;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_db_vexecute                                                  *
 *                                                                            *
 * Purpose: Execute SQL statement. For non-select statements only.            *
 *                                                                            *
 * Return value: TRX_DB_FAIL (on error) or TRX_DB_DOWN (on recoverable error) *
 *               or number of rows affected (on success)                      *
 *                                                                            *
 ******************************************************************************/
int	trx_db_vexecute(const char *fmt, va_list args)
{
	char	*sql = NULL;
	int	ret = TRX_DB_OK;
	double	sec = 0;
#if defined(HAVE_IBM_DB2)
	SQLHANDLE	hstmt = 0;
	SQLRETURN	ret1;
	SQLLEN		row1;
	SQLLEN		rows = 0;
#elif defined(HAVE_MYSQL)
	int		status;
#elif defined(HAVE_ORACLE)
	sword		err = OCI_SUCCESS;
#elif defined(HAVE_POSTGRESQL)
	PGresult	*result;
	char		*error = NULL;
#elif defined(HAVE_SQLITE3)
	int		err;
	char		*error = NULL;
#endif

	if (0 != CONFIG_LOG_SLOW_QUERIES)
		sec = trx_time();

	sql = trx_dvsprintf(sql, fmt, args);

	if (0 == txn_level)
		treegix_log(LOG_LEVEL_DEBUG, "query without transaction detected");

	if (TRX_DB_OK != txn_error)
	{
		treegix_log(LOG_LEVEL_DEBUG, "ignoring query [txnlev:%d] [%s] within failed transaction", txn_level, sql);
		ret = TRX_DB_FAIL;
		goto clean;
	}

	treegix_log(LOG_LEVEL_DEBUG, "query [txnlev:%d] [%s]", txn_level, sql);

#if defined(HAVE_IBM_DB2)
	/* allocate a statement handle */
	if (SUCCEED != trx_ibm_db2_success(SQLAllocHandle(SQL_HANDLE_STMT, ibm_db2.hdbc, &hstmt)))
		ret = TRX_DB_DOWN;

	/* directly execute the statement; returns SQL_NO_DATA_FOUND when no rows were affected */
  	if (TRX_DB_OK == ret && SUCCEED != trx_ibm_db2_success_ext(SQLExecDirect(hstmt, (SQLCHAR *)sql, SQL_NTS)))
		ret = TRX_DB_DOWN;

	/* get number of affected rows */
	if (TRX_DB_OK == ret && SUCCEED != trx_ibm_db2_success(SQLRowCount(hstmt, &rows)))
		ret = TRX_DB_DOWN;

	/* process other SQL statements in the batch */
	while (TRX_DB_OK == ret && SUCCEED == trx_ibm_db2_success(ret1 = SQLMoreResults(hstmt)))
	{
		if (SUCCEED != trx_ibm_db2_success(SQLRowCount(hstmt, &row1)))
			ret = TRX_DB_DOWN;
		else
			rows += row1;
	}

	if (TRX_DB_OK == ret && SQL_NO_DATA_FOUND != ret1)
		ret = TRX_DB_DOWN;

	if (TRX_DB_OK != ret)
	{
		trx_ibm_db2_log_errors(SQL_HANDLE_DBC, ibm_db2.hdbc, ERR_Z3005, sql);
		trx_ibm_db2_log_errors(SQL_HANDLE_STMT, hstmt, ERR_Z3005, sql);

		ret = (SQL_CD_TRUE == IBM_DB2server_status() ? TRX_DB_FAIL : TRX_DB_DOWN);
	}
	else if (0 <= rows)
	{
		ret = (int)rows;
	}

	if (hstmt)
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
#elif defined(HAVE_MYSQL)
	if (NULL == conn)
	{
		trx_db_errlog(ERR_Z3003, 0, NULL, NULL);
		ret = TRX_DB_FAIL;
	}
	else
	{
		if (0 != (status = mysql_query(conn, sql)))
		{
			trx_db_errlog(ERR_Z3005, mysql_errno(conn), mysql_error(conn), sql);

			ret = (SUCCEED == is_recoverable_mysql_error() ? TRX_DB_DOWN : TRX_DB_FAIL);
		}
		else
		{
			do
			{
				if (0 != mysql_field_count(conn))
				{
					treegix_log(LOG_LEVEL_DEBUG, "cannot retrieve result set");
					break;
				}

				ret += (int)mysql_affected_rows(conn);

				/* more results? 0 = yes (keep looping), -1 = no, >0 = error */
				if (0 < (status = mysql_next_result(conn)))
				{
					trx_db_errlog(ERR_Z3005, mysql_errno(conn), mysql_error(conn), sql);
					ret = (SUCCEED == is_recoverable_mysql_error() ? TRX_DB_DOWN : TRX_DB_FAIL);
				}
			}
			while (0 == status);
		}
	}
#elif defined(HAVE_ORACLE)
	if (OCI_SUCCESS == (err = trx_oracle_statement_prepare(sql)))
	{
		ub4	nrows = 0;

		if (OCI_SUCCESS == (err = trx_oracle_statement_execute(1, &nrows)))
			ret = (int)nrows;
	}

	if (OCI_SUCCESS != err)
		ret = OCI_handle_sql_error(ERR_Z3005, err, sql);

#elif defined(HAVE_POSTGRESQL)
	result = PQexec(conn,sql);

	if (NULL == result)
	{
		trx_db_errlog(ERR_Z3005, 0, "result is NULL", sql);
		ret = (CONNECTION_OK == PQstatus(conn) ? TRX_DB_FAIL : TRX_DB_DOWN);
	}
	else if (PGRES_COMMAND_OK != PQresultStatus(result))
	{
		trx_postgresql_error(&error, result);
		trx_db_errlog(ERR_Z3005, 0, error, sql);
		trx_free(error);

		ret = (SUCCEED == is_recoverable_postgresql_error(conn, result) ? TRX_DB_DOWN : TRX_DB_FAIL);
	}

	if (TRX_DB_OK == ret)
		ret = atoi(PQcmdTuples(result));

	PQclear(result);
#elif defined(HAVE_SQLITE3)
	if (0 == txn_level)
		trx_mutex_lock(sqlite_access);

lbl_exec:
	if (SQLITE_OK != (err = sqlite3_exec(conn, sql, NULL, 0, &error)))
	{
		if (SQLITE_BUSY == err)
			goto lbl_exec;

		trx_db_errlog(ERR_Z3005, 0, error, sql);
		sqlite3_free(error);

		switch (err)
		{
			case SQLITE_ERROR:	/* SQL error or missing database; assuming SQL error, because if we
						   are this far into execution, trx_db_connect() was successful */
			case SQLITE_NOMEM:	/* A malloc() failed */
			case SQLITE_TOOBIG:	/* String or BLOB exceeds size limit */
			case SQLITE_CONSTRAINT:	/* Abort due to constraint violation */
			case SQLITE_MISMATCH:	/* Data type mismatch */
				ret = TRX_DB_FAIL;
				break;
			default:
				ret = TRX_DB_DOWN;
				break;
		}
	}

	if (TRX_DB_OK == ret)
		ret = sqlite3_changes(conn);

	if (0 == txn_level)
		trx_mutex_unlock(sqlite_access);
#endif	/* HAVE_SQLITE3 */

	if (0 != CONFIG_LOG_SLOW_QUERIES)
	{
		sec = trx_time() - sec;
		if (sec > (double)CONFIG_LOG_SLOW_QUERIES / 1000.0)
			treegix_log(LOG_LEVEL_WARNING, "slow query: " TRX_FS_DBL " sec, \"%s\"", sec, sql);
	}

	if (TRX_DB_FAIL == ret && 0 < txn_level)
	{
		treegix_log(LOG_LEVEL_DEBUG, "query [%s] failed, setting transaction as failed", sql);
		txn_error = TRX_DB_FAIL;
	}
clean:
	trx_free(sql);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_vselect                                                   *
 *                                                                            *
 * Purpose: execute a select statement                                        *
 *                                                                            *
 * Return value: data, NULL (on error) or (DB_RESULT)TRX_DB_DOWN              *
 *                                                                            *
 ******************************************************************************/
DB_RESULT	trx_db_vselect(const char *fmt, va_list args)
{
	char		*sql = NULL;
	DB_RESULT	result = NULL;
	double		sec = 0;
#if defined(HAVE_IBM_DB2)
	int		i;
	SQLRETURN	ret = SQL_SUCCESS;
#elif defined(HAVE_ORACLE)
	sword		err = OCI_SUCCESS;
	ub4		prefetch_rows = 200, counter;
#elif defined(HAVE_POSTGRESQL)
	char		*error = NULL;
#elif defined(HAVE_SQLITE3)
	int		ret = FAIL;
	char		*error = NULL;
#endif

	if (0 != CONFIG_LOG_SLOW_QUERIES)
		sec = trx_time();

	sql = trx_dvsprintf(sql, fmt, args);

	if (TRX_DB_OK != txn_error)
	{
		treegix_log(LOG_LEVEL_DEBUG, "ignoring query [txnlev:%d] [%s] within failed transaction", txn_level, sql);
		goto clean;
	}

	treegix_log(LOG_LEVEL_DEBUG, "query [txnlev:%d] [%s]", txn_level, sql);

#if defined(HAVE_IBM_DB2)
	result = trx_malloc(result, sizeof(struct trx_db_result));
	memset(result, 0, sizeof(struct trx_db_result));

	/* allocate a statement handle */
	if (SUCCEED != trx_ibm_db2_success(ret = SQLAllocHandle(SQL_HANDLE_STMT, ibm_db2.hdbc, &result->hstmt)))
		goto error;

	/* directly execute the statement */
	if (SUCCEED != trx_ibm_db2_success(ret = SQLExecDirect(result->hstmt, (SQLCHAR *)sql, SQL_NTS)))
		goto error;

	/* identify the number of output columns */
	if (SUCCEED != trx_ibm_db2_success(ret = SQLNumResultCols(result->hstmt, &result->ncolumn)))
		goto error;

	if (0 == result->ncolumn)
		goto error;

	result->nalloc = 0;
	result->values = trx_malloc(result->values, sizeof(char *) * result->ncolumn);
	result->values_cli = trx_malloc(result->values_cli, sizeof(char *) * result->ncolumn);
	result->values_len = trx_malloc(result->values_len, sizeof(SQLINTEGER) * result->ncolumn);

	for (i = 0; i < result->ncolumn; i++)
	{
		/* get the display size for a column */
		if (SUCCEED != trx_ibm_db2_success(ret = SQLColAttribute(result->hstmt, (SQLSMALLINT)(i + 1),
				SQL_DESC_DISPLAY_SIZE, NULL, 0, NULL, &result->values_len[i])))
		{
			goto error;
		}

		result->values_len[i] += 1; /* '\0'; */

		/* allocate memory to bind a column */
		result->values_cli[i] = trx_malloc(NULL, result->values_len[i]);
		result->nalloc++;

		/* bind columns to program variables, converting all types to CHAR */
		if (SUCCEED != trx_ibm_db2_success(ret = SQLBindCol(result->hstmt, (SQLSMALLINT)(i + 1),
				SQL_C_CHAR, result->values_cli[i], result->values_len[i], &result->values_len[i])))
		{
			goto error;
		}
	}
error:
	if (SUCCEED != trx_ibm_db2_success(ret) || 0 == result->ncolumn)
	{
		trx_ibm_db2_log_errors(SQL_HANDLE_DBC, ibm_db2.hdbc, ERR_Z3005, sql);
		trx_ibm_db2_log_errors(SQL_HANDLE_STMT, result->hstmt, ERR_Z3005, sql);

		DBfree_result(result);

		result = (SQL_CD_TRUE == IBM_DB2server_status() ? NULL : (DB_RESULT)TRX_DB_DOWN);
	}
#elif defined(HAVE_MYSQL)
	result = (DB_RESULT)trx_malloc(NULL, sizeof(struct trx_db_result));
	result->result = NULL;

	if (NULL == conn)
	{
		trx_db_errlog(ERR_Z3003, 0, NULL, NULL);

		DBfree_result(result);
		result = NULL;
	}
	else
	{
		if (0 != mysql_query(conn, sql) || NULL == (result->result = mysql_store_result(conn)))
		{
			trx_db_errlog(ERR_Z3005, mysql_errno(conn), mysql_error(conn), sql);

			DBfree_result(result);
			result = (SUCCEED == is_recoverable_mysql_error() ? (DB_RESULT)TRX_DB_DOWN : NULL);
		}
	}
#elif defined(HAVE_ORACLE)
	result = trx_malloc(NULL, sizeof(struct trx_db_result));
	memset(result, 0, sizeof(struct trx_db_result));
	trx_vector_ptr_append(&oracle.db_results, result);

	err = OCIHandleAlloc((dvoid *)oracle.envhp, (dvoid **)&result->stmthp, OCI_HTYPE_STMT, (size_t)0, (dvoid **)0);

	/* Prefetching when working with Oracle is needed because otherwise it fetches only 1 row at a time when doing */
	/* selects (default behavior). There are 2 ways to do prefetching: memory based and rows based. Based on the   */
	/* study optimal (speed-wise) memory based prefetch is 2 MB. But in case of many subsequent selects CPU usage  */
	/* jumps up to 100 %. Using rows prefetch with up to 200 rows does not affect CPU usage, it is the same as     */
	/* without prefetching at all. See TRX-5920, TRX-6493 for details.                                             */
	/*                                                                                                             */
	/* Tested on Oracle 11gR2.                                                                                     */
	/*                                                                                                             */
	/* Oracle info: docs.oracle.com/cd/B28359_01/appdev.111/b28395/oci04sql.htm                                    */

	if (OCI_SUCCESS == err)
	{
		err = OCIAttrSet(result->stmthp, OCI_HTYPE_STMT, &prefetch_rows, sizeof(prefetch_rows),
				OCI_ATTR_PREFETCH_ROWS, oracle.errhp);
	}

	if (OCI_SUCCESS == err)
	{
		err = OCIStmtPrepare(result->stmthp, oracle.errhp, (text *)sql, (ub4)strlen((char *)sql),
				(ub4)OCI_NTV_SYNTAX, (ub4)OCI_DEFAULT);
	}

	if (OCI_SUCCESS == err)
	{
		err = OCIStmtExecute(oracle.svchp, result->stmthp, oracle.errhp, (ub4)0, (ub4)0,
				(CONST OCISnapshot *)NULL, (OCISnapshot *)NULL,
				0 == txn_level ? OCI_COMMIT_ON_SUCCESS : OCI_DEFAULT);
	}

	if (OCI_SUCCESS == err)
	{
		/* get the number of columns in the query */
		err = OCIAttrGet((void *)result->stmthp, OCI_HTYPE_STMT, (void *)&result->ncolumn,
				  (ub4 *)0, OCI_ATTR_PARAM_COUNT, oracle.errhp);
	}

	if (OCI_SUCCESS != err)
		goto error;

	assert(0 < result->ncolumn);

	result->values = trx_malloc(NULL, result->ncolumn * sizeof(char *));
	result->clobs = trx_malloc(NULL, result->ncolumn * sizeof(OCILobLocator *));
	result->values_alloc = trx_malloc(NULL, result->ncolumn * sizeof(ub4));
	memset(result->values, 0, result->ncolumn * sizeof(char *));
	memset(result->clobs, 0, result->ncolumn * sizeof(OCILobLocator *));
	memset(result->values_alloc, 0, result->ncolumn * sizeof(ub4));

	for (counter = 1; OCI_SUCCESS == err && counter <= (ub4)result->ncolumn; counter++)
	{
		OCIParam	*parmdp = NULL;
		OCIDefine	*defnp = NULL;
		ub4		char_semantics;
		ub2		col_width = 0, data_type = 0;

		/* request a parameter descriptor in the select-list */
		err = OCIParamGet((void *)result->stmthp, OCI_HTYPE_STMT, oracle.errhp, (void **)&parmdp, (ub4)counter);

		if (OCI_SUCCESS == err)
		{
			/* retrieve the data type for the column */
			err = OCIAttrGet((void *)parmdp, OCI_DTYPE_PARAM, (dvoid *)&data_type, (ub4 *)NULL,
					(ub4)OCI_ATTR_DATA_TYPE, (OCIError *)oracle.errhp);
		}

		if (SQLT_CLOB == data_type)
		{
			if (OCI_SUCCESS == err)
			{
				/* allocate the lob locator variable */
				err = OCIDescriptorAlloc((dvoid *)oracle.envhp, (dvoid **)&result->clobs[counter - 1],
						OCI_DTYPE_LOB, (size_t)0, (dvoid **)0);
			}

			if (OCI_SUCCESS == err)
			{
				/* associate clob var with its define handle */
				err = OCIDefineByPos((void *)result->stmthp, &defnp, (OCIError *)oracle.errhp,
						(ub4)counter, (dvoid *)&result->clobs[counter - 1], (sb4)-1,
						data_type, (dvoid *)0, (ub2 *)0, (ub2 *)0, (ub4)OCI_DEFAULT);
			}
		}
		else
		{
			if (OCI_SUCCESS == err)
			{
				/* retrieve the length semantics for the column */
				char_semantics = 0;
				err = OCIAttrGet((void *)parmdp, (ub4)OCI_DTYPE_PARAM, (void *)&char_semantics,
						(ub4 *)NULL, (ub4)OCI_ATTR_CHAR_USED, (OCIError *)oracle.errhp);
			}

			if (OCI_SUCCESS == err)
			{
				if (0 != char_semantics)
				{
					/* retrieve the column width in characters */
					err = OCIAttrGet((void *)parmdp, (ub4)OCI_DTYPE_PARAM, (void *)&col_width,
							(ub4 *)NULL, (ub4)OCI_ATTR_CHAR_SIZE, (OCIError *)oracle.errhp);

					/* adjust for UTF-8 */
					col_width *= 4;
				}
				else
				{
					/* retrieve the column width in bytes */
					err = OCIAttrGet((void *)parmdp, (ub4)OCI_DTYPE_PARAM, (void *)&col_width,
							(ub4 *)NULL, (ub4)OCI_ATTR_DATA_SIZE, (OCIError *)oracle.errhp);
				}
			}
			col_width++;	/* add 1 byte for terminating '\0' */

			result->values_alloc[counter - 1] = col_width;
			result->values[counter - 1] = trx_malloc(NULL, col_width);
			*result->values[counter - 1] = '\0';

			if (OCI_SUCCESS == err)
			{
				/* represent any data as characters */
				err = OCIDefineByPos(result->stmthp, &defnp, oracle.errhp, counter,
						(dvoid *)result->values[counter - 1], col_width, SQLT_STR,
						(dvoid *)0, (ub2 *)0, (ub2 *)0, OCI_DEFAULT);
			}
		}

		/* free cell descriptor */
		OCIDescriptorFree(parmdp, OCI_DTYPE_PARAM);
		parmdp = NULL;
	}
error:
	if (OCI_SUCCESS != err)
	{
		int	server_status;

		server_status = OCI_handle_sql_error(ERR_Z3005, err, sql);
		DBfree_result(result);

		result = (TRX_DB_DOWN == server_status ? (DB_RESULT)(intptr_t)server_status : NULL);
	}
#elif defined(HAVE_POSTGRESQL)
	result = trx_malloc(NULL, sizeof(struct trx_db_result));
	result->pg_result = PQexec(conn, sql);
	result->values = NULL;
	result->cursor = 0;
	result->row_num = 0;

	if (NULL == result->pg_result)
		trx_db_errlog(ERR_Z3005, 0, "result is NULL", sql);

	if (PGRES_TUPLES_OK != PQresultStatus(result->pg_result))
	{
		trx_postgresql_error(&error, result->pg_result);
		trx_db_errlog(ERR_Z3005, 0, error, sql);
		trx_free(error);

		if (SUCCEED == is_recoverable_postgresql_error(conn, result->pg_result))
		{
			DBfree_result(result);
			result = (DB_RESULT)TRX_DB_DOWN;
		}
		else
		{
			DBfree_result(result);
			result = NULL;
		}
	}
	else	/* init rownum */
		result->row_num = PQntuples(result->pg_result);
#elif defined(HAVE_SQLITE3)
	if (0 == txn_level)
		trx_mutex_lock(sqlite_access);

	result = trx_malloc(NULL, sizeof(struct trx_db_result));
	result->curow = 0;

lbl_get_table:
	if (SQLITE_OK != (ret = sqlite3_get_table(conn,sql, &result->data, &result->nrow, &result->ncolumn, &error)))
	{
		if (SQLITE_BUSY == ret)
			goto lbl_get_table;

		trx_db_errlog(ERR_Z3005, 0, error, sql);
		sqlite3_free(error);

		DBfree_result(result);

		switch (ret)
		{
			case SQLITE_ERROR:	/* SQL error or missing database; assuming SQL error, because if we
						   are this far into execution, trx_db_connect() was successful */
			case SQLITE_NOMEM:	/* a malloc() failed */
			case SQLITE_MISMATCH:	/* data type mismatch */
				result = NULL;
				break;
			default:
				result = (DB_RESULT)TRX_DB_DOWN;
				break;
		}
	}

	if (0 == txn_level)
		trx_mutex_unlock(sqlite_access);
#endif	/* HAVE_SQLITE3 */
	if (0 != CONFIG_LOG_SLOW_QUERIES)
	{
		sec = trx_time() - sec;
		if (sec > (double)CONFIG_LOG_SLOW_QUERIES / 1000.0)
			treegix_log(LOG_LEVEL_WARNING, "slow query: " TRX_FS_DBL " sec, \"%s\"", sec, sql);
	}

	if (NULL == result && 0 < txn_level)
	{
		treegix_log(LOG_LEVEL_DEBUG, "query [%s] failed, setting transaction as failed", sql);
		txn_error = TRX_DB_FAIL;
	}
clean:
	trx_free(sql);

	return result;
}

/*
 * Execute SQL statement. For select statements only.
 */
DB_RESULT	trx_db_select_n(const char *query, int n)
{
#if defined(HAVE_IBM_DB2)
	return trx_db_select("%s fetch first %d rows only", query, n);
#elif defined(HAVE_MYSQL)
	return trx_db_select("%s limit %d", query, n);
#elif defined(HAVE_ORACLE)
	return trx_db_select("select * from (%s) where rownum<=%d", query, n);
#elif defined(HAVE_POSTGRESQL)
	return trx_db_select("%s limit %d", query, n);
#elif defined(HAVE_SQLITE3)
	return trx_db_select("%s limit %d", query, n);
#endif
}

#ifdef HAVE_POSTGRESQL
/******************************************************************************
 *                                                                            *
 * Function: trx_db_bytea_unescape                                            *
 *                                                                            *
 * Purpose: converts the null terminated string into binary buffer            *
 *                                                                            *
 * Transformations:                                                           *
 *      \ooo == a byte whose value = ooo (ooo is an octal number)             *
 *      \\   == \                                                             *
 *                                                                            *
 * Parameters:                                                                *
 *      io - [IN/OUT] null terminated string / binary data                    *
 *                                                                            *
 * Return value: length of the binary buffer                                  *
 *                                                                            *
 ******************************************************************************/
static size_t	trx_db_bytea_unescape(u_char *io)
{
	const u_char	*i = io;
	u_char		*o = io;

	while ('\0' != *i)
	{
		switch (*i)
		{
			case '\\':
				i++;
				if ('\\' == *i)
				{
					*o++ = *i++;
				}
				else
				{
					if (0 != isdigit(i[0]) && 0 != isdigit(i[1]) && 0 != isdigit(i[2]))
					{
						*o = (*i++ - 0x30) << 6;
						*o += (*i++ - 0x30) << 3;
						*o++ += *i++ - 0x30;
					}
				}
				break;

			default:
				*o++ = *i++;
		}
	}

	return o - io;
}
#endif

DB_ROW	trx_db_fetch(DB_RESULT result)
{
#if defined(HAVE_IBM_DB2)
	int		i;
#elif defined(HAVE_ORACLE)
	int		i;
	sword		rc;
	static char	errbuf[512];
	sb4		errcode;
#endif

	if (NULL == result)
		return NULL;

#if defined(HAVE_IBM_DB2)
	if (SUCCEED != trx_ibm_db2_success(SQLFetch(result->hstmt)))	/* e.g., SQL_NO_DATA_FOUND */
		return NULL;

	for (i = 0; i < result->ncolumn; i++)
		result->values[i] = (SQL_NULL_DATA == result->values_len[i] ? NULL : result->values_cli[i]);

	return result->values;
#elif defined(HAVE_MYSQL)
	if (NULL == result->result)
		return NULL;

	return (DB_ROW)mysql_fetch_row(result->result);
#elif defined(HAVE_ORACLE)
	if (NULL == result->stmthp)
		return NULL;

	if (OCI_NO_DATA == (rc = OCIStmtFetch2(result->stmthp, oracle.errhp, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT)))
		return NULL;

	if (OCI_SUCCESS != rc)
	{
		ub4	rows_fetched;
		ub4	sizep = sizeof(ub4);

		if (OCI_SUCCESS != (rc = OCIErrorGet((dvoid *)oracle.errhp, (ub4)1, (text *)NULL,
				&errcode, (text *)errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR)))
		{
			trx_db_errlog(ERR_Z3006, rc, trx_oci_error(rc, NULL), NULL);
			return NULL;
		}

		switch (errcode)
		{
			case 1012:	/* ORA-01012: not logged on */
			case 2396:	/* ORA-02396: exceeded maximum idle time */
			case 3113:	/* ORA-03113: end-of-file on communication channel */
			case 3114:	/* ORA-03114: not connected to ORACLE */
				trx_db_errlog(ERR_Z3006, errcode, errbuf, NULL);
				return NULL;
			default:
				rc = OCIAttrGet((void *)result->stmthp, (ub4)OCI_HTYPE_STMT, (void *)&rows_fetched,
						(ub4 *)&sizep, (ub4)OCI_ATTR_ROWS_FETCHED, (OCIError *)oracle.errhp);

				if (OCI_SUCCESS != rc || 1 != rows_fetched)
				{
					trx_db_errlog(ERR_Z3006, errcode, errbuf, NULL);
					return NULL;
				}
		}
	}

	for (i = 0; i < result->ncolumn; i++)
	{
		ub4	alloc, amount;
		ub1	csfrm;
		sword	rc2;

		if (NULL == result->clobs[i])
			continue;

		if (OCI_SUCCESS != (rc2 = OCILobGetLength(oracle.svchp, oracle.errhp, result->clobs[i], &amount)))
		{
			/* If the LOB is NULL, the length is undefined. */
			/* In this case the function returns OCI_INVALID_HANDLE. */
			if (OCI_INVALID_HANDLE != rc2)
			{
				trx_db_errlog(ERR_Z3006, rc2, trx_oci_error(rc2, NULL), NULL);
				return NULL;
			}
			else
				amount = 0;
		}
		else if (OCI_SUCCESS != (rc2 = OCILobCharSetForm(oracle.envhp, oracle.errhp, result->clobs[i], &csfrm)))
		{
			trx_db_errlog(ERR_Z3006, rc2, trx_oci_error(rc2, NULL), NULL);
			return NULL;
		}

		if (result->values_alloc[i] < (alloc = amount * TRX_MAX_BYTES_IN_UTF8_CHAR + 1))
		{
			result->values_alloc[i] = alloc;
			result->values[i] = trx_realloc(result->values[i], result->values_alloc[i]);
		}

		if (OCI_SUCCESS == rc2)
		{
			if (OCI_SUCCESS != (rc2 = OCILobRead(oracle.svchp, oracle.errhp, result->clobs[i], &amount,
					(ub4)1, (dvoid *)result->values[i], (ub4)(result->values_alloc[i] - 1),
					(dvoid *)NULL, (OCICallbackLobRead)NULL, (ub2)0, csfrm)))
			{
				trx_db_errlog(ERR_Z3006, rc2, trx_oci_error(rc2, NULL), NULL);
				return NULL;
			}
		}

		result->values[i][amount] = '\0';
	}

	return result->values;
#elif defined(HAVE_POSTGRESQL)
	/* free old data */
	if (NULL != result->values)
		trx_free(result->values);

	/* EOF */
	if (result->cursor == result->row_num)
		return NULL;

	/* init result */
	result->fld_num = PQnfields(result->pg_result);

	if (result->fld_num > 0)
	{
		int	i;

		result->values = trx_malloc(result->values, sizeof(char *) * result->fld_num);

		for (i = 0; i < result->fld_num; i++)
		{
			if (PQgetisnull(result->pg_result, result->cursor, i))
			{
				result->values[i] = NULL;
			}
			else
			{
				result->values[i] = PQgetvalue(result->pg_result, result->cursor, i);
				if (PQftype(result->pg_result, i) == TRX_PG_BYTEAOID)	/* binary data type BYTEAOID */
					trx_db_bytea_unescape((u_char *)result->values[i]);
			}
		}
	}

	result->cursor++;

	return result->values;
#elif defined(HAVE_SQLITE3)
	/* EOF */
	if (result->curow >= result->nrow)
		return NULL;

	if (NULL == result->data)
		return NULL;

	result->curow++;	/* NOTE: first row == header row */

	return &(result->data[result->curow * result->ncolumn]);
#endif
}

int	trx_db_is_null(const char *field)
{
	if (NULL == field)
		return SUCCEED;
#ifdef HAVE_ORACLE
	if ('\0' == *field)
		return SUCCEED;
#endif
	return FAIL;
}

#ifdef HAVE_ORACLE
static void	OCI_DBclean_result(DB_RESULT result)
{
	if (NULL == result)
		return;

	if (NULL != result->values)
	{
		int	i;

		for (i = 0; i < result->ncolumn; i++)
		{
			trx_free(result->values[i]);

			/* deallocate the lob locator variable */
			if (NULL != result->clobs[i])
			{
				OCIDescriptorFree((dvoid *)result->clobs[i], OCI_DTYPE_LOB);
				result->clobs[i] = NULL;
			}
		}

		trx_free(result->values);
		trx_free(result->clobs);
		trx_free(result->values_alloc);
	}

	if (result->stmthp)
	{
		OCIHandleFree((dvoid *)result->stmthp, OCI_HTYPE_STMT);
		result->stmthp = NULL;
	}
}
#endif

void	DBfree_result(DB_RESULT result)
{
#if defined(HAVE_IBM_DB2)
	if (NULL == result)
		return;

	if (NULL != result->values_cli)
	{
		int	i;

		for (i = 0; i < result->nalloc; i++)
		{
			trx_free(result->values_cli[i]);
		}

		trx_free(result->values);
		trx_free(result->values_cli);
		trx_free(result->values_len);
	}

	if (result->hstmt)
		SQLFreeHandle(SQL_HANDLE_STMT, result->hstmt);

	trx_free(result);
#elif defined(HAVE_MYSQL)
	if (NULL == result)
		return;

	mysql_free_result(result->result);
	trx_free(result);
#elif defined(HAVE_ORACLE)
	int i;

	if (NULL == result)
		return;

	OCI_DBclean_result(result);

	for (i = 0; i < oracle.db_results.values_num; i++)
	{
		if (oracle.db_results.values[i] == result)
		{
			trx_vector_ptr_remove_noorder(&oracle.db_results, i);
			break;
		}
	}

	trx_free(result);
#elif defined(HAVE_POSTGRESQL)
	if (NULL == result)
		return;

	if (NULL != result->values)
	{
		result->fld_num = 0;
		trx_free(result->values);
		result->values = NULL;
	}

	PQclear(result->pg_result);
	trx_free(result);
#elif defined(HAVE_SQLITE3)
	if (NULL == result)
		return;

	if (NULL != result->data)
	{
		sqlite3_free_table(result->data);
	}

	trx_free(result);
#endif	/* HAVE_SQLITE3 */
}

#ifdef HAVE_IBM_DB2
/* server status: SQL_CD_TRUE or SQL_CD_FALSE */
static int	IBM_DB2server_status(void)
{
	int	server_status = SQL_CD_TRUE;

	if (SUCCEED != trx_ibm_db2_success(SQLGetConnectAttr(ibm_db2.hdbc, SQL_ATTR_CONNECTION_DEAD, &server_status,
			SQL_IS_POINTER, NULL)))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot determine IBM DB2 server status, assuming not connected");
	}

	return (SQL_CD_FALSE == server_status ? SQL_CD_TRUE : SQL_CD_FALSE);
}

static int	trx_ibm_db2_success(SQLRETURN ret)
{
	return (SQL_SUCCESS == ret || SQL_SUCCESS_WITH_INFO == ret ? SUCCEED : FAIL);
}

static int	trx_ibm_db2_success_ext(SQLRETURN ret)
{
	return (SQL_SUCCESS == ret || SQL_SUCCESS_WITH_INFO == ret || SQL_NO_DATA_FOUND == ret ? SUCCEED : FAIL);
}

static void	trx_ibm_db2_log_errors(SQLSMALLINT htype, SQLHANDLE hndl, trx_err_codes_t err, const char *context)
{
	SQLCHAR		tmp_message[SQL_MAX_MESSAGE_LENGTH + 1], sqlstate[SQL_SQLSTATE_SIZE + 1];
	char		*message = NULL;
	SQLINTEGER	sqlcode = 0;
	SQLSMALLINT	rec_nr = 1;
	size_t		message_alloc = 0, message_offset = 0;

	while (SQL_SUCCESS == SQLGetDiagRec(htype, hndl, rec_nr++, sqlstate, &sqlcode, tmp_message, sizeof(tmp_message),
			NULL))
	{
		if (NULL != message)
			trx_chrcpy_alloc(&message, &message_alloc, &message_offset, '|');

		trx_snprintf_alloc(&message, &message_alloc, &message_offset, "[%s] %s", sqlstate, tmp_message);
	}

	trx_db_errlog(err, (int)sqlcode, message, context);

	trx_free(message);
}
#endif

#ifdef HAVE_ORACLE
/* server status: OCI_SERVER_NORMAL or OCI_SERVER_NOT_CONNECTED */
static ub4	OCI_DBserver_status(void)
{
	sword	err;
	ub4	server_status = OCI_SERVER_NOT_CONNECTED;

	err = OCIAttrGet((void *)oracle.srvhp, OCI_HTYPE_SERVER, (void *)&server_status,
			(ub4 *)0, OCI_ATTR_SERVER_STATUS, (OCIError *)oracle.errhp);

	if (OCI_SUCCESS != err)
		treegix_log(LOG_LEVEL_WARNING, "cannot determine Oracle server status, assuming not connected");

	return server_status;
}
#endif	/* HAVE_ORACLE */

static int	trx_db_is_escape_sequence(char c)
{
#if defined(HAVE_MYSQL)
	if ('\'' == c || '\\' == c)
#elif defined(HAVE_POSTGRESQL)
	if ('\'' == c || ('\\' == c && 1 == TRX_PG_ESCAPE_BACKSLASH))
#else
	if ('\'' == c)
#endif
		return SUCCEED;

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_escape_string                                             *
 *                                                                            *
 * Return value: escaped string                                               *
 *                                                                            *
 * Comments: sync changes with 'trx_db_get_escape_string_len'                 *
 *           and 'trx_db_dyn_escape_string'                                   *
 *                                                                            *
 ******************************************************************************/
static void	trx_db_escape_string(const char *src, char *dst, size_t len, trx_escape_sequence_t flag)
{
	const char	*s;
	char		*d;

	assert(dst);

	len--;	/* '\0' */

	for (s = src, d = dst; NULL != s && '\0' != *s && 0 < len; s++)
	{
		if (ESCAPE_SEQUENCE_ON == flag && SUCCEED == trx_db_is_escape_sequence(*s))
		{
			if (2 > len)
				break;

#if defined(HAVE_MYSQL)
			*d++ = '\\';
#elif defined(HAVE_POSTGRESQL)
			*d++ = *s;
#else
			*d++ = '\'';
#endif
			len--;
		}
		*d++ = *s;
		len--;
	}
	*d = '\0';
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_get_escape_string_len                                     *
 *                                                                            *
 * Purpose: to calculate escaped string length limited by bytes or characters *
 *          whichever is reached first.                                       *
 *                                                                            *
 * Parameters: s         - [IN] string to escape                              *
 *             max_bytes - [IN] limit in bytes                                *
 *             max_chars - [IN] limit in characters                           *
 *             flag      - [IN] sequences need to be escaped on/off           *
 *                                                                            *
 * Return value: return length in bytes of escaped string                     *
 *               with terminating '\0'                                        *
 *                                                                            *
 ******************************************************************************/
static size_t	trx_db_get_escape_string_len(const char *s, size_t max_bytes, size_t max_chars,
		trx_escape_sequence_t flag)
{
	size_t	csize, len = 1;	/* '\0' */

	if (NULL == s)
		return len;

	while ('\0' != *s && 0 < max_chars)
	{
		csize = trx_utf8_char_len(s);

		/* process non-UTF-8 characters as single byte characters */
		if (0 == csize)
			csize = 1;

		if (max_bytes < csize)
			break;

		if (ESCAPE_SEQUENCE_ON == flag && SUCCEED == trx_db_is_escape_sequence(*s))
			len++;

		s += csize;
		len += csize;
		max_bytes -= csize;
		max_chars--;
	}

	return len;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_dyn_escape_string                                         *
 *                                                                            *
 * Purpose: to escape string limited by bytes or characters, whichever limit  *
 *          is reached first.                                                 *
 *                                                                            *
 * Parameters: src       - [IN] string to escape                              *
 *             max_bytes - [IN] limit in bytes                                *
 *             max_chars - [IN] limit in characters                           *
 *             flag      - [IN] sequences need to be escaped on/off           *
 *                                                                            *
 * Return value: escaped string                                               *
 *                                                                            *
 ******************************************************************************/
char	*trx_db_dyn_escape_string(const char *src, size_t max_bytes, size_t max_chars, trx_escape_sequence_t flag)
{
	char	*dst = NULL;
	size_t	len;

	len = trx_db_get_escape_string_len(src, max_bytes, max_chars, flag);

	dst = (char *)trx_malloc(dst, len);

	trx_db_escape_string(src, dst, len, flag);

	return dst;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_get_escape_like_pattern_len                               *
 *                                                                            *
 * Return value: return length of escaped LIKE pattern with terminating '\0'  *
 *                                                                            *
 * Comments: sync changes with 'trx_db_escape_like_pattern'                   *
 *                                                                            *
 ******************************************************************************/
static int	trx_db_get_escape_like_pattern_len(const char *src)
{
	int		len;
	const char	*s;

	len = trx_db_get_escape_string_len(src, TRX_SIZE_T_MAX, TRX_SIZE_T_MAX, ESCAPE_SEQUENCE_ON) - 1; /* minus '\0' */

	for (s = src; s && *s; s++)
	{
		len += (*s == '_' || *s == '%' || *s == TRX_SQL_LIKE_ESCAPE_CHAR);
		len += 1;
	}

	len++; /* '\0' */

	return len;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_escape_like_pattern                                       *
 *                                                                            *
 * Return value: escaped string to be used as pattern in LIKE                 *
 *                                                                            *
 * Comments: sync changes with 'trx_db_get_escape_like_pattern_len'           *
 *                                                                            *
 *           For instance, we wish to find string a_b%c\d'e!f in our database *
 *           using '!' as escape character. Our queries then become:          *
 *                                                                            *
 *           ... LIKE 'a!_b!%c\\d\'e!!f' ESCAPE '!' (MySQL, PostgreSQL)       *
 *           ... LIKE 'a!_b!%c\d''e!!f' ESCAPE '!' (IBM DB2, Oracle, SQLite3) *
 *                                                                            *
 *           Using backslash as escape character in LIKE would be too much    *
 *           trouble, because escaping backslashes would have to be escaped   *
 *           as well, like so:                                                *
 *                                                                            *
 *           ... LIKE 'a\\_b\\%c\\\\d\'e!f' ESCAPE '\\' or                    *
 *           ... LIKE 'a\\_b\\%c\\\\d\\\'e!f' ESCAPE '\\' (MySQL, PostgreSQL) *
 *           ... LIKE 'a\_b\%c\\d''e!f' ESCAPE '\' (IBM DB2, Oracle, SQLite3) *
 *                                                                            *
 *           Hence '!' instead of backslash.                                  *
 *                                                                            *
 ******************************************************************************/
static void	trx_db_escape_like_pattern(const char *src, char *dst, int len)
{
	char		*d;
	char		*tmp = NULL;
	const char	*t;

	assert(dst);

	tmp = (char *)trx_malloc(tmp, len);

	trx_db_escape_string(src, tmp, len, ESCAPE_SEQUENCE_ON);

	len--; /* '\0' */

	for (t = tmp, d = dst; t && *t && len; t++)
	{
		if (*t == '_' || *t == '%' || *t == TRX_SQL_LIKE_ESCAPE_CHAR)
		{
			if (len <= 1)
				break;
			*d++ = TRX_SQL_LIKE_ESCAPE_CHAR;
			len--;
		}
		*d++ = *t;
		len--;
	}

	*d = '\0';

	trx_free(tmp);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_dyn_escape_like_pattern                                   *
 *                                                                            *
 * Return value: escaped string to be used as pattern in LIKE                 *
 *                                                                            *
 ******************************************************************************/
char	*trx_db_dyn_escape_like_pattern(const char *src)
{
	int	len;
	char	*dst = NULL;

	len = trx_db_get_escape_like_pattern_len(src);

	dst = (char *)trx_malloc(dst, len);

	trx_db_escape_like_pattern(src, dst, len);

	return dst;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_strlen_n                                                  *
 *                                                                            *
 * Purpose: return the string length to fit into a database field of the      *
 *          specified size                                                    *
 *                                                                            *
 * Return value: the string length in bytes                                   *
 *                                                                            *
 ******************************************************************************/
int	trx_db_strlen_n(const char *text, size_t maxlen)
{
#ifdef HAVE_IBM_DB2
	return trx_strlen_utf8_nbytes(text, maxlen);
#else
	return trx_strlen_utf8_nchars(text, maxlen);
#endif
}
