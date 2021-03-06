
#ifndef TREEGIX_DB_H
#define TREEGIX_DB_H

#include "common.h"
#include "trxalgo.h"
#include "trxdb.h"
#include "dbschema.h"

extern char	*CONFIG_DBHOST;
extern char	*CONFIG_DBNAME;
extern char	*CONFIG_DBSCHEMA;
extern char	*CONFIG_DBUSER;
extern char	*CONFIG_DBPASSWORD;
extern char	*CONFIG_DBSOCKET;
extern int	CONFIG_DBPORT;
extern int	CONFIG_HISTSYNCER_FORKS;
extern int	CONFIG_UNAVAILABLE_DELAY;

typedef enum
{
	GRAPH_TYPE_NORMAL = 0,
	GRAPH_TYPE_STACKED = 1
}
trx_graph_types;

typedef enum
{
	CALC_FNC_MIN = 1,
	CALC_FNC_AVG = 2,
	CALC_FNC_MAX = 4,
	CALC_FNC_ALL = 7
}
trx_graph_item_calc_function;

typedef enum
{
	GRAPH_ITEM_SIMPLE = 0,
	GRAPH_ITEM_AGGREGATED = 1
}
trx_graph_item_type;

struct	_DC_TRIGGER;

#define TRX_DB_CONNECT_NORMAL	0
#define TRX_DB_CONNECT_EXIT	1
#define TRX_DB_CONNECT_ONCE	2

/* type of database */
#define TRX_DB_UNKNOWN	0
#define TRX_DB_SERVER	1
#define TRX_DB_PROXY	2

#define TRIGGER_OPDATA_LEN		255
#define TRIGGER_URL_LEN			255
#define TRIGGER_DESCRIPTION_LEN		255
#define TRIGGER_EXPRESSION_LEN		2048
#define TRIGGER_EXPRESSION_LEN_MAX	(TRIGGER_EXPRESSION_LEN + 1)
#if defined(HAVE_IBM_DB2) || defined(HAVE_ORACLE)
#	define TRIGGER_COMMENTS_LEN	2048
#else
#	define TRIGGER_COMMENTS_LEN	65535
#endif
#define TAG_NAME_LEN			255
#define TAG_VALUE_LEN			255

#define GROUP_NAME_LEN			255

#define HOST_HOST_LEN			MAX_TRX_HOSTNAME_LEN
#define HOST_HOST_LEN_MAX		(HOST_HOST_LEN + 1)
#define HOST_NAME_LEN			128
#define HOST_ERROR_LEN			2048
#define HOST_ERROR_LEN_MAX		(HOST_ERROR_LEN + 1)
#define HOST_IPMI_USERNAME_LEN		16
#define HOST_IPMI_USERNAME_LEN_MAX	(HOST_IPMI_USERNAME_LEN + 1)
#define HOST_IPMI_PASSWORD_LEN		20
#define HOST_IPMI_PASSWORD_LEN_MAX	(HOST_IPMI_PASSWORD_LEN + 1)
#define HOST_PROXY_ADDRESS_LEN		255
#define HOST_PROXY_ADDRESS_LEN_MAX	(HOST_PROXY_ADDRESS_LEN + 1)

#define INTERFACE_DNS_LEN		255
#define INTERFACE_DNS_LEN_MAX		(INTERFACE_DNS_LEN + 1)
#define INTERFACE_IP_LEN		64
#define INTERFACE_IP_LEN_MAX		(INTERFACE_IP_LEN + 1)
#define INTERFACE_ADDR_LEN		255	/* MAX(INTERFACE_DNS_LEN,INTERFACE_IP_LEN) */
#define INTERFACE_ADDR_LEN_MAX		(INTERFACE_ADDR_LEN + 1)
#define INTERFACE_PORT_LEN		64
#define INTERFACE_PORT_LEN_MAX		(INTERFACE_PORT_LEN + 1)

#define ITEM_NAME_LEN			255
#define ITEM_KEY_LEN			255
#define ITEM_DELAY_LEN			1024
#define ITEM_HISTORY_LEN		255
#define ITEM_TRENDS_LEN			255
#define ITEM_UNITS_LEN			255
#define ITEM_SNMP_COMMUNITY_LEN		64
#define ITEM_SNMP_COMMUNITY_LEN_MAX	(ITEM_SNMP_COMMUNITY_LEN + 1)
#define ITEM_SNMP_OID_LEN		512
#define ITEM_SNMP_OID_LEN_MAX		(ITEM_SNMP_OID_LEN + 1)
#define ITEM_ERROR_LEN			2048
#define ITEM_ERROR_LEN_MAX		(ITEM_ERROR_LEN + 1)
#define ITEM_TRAPPER_HOSTS_LEN		255
#define ITEM_TRAPPER_HOSTS_LEN_MAX	(ITEM_TRAPPER_HOSTS_LEN + 1)
#define ITEM_SNMPV3_SECURITYNAME_LEN		64
#define ITEM_SNMPV3_SECURITYNAME_LEN_MAX	(ITEM_SNMPV3_SECURITYNAME_LEN + 1)
#define ITEM_SNMPV3_AUTHPASSPHRASE_LEN		64
#define ITEM_SNMPV3_AUTHPASSPHRASE_LEN_MAX	(ITEM_SNMPV3_AUTHPASSPHRASE_LEN + 1)
#define ITEM_SNMPV3_PRIVPASSPHRASE_LEN		64
#define ITEM_SNMPV3_PRIVPASSPHRASE_LEN_MAX	(ITEM_SNMPV3_PRIVPASSPHRASE_LEN + 1)
#define ITEM_SNMPV3_CONTEXTNAME_LEN		255
#define ITEM_SNMPV3_CONTEXTNAME_LEN_MAX		(ITEM_SNMPV3_CONTEXTNAME_LEN + 1)
#define ITEM_LOGTIMEFMT_LEN		64
#define ITEM_LOGTIMEFMT_LEN_MAX		(ITEM_LOGTIMEFMT_LEN + 1)
#define ITEM_IPMI_SENSOR_LEN		128
#define ITEM_IPMI_SENSOR_LEN_MAX	(ITEM_IPMI_SENSOR_LEN + 1)
#define ITEM_USERNAME_LEN		64
#define ITEM_USERNAME_LEN_MAX		(ITEM_USERNAME_LEN + 1)
#define ITEM_PASSWORD_LEN		64
#define ITEM_PASSWORD_LEN_MAX		(ITEM_PASSWORD_LEN + 1)
#define ITEM_PUBLICKEY_LEN		64
#define ITEM_PUBLICKEY_LEN_MAX		(ITEM_PUBLICKEY_LEN + 1)
#define ITEM_PRIVATEKEY_LEN		64
#define ITEM_PRIVATEKEY_LEN_MAX		(ITEM_PRIVATEKEY_LEN + 1)
#define ITEM_JMX_ENDPOINT_LEN		255
#define ITEM_JMX_ENDPOINT_LEN_MAX	(ITEM_JMX_ENDPOINT_LEN + 1)
#define ITEM_TIMEOUT_LEN		255
#define ITEM_TIMEOUT_LEN_MAX		(ITEM_TIMEOUT_LEN + 1)
#define ITEM_URL_LEN			2048
#define ITEM_URL_LEN_MAX		(ITEM_URL_LEN + 1)
#define ITEM_QUERY_FIELDS_LEN		2048
#define ITEM_QUERY_FIELDS_LEN_MAX	(ITEM_QUERY_FIELDS_LEN + 1)
#define ITEM_STATUS_CODES_LEN		255
#define ITEM_STATUS_CODES_LEN_MAX	(ITEM_STATUS_CODES_LEN + 1)
#define ITEM_HTTP_PROXY_LEN		255
#define ITEM_HTTP_PROXY_LEN_MAX		(ITEM_HTTP_PROXY_LEN + 1)
#define ITEM_SSL_KEY_PASSWORD_LEN	64
#define ITEM_SSL_KEY_PASSWORD_LEN_MAX	(ITEM_SSL_KEY_PASSWORD_LEN + 1)
#define ITEM_SSL_CERT_FILE_LEN		255
#define ITEM_SSL_CERT_FILE_LEN_MAX	(ITEM_SSL_CERT_FILE_LEN + 1)
#define ITEM_SSL_KEY_FILE_LEN		255
#define ITEM_SSL_KEY_FILE_LEN_MAX	(ITEM_SSL_KEY_FILE_LEN + 1)
#if defined(HAVE_IBM_DB2) || defined(HAVE_ORACLE)
#	define ITEM_PARAM_LEN		2048
#	define ITEM_DESCRIPTION_LEN	2048
#	define ITEM_POSTS_LEN		2048
#	define ITEM_HEADERS_LEN		2048
#else
#	define ITEM_PARAM_LEN		65535
#	define ITEM_DESCRIPTION_LEN	65535
#	define ITEM_POSTS_LEN		65535
#	define ITEM_HEADERS_LEN		65535
#endif

#define HISTORY_STR_VALUE_LEN		255
#ifdef HAVE_IBM_DB2
#	define HISTORY_TEXT_VALUE_LEN	2048
#	define HISTORY_LOG_VALUE_LEN	2048
#else
#	define HISTORY_TEXT_VALUE_LEN	65535
#	define HISTORY_LOG_VALUE_LEN	65535
#endif

#define HISTORY_LOG_SOURCE_LEN		64
#define HISTORY_LOG_SOURCE_LEN_MAX	(HISTORY_LOG_SOURCE_LEN + 1)

#define ALERT_ERROR_LEN			2048
#define ALERT_ERROR_LEN_MAX		(ALERT_ERROR_LEN + 1)

#define GRAPH_NAME_LEN			128

#define GRAPH_ITEM_COLOR_LEN		6
#define GRAPH_ITEM_COLOR_LEN_MAX	(GRAPH_ITEM_COLOR_LEN + 1)

#define DSERVICE_VALUE_LEN		255
#define MAX_DISCOVERED_VALUE_SIZE	(DSERVICE_VALUE_LEN * TRX_MAX_BYTES_IN_UTF8_CHAR + 1)

#define HTTPTEST_HTTP_USER_LEN		64
#define HTTPTEST_HTTP_PASSWORD_LEN	64

#define PROXY_DHISTORY_VALUE_LEN	255

#define ITEM_PREPROC_PARAMS_LEN		255

#define EVENT_NAME_LEN			2048

#define FUNCTION_PARAM_LEN		255

#define TRX_SQL_ITEM_FIELDS	"i.itemid,i.key_,h.host,i.type,i.history,i.hostid,i.value_type,i.delta,"	\
				"i.units,i.multiplier,i.formula,i.state,i.valuemapid,i.trends,i.data_type"
#define TRX_SQL_ITEM_TABLES	"hosts h,items i"
#define TRX_SQL_TIME_FUNCTIONS	"'nodata','date','dayofmonth','dayofweek','time','now'"
#define TRX_SQL_ITEM_FIELDS_NUM	15
#define TRX_SQL_ITEM_SELECT	TRX_SQL_ITEM_FIELDS " from " TRX_SQL_ITEM_TABLES

#ifdef HAVE_ORACLE
#	define TRX_PLSQL_BEGIN	"begin\n"
#	define TRX_PLSQL_END	"end;"
#	define	DBbegin_multiple_update(sql, sql_alloc, sql_offset)			\
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, TRX_PLSQL_BEGIN)
#	define	DBend_multiple_update(sql, sql_alloc, sql_offset)			\
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, TRX_PLSQL_END)
#	if 0 == TRX_MAX_OVERFLOW_SQL_SIZE
#		define	TRX_SQL_EXEC_FROM	TRX_CONST_STRLEN(TRX_PLSQL_BEGIN)
#	else
#		define	TRX_SQL_EXEC_FROM	0
#	endif

#	define	TRX_SQL_STRCMP		"%s%s%s"
#	define	TRX_SQL_STRVAL_EQ(str)				\
			'\0' != *str ? "='"  : "",		\
			'\0' != *str ? str   : " is null",	\
			'\0' != *str ? "'"   : ""
#	define	TRX_SQL_STRVAL_NE(str)				\
			'\0' != *str ? "<>'" : "",		\
			'\0' != *str ? str   : " is not null",	\
			'\0' != *str ? "'"   : ""

#else
#	define	DBbegin_multiple_update(sql, sql_alloc, sql_offset)	do {} while (0)
#	define	DBend_multiple_update(sql, sql_alloc, sql_offset)	do {} while (0)

#	define	TRX_SQL_EXEC_FROM	0
#	ifdef HAVE_MYSQL
#		define	TRX_SQL_STRCMP		"%s binary '%s'"
#	else
#		define	TRX_SQL_STRCMP		"%s'%s'"
#	endif
#	define	TRX_SQL_STRVAL_EQ(str)	"=", str
#	define	TRX_SQL_STRVAL_NE(str)	"<>", str
#endif

#define TRX_SQL_NULLCMP(f1, f2)	"((" f1 " is null and " f2 " is null) or " f1 "=" f2 ")"

#define TRX_DBROW2UINT64(uint, row)	if (SUCCEED == DBis_null(row))		\
						uint = 0;			\
					else					\
						is_uint64(row, &uint)

#define TRX_DB_MAX_ID	(trx_uint64_t)__UINT64_C(0x7fffffffffffffff)

typedef struct
{
	trx_uint64_t	druleid;
	trx_uint64_t	unique_dcheckid;
	char		*iprange;
	char		*name;
}
DB_DRULE;

typedef struct
{
	trx_uint64_t	dcheckid;
	char		*ports;
	char		*key_;
	char		*snmp_community;
	char		*snmpv3_securityname;
	char		*snmpv3_authpassphrase;
	char		*snmpv3_privpassphrase;
	char		*snmpv3_contextname;
	int		type;
	unsigned char	snmpv3_securitylevel;
	unsigned char	snmpv3_authprotocol;
	unsigned char	snmpv3_privprotocol;
}
DB_DCHECK;

typedef struct
{
	trx_uint64_t	dhostid;
	int		status;
	int		lastup;
	int		lastdown;
}
DB_DHOST;

typedef struct
{
	trx_uint64_t	dserviceid;
	int		status;
	int		lastup;
	int		lastdown;
	char		*value;
}
DB_DSERVICE;

typedef struct
{
	trx_uint64_t	triggerid;
	char		*description;
	char		*expression;
	char		*recovery_expression;
	char		*url;
	char		*comments;
	char		*correlation_tag;
	char		*opdata;
	unsigned char	value;
	unsigned char	priority;
	unsigned char	type;
	unsigned char	recovery_mode;
	unsigned char	correlation_mode;
}
DB_TRIGGER;

typedef struct
{
	trx_uint64_t		eventid;
	DB_TRIGGER		trigger;
	trx_uint64_t		objectid;
	char			*name;
	int			source;
	int			object;
	int			clock;
	int			value;
	int			acknowledged;
	int			ns;
	int			severity;
	unsigned char		suppressed;

	trx_vector_ptr_t	tags;	/* used for both trx_tag_t and trx_host_tag_t */

#define TRX_FLAGS_DB_EVENT_UNSET		0x0000
#define TRX_FLAGS_DB_EVENT_CREATE		0x0001
#define TRX_FLAGS_DB_EVENT_NO_ACTION		0x0002
	trx_uint64_t		flags;
}
DB_EVENT;

typedef struct
{
	trx_uint64_t		mediatypeid;
	trx_media_type_t	type;
	char			*description;
	char			*smtp_server;
	char			*smtp_helo;
	char			*smtp_email;
	char			*exec_path;
	char			*exec_params;
	char			*gsm_modem;
	char			*username;
	char			*passwd;
	unsigned short		smtp_port;
	unsigned char		smtp_security;
	unsigned char		smtp_verify_peer;
	unsigned char		smtp_verify_host;
	unsigned char		smtp_authentication;
}
DB_MEDIATYPE;

typedef struct
{
	trx_uint64_t	conditionid;
	trx_uint64_t	actionid;
	char		*value;
	char		*value2;
	int		condition_result;
	unsigned char	conditiontype;
	unsigned char	op;
}
DB_CONDITION;

typedef struct
{
	trx_uint64_t	alertid;
	trx_uint64_t 	actionid;
	int		clock;
	trx_uint64_t	mediatypeid;
	char		*sendto;
	char		*subject;
	char		*message;
	trx_alert_status_t	status;
	int		retries;
}
DB_ALERT;

typedef struct
{
	trx_uint64_t	housekeeperid;
	char		*tablename;
	char		*field;
	trx_uint64_t	value;
}
DB_HOUSEKEEPER;

typedef struct
{
	trx_uint64_t	httptestid;
	char		*name;
	char		*agent;
	char		*http_user;
	char		*http_password;
	char		*http_proxy;
	char		*ssl_cert_file;
	char		*ssl_key_file;
	char		*ssl_key_password;
	char		*delay;
	int		authentication;
	int		retries;
	int		verify_peer;
	int		verify_host;
}
DB_HTTPTEST;

typedef struct
{
	trx_uint64_t	httpstepid;
	trx_uint64_t	httptestid;
	char		*name;
	char		*url;
	char		*posts;
	char		*required;
	char		*status_codes;
	int		no;
	int		timeout;
	int		follow_redirects;
	int		retrieve_mode;
	int		post_type;
}
DB_HTTPSTEP;

typedef struct
{
	trx_uint64_t		escalationid;
	trx_uint64_t		actionid;
	trx_uint64_t		triggerid;
	trx_uint64_t		itemid;
	trx_uint64_t		eventid;
	trx_uint64_t		r_eventid;
	trx_uint64_t		acknowledgeid;
	int			nextcheck;
	int			esc_step;
	trx_escalation_status_t	status;
}
DB_ESCALATION;

typedef struct
{
	trx_uint64_t	actionid;
	char		*name;
	char		*shortdata;
	char		*longdata;
	char		*r_shortdata;
	char		*r_longdata;
	char		*ack_shortdata;
	char		*ack_longdata;
	int		esc_period;
	unsigned char	eventsource;
	unsigned char	pause_suppressed;
	unsigned char	recovery;
	unsigned char	status;
}
DB_ACTION;

typedef struct
{
	trx_uint64_t	acknowledgeid;
	trx_uint64_t	userid;
	char		*message;
	int		clock;
	int		action;
	int		old_severity;
	int		new_severity;
}
DB_ACKNOWLEDGE;

int	DBinit(char **error);
void	DBdeinit(void);

int	DBconnect(int flag);
void	DBclose(void);

#ifdef HAVE_ORACLE
void	DBstatement_prepare(const char *sql);
#endif
int		DBexecute(const char *fmt, ...) __trx_attr_format_printf(1, 2);
int		DBexecute_once(const char *fmt, ...) __trx_attr_format_printf(1, 2);
DB_RESULT	DBselect_once(const char *fmt, ...) __trx_attr_format_printf(1, 2);
DB_RESULT	DBselect(const char *fmt, ...) __trx_attr_format_printf(1, 2);
DB_RESULT	DBselectN(const char *query, int n);
DB_ROW		DBfetch(DB_RESULT result);
int		DBis_null(const char *field);
void		DBbegin(void);
int		DBcommit(void);
void		DBrollback(void);
int		DBend(int ret);

const TRX_TABLE	*DBget_table(const char *tablename);
const TRX_FIELD	*DBget_field(const TRX_TABLE *table, const char *fieldname);
#define DBget_maxid(table)	DBget_maxid_num(table, 1)
trx_uint64_t	DBget_maxid_num(const char *tablename, int num);

/******************************************************************************
 *                                                                            *
 * Type: TRX_GRAPH_ITEMS                                                      *
 *                                                                            *
 * Purpose: represent graph item data                                         *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
typedef struct
{
	trx_uint64_t	itemid;	/* itemid should come first for correct sorting */
	trx_uint64_t	gitemid;
	char		key[ITEM_KEY_LEN * TRX_MAX_BYTES_IN_UTF8_CHAR + 1];
	int		drawtype;
	int		sortorder;
	char		color[GRAPH_ITEM_COLOR_LEN_MAX];
	int		yaxisside;
	int		calc_fnc;
	int		type;
	unsigned char	flags;
}
TRX_GRAPH_ITEMS;

typedef struct
{
	trx_uint64_t	triggerid;
	unsigned char	value;
	unsigned char	state;
	unsigned char	priority;
	int		lastchange;
	int		problem_count;
	char		*error;

#define TRX_FLAGS_TRIGGER_DIFF_UNSET				0x0000
#define TRX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE			0x0001
#define TRX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE		0x0002
#define TRX_FLAGS_TRIGGER_DIFF_UPDATE_STATE			0x0004
#define TRX_FLAGS_TRIGGER_DIFF_UPDATE_ERROR			0x0008
#define TRX_FLAGS_TRIGGER_DIFF_UPDATE										\
		(TRX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE | TRX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE | 		\
		TRX_FLAGS_TRIGGER_DIFF_UPDATE_STATE | TRX_FLAGS_TRIGGER_DIFF_UPDATE_ERROR)
#define TRX_FLAGS_TRIGGER_DIFF_UPDATE_PROBLEM_COUNT		0x1000
#define TRX_FLAGS_TRIGGER_DIFF_RECALCULATE_PROBLEM_COUNT	0x2000
	trx_uint64_t			flags;
}
trx_trigger_diff_t;

void	trx_process_triggers(trx_vector_ptr_t *triggers, trx_vector_ptr_t *diffs);
void	trx_db_save_trigger_changes(const trx_vector_ptr_t *diffs);
void	trx_trigger_diff_free(trx_trigger_diff_t *diff);
void	trx_append_trigger_diff(trx_vector_ptr_t *trigger_diff, trx_uint64_t triggerid, unsigned char priority,
		trx_uint64_t flags, unsigned char value, unsigned char state, int lastchange, const char *error);

int	DBget_row_count(const char *table_name);
int	DBget_proxy_lastaccess(const char *hostname, int *lastaccess, char **error);

char	*DBdyn_escape_field(const char *table_name, const char *field_name, const char *src);
char	*DBdyn_escape_string(const char *src);
char	*DBdyn_escape_string_len(const char *src, size_t length);
char	*DBdyn_escape_like_pattern(const char *src);

trx_uint64_t	DBadd_host(char *server, int port, int status, int useip, char *ip, int disable_until, int available);
int	DBadd_templates_to_host(int hostid, int host_templateid);

int	DBadd_template_linkage(int hostid, int templateid, int items, int triggers, int graphs);

int	DBadd_trigger_to_linked_hosts(int triggerid, int hostid);
void	DBdelete_sysmaps_hosts_by_hostid(trx_uint64_t hostid);

int	DBadd_graph_item_to_linked_hosts(int gitemid, int hostid);

int	DBcopy_template_elements(trx_uint64_t hostid, trx_vector_uint64_t *lnk_templateids, char **error);
int	DBdelete_template_elements(trx_uint64_t hostid, trx_vector_uint64_t *del_templateids, char **error);

void	DBdelete_items(trx_vector_uint64_t *itemids);
void	DBdelete_graphs(trx_vector_uint64_t *graphids);
void	DBdelete_hosts(trx_vector_uint64_t *hostids);
void	DBdelete_hosts_with_prototypes(trx_vector_uint64_t *hostids);

int	DBupdate_itservices(const trx_vector_ptr_t *trigger_diff);
int	DBremove_triggers_from_itservices(trx_uint64_t *triggerids, int triggerids_num);

int	trx_create_itservices_lock(char **error);
void	trx_destroy_itservices_lock(void);

void	DBadd_condition_alloc(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *fieldname,
		const trx_uint64_t *values, const int num);
void	DBadd_str_condition_alloc(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *fieldname,
		const char **values, const int num);

int	trx_check_user_permissions(const trx_uint64_t *userid, const trx_uint64_t *recipient_userid);

const char	*trx_host_string(trx_uint64_t hostid);
const char	*trx_host_key_string(trx_uint64_t itemid);
const char	*trx_user_string(trx_uint64_t userid);

void	DBregister_host(trx_uint64_t proxy_hostid, const char *host, const char *ip, const char *dns,
		unsigned short port, unsigned int connection_type, const char *host_metadata, unsigned short flag,
		int now);
void	DBregister_host_prepare(trx_vector_ptr_t *autoreg_hosts, const char *host, const char *ip, const char *dns,
		unsigned short port, unsigned int connection_type, const char *host_metadata, unsigned short flag,
		int now);
void	DBregister_host_flush(trx_vector_ptr_t *autoreg_hosts, trx_uint64_t proxy_hostid);
void	DBregister_host_clean(trx_vector_ptr_t *autoreg_hosts);

void	DBproxy_register_host(const char *host, const char *ip, const char *dns, unsigned short port,
		unsigned int connection_type, const char *host_metadata, unsigned short flag);
int	DBexecute_overflowed_sql(char **sql, size_t *sql_alloc, size_t *sql_offset);
char	*DBget_unique_hostname_by_sample(const char *host_name_sample, const char *field_name);

const char	*DBsql_id_ins(trx_uint64_t id);
const char	*DBsql_id_cmp(trx_uint64_t id);

typedef enum
{
	TRX_CONN_DEFAULT = 0,
	TRX_CONN_IP,
	TRX_CONN_DNS,
}
trx_conn_flags_t;

trx_uint64_t	DBadd_interface(trx_uint64_t hostid, unsigned char type, unsigned char useip, const char *ip,
		const char *dns, unsigned short port, trx_conn_flags_t flags);

const char	*DBget_inventory_field(unsigned char inventory_link);

void	DBset_host_inventory(trx_uint64_t hostid, int inventory_mode);
void	DBadd_host_inventory(trx_uint64_t hostid, int inventory_mode);

int	DBtxn_status(void);
int	DBtxn_ongoing(void);

int	DBtable_exists(const char *table_name);
int	DBfield_exists(const char *table_name, const char *field_name);
#ifndef HAVE_SQLITE3
int	DBindex_exists(const char *table_name, const char *index_name);
#endif

int	DBexecute_multiple_query(const char *query, const char *field_name, trx_vector_uint64_t *ids);
int	DBlock_record(const char *table, trx_uint64_t id, const char *add_field, trx_uint64_t add_id);
int	DBlock_records(const char *table, const trx_vector_uint64_t *ids);
int	DBlock_ids(const char *table_name, const char *field_name, trx_vector_uint64_t *ids);

#define DBlock_hostid(id)			DBlock_record("hosts", id, NULL, 0)
#define DBlock_druleid(id)			DBlock_record("drules", id, NULL, 0)
#define DBlock_dcheckid(dcheckid, druleid)	DBlock_record("dchecks", dcheckid, "druleid", druleid)
#define DBlock_hostids(ids)			DBlock_records("hosts", ids)

void	DBdelete_groups(trx_vector_uint64_t *groupids);

void	DBselect_uint64(const char *sql, trx_vector_uint64_t *ids);

/* bulk insert support */

/* database bulk insert data */
typedef struct
{
	/* the target table */
	const TRX_TABLE		*table;
	/* the fields to insert (pointers to the TRX_FIELD structures from database schema) */
	trx_vector_ptr_t	fields;
	/* the values rows to insert (pointers to arrays of trx_db_value_t structures) */
	trx_vector_ptr_t	rows;
	/* index of autoincrement field */
	int			autoincrement;
}
trx_db_insert_t;

void	trx_db_insert_prepare_dyn(trx_db_insert_t *self, const TRX_TABLE *table, const TRX_FIELD **fields,
		int fields_num);
void	trx_db_insert_prepare(trx_db_insert_t *self, const char *table, ...);
void	trx_db_insert_add_values_dyn(trx_db_insert_t *self, const trx_db_value_t **values, int values_num);
void	trx_db_insert_add_values(trx_db_insert_t *self, ...);
int	trx_db_insert_execute(trx_db_insert_t *self);
void	trx_db_insert_clean(trx_db_insert_t *self);
void	trx_db_insert_autoincrement(trx_db_insert_t *self, const char *field_name);
int	trx_db_get_database_type(void);

/* agent (TREEGIX, SNMP, IPMI, JMX) availability data */
typedef struct
{
	/* flags specifying which fields are set, see TRX_FLAGS_AGENT_STATUS_* defines */
	unsigned char	flags;

	/* agent availability fields */
	unsigned char	available;
	char		*error;
	int		errors_from;
	int		disable_until;
}
trx_agent_availability_t;

#define TRX_FLAGS_AGENT_STATUS_NONE		0x00000000
#define TRX_FLAGS_AGENT_STATUS_AVAILABLE	0x00000001
#define TRX_FLAGS_AGENT_STATUS_ERROR		0x00000002
#define TRX_FLAGS_AGENT_STATUS_ERRORS_FROM	0x00000004
#define TRX_FLAGS_AGENT_STATUS_DISABLE_UNTIL	0x00000008

#define TRX_FLAGS_AGENT_STATUS		(TRX_FLAGS_AGENT_STATUS_AVAILABLE |	\
					TRX_FLAGS_AGENT_STATUS_ERROR |		\
					TRX_FLAGS_AGENT_STATUS_ERRORS_FROM |	\
					TRX_FLAGS_AGENT_STATUS_DISABLE_UNTIL)

#define TRX_AGENT_TREEGIX	(INTERFACE_TYPE_AGENT - 1)
#define TRX_AGENT_SNMP		(INTERFACE_TYPE_SNMP - 1)
#define TRX_AGENT_IPMI		(INTERFACE_TYPE_IPMI - 1)
#define TRX_AGENT_JMX		(INTERFACE_TYPE_JMX - 1)
#define TRX_AGENT_UNKNOWN 	255
#define TRX_AGENT_MAX		INTERFACE_TYPE_COUNT

typedef struct
{
	trx_uint64_t			hostid;

	trx_agent_availability_t	agents[TRX_AGENT_MAX];
}
trx_host_availability_t;


int	trx_sql_add_host_availability(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const trx_host_availability_t *ha);
int	DBget_user_by_active_session(const char *sessionid, trx_user_t *user);

typedef struct
{
	trx_uint64_t	itemid;
	trx_uint64_t	lastlogsize;
	unsigned char	state;
	int		mtime;
	int		lastclock;
	const char	*error;

	trx_uint64_t	flags;
#define TRX_FLAGS_ITEM_DIFF_UNSET			__UINT64_C(0x0000)
#define TRX_FLAGS_ITEM_DIFF_UPDATE_STATE		__UINT64_C(0x0001)
#define TRX_FLAGS_ITEM_DIFF_UPDATE_ERROR		__UINT64_C(0x0002)
#define TRX_FLAGS_ITEM_DIFF_UPDATE_MTIME		__UINT64_C(0x0004)
#define TRX_FLAGS_ITEM_DIFF_UPDATE_LASTLOGSIZE		__UINT64_C(0x0008)
#define TRX_FLAGS_ITEM_DIFF_UPDATE_LASTCLOCK		__UINT64_C(0x1000)
#define TRX_FLAGS_ITEM_DIFF_UPDATE_DB			\
	(TRX_FLAGS_ITEM_DIFF_UPDATE_STATE | TRX_FLAGS_ITEM_DIFF_UPDATE_ERROR |\
	TRX_FLAGS_ITEM_DIFF_UPDATE_MTIME | TRX_FLAGS_ITEM_DIFF_UPDATE_LASTLOGSIZE)
#define TRX_FLAGS_ITEM_DIFF_UPDATE	(TRX_FLAGS_ITEM_DIFF_UPDATE_DB | TRX_FLAGS_ITEM_DIFF_UPDATE_LASTCLOCK)
}
trx_item_diff_t;

/* event support */
void	trx_db_get_events_by_eventids(trx_vector_uint64_t *eventids, trx_vector_ptr_t *events);
void	trx_db_free_event(DB_EVENT *event);
void	trx_db_get_eventid_r_eventid_pairs(trx_vector_uint64_t *eventids, trx_vector_uint64_pair_t *event_pairs,
		trx_vector_uint64_t *r_eventids);

void	trx_db_trigger_clean(DB_TRIGGER *trigger);


typedef struct
{
	trx_uint64_t	hostid;
	unsigned char	compress;
	int		version;
	int		lastaccess;
	int		last_version_error_time;

#define TRX_FLAGS_PROXY_DIFF_UNSET				__UINT64_C(0x0000)
#define TRX_FLAGS_PROXY_DIFF_UPDATE_COMPRESS			__UINT64_C(0x0001)
#define TRX_FLAGS_PROXY_DIFF_UPDATE_VERSION			__UINT64_C(0x0002)
#define TRX_FLAGS_PROXY_DIFF_UPDATE_LASTACCESS			__UINT64_C(0x0004)
#define TRX_FLAGS_PROXY_DIFF_UPDATE_LASTERROR			__UINT64_C(0x0008)
#define TRX_FLAGS_PROXY_DIFF_UPDATE (			\
		TRX_FLAGS_PROXY_DIFF_UPDATE_COMPRESS |	\
		TRX_FLAGS_PROXY_DIFF_UPDATE_VERSION | 	\
		TRX_FLAGS_PROXY_DIFF_UPDATE_LASTACCESS)
	trx_uint64_t	flags;
}
trx_proxy_diff_t;

int	trx_db_lock_maintenanceids(trx_vector_uint64_t *maintenanceids);

void	trx_db_save_item_changes(char **sql, size_t *sql_alloc, size_t *sql_offset, const trx_vector_ptr_t *item_diff);

/* mock field to estimate how much data can be stored in characters, bytes or both, */
/* depending on database backend                                                    */

typedef struct
{
	int	bytes_num;
	int	chars_num;
}
trx_db_mock_field_t;

void	trx_db_mock_field_init(trx_db_mock_field_t *field, int field_type, int field_len);
int	trx_db_mock_field_append(trx_db_mock_field_t *field, const char *text);

#endif
