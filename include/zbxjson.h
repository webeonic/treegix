
#ifndef TREEGIX_ZJSON_H
#define TREEGIX_ZJSON_H

#define TRX_PROTO_TAG_CLOCK			"clock"
#define TRX_PROTO_TAG_NS			"ns"
#define TRX_PROTO_TAG_DATA			"data"
#define TRX_PROTO_TAG_REGEXP			"regexp"
#define TRX_PROTO_TAG_DELAY			"delay"
#define TRX_PROTO_TAG_REFRESH_UNSUPPORTED	"refresh_unsupported"
#define TRX_PROTO_TAG_DRULE			"drule"
#define TRX_PROTO_TAG_DCHECK			"dcheck"
#define TRX_PROTO_TAG_HOST			"host"
#define TRX_PROTO_TAG_HOST_METADATA		"host_metadata"
#define TRX_PROTO_TAG_INFO			"info"
#define TRX_PROTO_TAG_IP			"ip"
#define TRX_PROTO_TAG_DNS			"dns"
#define TRX_PROTO_TAG_CONN			"conn"
#define TRX_PROTO_TAG_KEY			"key"
#define TRX_PROTO_TAG_KEY_ORIG			"key_orig"
#define TRX_PROTO_TAG_KEYS			"keys"
#define TRX_PROTO_TAG_LASTLOGSIZE		"lastlogsize"
#define TRX_PROTO_TAG_MTIME			"mtime"
#define TRX_PROTO_TAG_LOGTIMESTAMP		"timestamp"
#define TRX_PROTO_TAG_LOGSOURCE			"source"
#define TRX_PROTO_TAG_LOGSEVERITY		"severity"
#define TRX_PROTO_TAG_LOGEVENTID		"eventid"
#define TRX_PROTO_TAG_PORT			"port"
#define TRX_PROTO_TAG_TLS_ACCEPTED		"tls_accepted"
#define TRX_PROTO_TAG_PROXY			"proxy"
#define TRX_PROTO_TAG_REQUEST			"request"
#define TRX_PROTO_TAG_RESPONSE			"response"
#define TRX_PROTO_TAG_STATUS			"status"
#define TRX_PROTO_TAG_STATE			"state"
#define TRX_PROTO_TAG_TYPE			"type"
#define TRX_PROTO_TAG_LIMIT			"limit"
#define TRX_PROTO_TAG_VALUE			"value"
#define TRX_PROTO_TAG_SCRIPTID			"scriptid"
#define TRX_PROTO_TAG_HOSTID			"hostid"
#define TRX_PROTO_TAG_AVAILABLE			"available"
#define TRX_PROTO_TAG_SNMP_AVAILABLE		"snmp_available"
#define TRX_PROTO_TAG_IPMI_AVAILABLE		"ipmi_available"
#define TRX_PROTO_TAG_JMX_AVAILABLE		"jmx_available"
#define TRX_PROTO_TAG_ERROR			"error"
#define TRX_PROTO_TAG_SNMP_ERROR		"snmp_error"
#define TRX_PROTO_TAG_IPMI_ERROR		"ipmi_error"
#define TRX_PROTO_TAG_JMX_ERROR			"jmx_error"
#define TRX_PROTO_TAG_USERNAME			"username"
#define TRX_PROTO_TAG_PASSWORD			"password"
#define TRX_PROTO_TAG_SID			"sid"
#define TRX_PROTO_TAG_VERSION			"version"
#define TRX_PROTO_TAG_HOST_AVAILABILITY		"host availability"
#define TRX_PROTO_TAG_HISTORY_DATA		"history data"
#define TRX_PROTO_TAG_DISCOVERY_DATA		"discovery data"
#define TRX_PROTO_TAG_AUTO_REGISTRATION		"auto registration"
#define TRX_PROTO_TAG_MORE			"more"
#define TRX_PROTO_TAG_ITEMID			"itemid"
#define TRX_PROTO_TAG_TTL			"ttl"
#define TRX_PROTO_TAG_COMMANDTYPE		"commandtype"
#define TRX_PROTO_TAG_COMMAND			"command"
#define TRX_PROTO_TAG_EXECUTE_ON		"execute_on"
#define TRX_PROTO_TAG_AUTHTYPE			"authtype"
#define TRX_PROTO_TAG_PUBLICKEY			"publickey"
#define TRX_PROTO_TAG_PRIVATEKEY		"privatekey"
#define TRX_PROTO_TAG_PARENT_TASKID		"parent_taskid"
#define TRX_PROTO_TAG_TASKS			"tasks"
#define TRX_PROTO_TAG_ALERTID			"alertid"
#define TRX_PROTO_TAG_JMX_ENDPOINT		"jmx_endpoint"
#define TRX_PROTO_TAG_EVENTID			"eventid"
#define TRX_PROTO_TAG_NAME			"name"
#define TRX_PROTO_TAG_HOSTS			"hosts"
#define TRX_PROTO_TAG_GROUPS			"groups"
#define TRX_PROTO_TAG_APPLICATIONS		"applications"
#define TRX_PROTO_TAG_TAGS			"tags"
#define TRX_PROTO_TAG_TAG			"tag"
#define TRX_PROTO_TAG_PROBLEM_EVENTID		"p_eventid"
#define TRX_PROTO_TAG_ITEMID			"itemid"
#define TRX_PROTO_TAG_COUNT			"count"
#define TRX_PROTO_TAG_MIN			"min"
#define TRX_PROTO_TAG_AVG			"avg"
#define TRX_PROTO_TAG_MAX			"max"
#define TRX_PROTO_TAG_SESSION			"session"
#define TRX_PROTO_TAG_ID			"id"
#define TRX_PROTO_TAG_PARAMS			"params"
#define TRX_PROTO_TAG_FROM			"from"
#define TRX_PROTO_TAG_TO			"to"
#define TRX_PROTO_TAG_HISTORY			"history"
#define TRX_PROTO_TAG_TIMESTAMP			"timestamp"
#define TRX_PROTO_TAG_ERROR_HANDLER		"error_handler"
#define TRX_PROTO_TAG_ERROR_HANDLER_PARAMS	"error_handler_params"
#define TRX_PROTO_TAG_VALUE_TYPE		"value_type"
#define TRX_PROTO_TAG_STEPS			"steps"
#define TRX_PROTO_TAG_ACTION			"action"
#define TRX_PROTO_TAG_FAILED			"failed"
#define TRX_PROTO_TAG_RESULT			"result"
#define TRX_PROTO_TAG_LINE_RAW			"line_raw"
#define TRX_PROTO_TAG_LABELS			"labels"
#define TRX_PROTO_TAG_HELP			"help"
#define TRX_PROTO_TAG_MEDIATYPEID		"mediatypeid"
#define TRX_PROTO_TAG_SENDTO			"sendto"
#define TRX_PROTO_TAG_SUBJECT			"subject"
#define TRX_PROTO_TAG_MESSAGE			"message"
#define TRX_PROTO_TAG_PREVIOUS			"previous"
#define TRX_PROTO_TAG_SINGLE			"single"
#define TRX_PROTO_TAG_INTERFACE			"interface"
#define TRX_PROTO_TAG_FLAGS			"flags"
#define TRX_PROTO_TAG_PARAMETERS		"parameters"

#define TRX_PROTO_VALUE_FAILED		"failed"
#define TRX_PROTO_VALUE_SUCCESS		"success"

#define TRX_PROTO_VALUE_GET_ACTIVE_CHECKS	"active checks"
#define TRX_PROTO_VALUE_PROXY_CONFIG		"proxy config"
#define TRX_PROTO_VALUE_PROXY_HEARTBEAT		"proxy heartbeat"
#define TRX_PROTO_VALUE_SENDER_DATA		"sender data"
#define TRX_PROTO_VALUE_AGENT_DATA		"agent data"
#define TRX_PROTO_VALUE_COMMAND			"command"
#define TRX_PROTO_VALUE_JAVA_GATEWAY_INTERNAL	"java gateway internal"
#define TRX_PROTO_VALUE_JAVA_GATEWAY_JMX	"java gateway jmx"
#define TRX_PROTO_VALUE_GET_QUEUE		"queue.get"
#define TRX_PROTO_VALUE_GET_STATUS		"status.get"
#define TRX_PROTO_VALUE_PROXY_DATA		"proxy data"
#define TRX_PROTO_VALUE_PROXY_TASKS		"proxy tasks"

#define TRX_PROTO_VALUE_GET_QUEUE_OVERVIEW	"overview"
#define TRX_PROTO_VALUE_GET_QUEUE_PROXY		"overview by proxy"
#define TRX_PROTO_VALUE_GET_QUEUE_DETAILS	"details"

#define TRX_PROTO_VALUE_GET_STATUS_PING		"ping"
#define TRX_PROTO_VALUE_GET_STATUS_FULL		"full"

#define TRX_PROTO_VALUE_TREEGIX_STATS		"treegix.stats"
#define TRX_PROTO_VALUE_TREEGIX_STATS_QUEUE	"queue"

#define TRX_PROTO_VALUE_TREEGIX_ALERT_SEND	"alert.send"
#define TRX_PROTO_VALUE_PREPROCESSING_TEST	"preprocessing.test"

typedef enum
{
	TRX_JSON_TYPE_UNKNOWN = 0,
	TRX_JSON_TYPE_STRING,
	TRX_JSON_TYPE_INT,
	TRX_JSON_TYPE_ARRAY,
	TRX_JSON_TYPE_OBJECT,
	TRX_JSON_TYPE_NULL,
	TRX_JSON_TYPE_TRUE,
	TRX_JSON_TYPE_FALSE
}
trx_json_type_t;

typedef enum
{
	TRX_JSON_EMPTY = 0,
	TRX_JSON_COMMA
}
trx_json_status_t;

#define TRX_JSON_STAT_BUF_LEN 4096

struct trx_json
{
	char			*buffer;
	char			buf_stat[TRX_JSON_STAT_BUF_LEN];
	size_t			buffer_allocated;
	size_t			buffer_offset;
	size_t			buffer_size;
	trx_json_status_t	status;
	int			level;
};

struct trx_json_parse
{
	const char		*start;
	const char		*end;
};

const char	*trx_json_strerror(void);

void	trx_json_init(struct trx_json *j, size_t allocate);
void	trx_json_initarray(struct trx_json *j, size_t allocate);
void	trx_json_clean(struct trx_json *j);
void	trx_json_free(struct trx_json *j);
void	trx_json_addobject(struct trx_json *j, const char *name);
void	trx_json_addarray(struct trx_json *j, const char *name);
void	trx_json_addstring(struct trx_json *j, const char *name, const char *string, trx_json_type_t type);
void	trx_json_adduint64(struct trx_json *j, const char *name, trx_uint64_t value);
void	trx_json_addint64(struct trx_json *j, const char *name, trx_int64_t value);
void	trx_json_addraw(struct trx_json *j, const char *name, const char *data);
void	trx_json_addfloat(struct trx_json *j, const char *name, double value);
int	trx_json_close(struct trx_json *j);

int		trx_json_open(const char *buffer, struct trx_json_parse *jp);
const char	*trx_json_next(const struct trx_json_parse *jp, const char *p);
const char	*trx_json_next_value(const struct trx_json_parse *jp, const char *p, char *string, size_t len,
		trx_json_type_t *type);
const char	*trx_json_next_value_dyn(const struct trx_json_parse *jp, const char *p, char **string,
		size_t *string_alloc, trx_json_type_t *type);
const char	*trx_json_pair_next(const struct trx_json_parse *jp, const char *p, char *name, size_t len);
const char	*trx_json_pair_by_name(const struct trx_json_parse *jp, const char *name);
int		trx_json_value_by_name(const struct trx_json_parse *jp, const char *name, char *string, size_t len);
int		trx_json_value_by_name_dyn(const struct trx_json_parse *jp, const char *name, char **string, size_t *string_alloc);
int		trx_json_brackets_open(const char *p, struct trx_json_parse *out);
int		trx_json_brackets_by_name(const struct trx_json_parse *jp, const char *name, struct trx_json_parse *out);
int		trx_json_object_is_empty(const struct trx_json_parse *jp);
int		trx_json_count(const struct trx_json_parse *jp);
const char	*trx_json_decodevalue(const char *p, char *string, size_t size, trx_json_type_t *type);
const char	*trx_json_decodevalue_dyn(const char *p, char **string, size_t *string_alloc, trx_json_type_t *type);
void		trx_json_escape(char **string);

/* jsonpath support */

typedef struct trx_jsonpath_segment trx_jsonpath_segment_t;

typedef struct
{
	trx_jsonpath_segment_t	*segments;
	int			segments_num;
	int			segments_alloc;

	/* set to 1 when jsonpath points at single location */
	unsigned char		definite;
}
trx_jsonpath_t;

void	trx_jsonpath_clear(trx_jsonpath_t *jsonpath);
int	trx_jsonpath_compile(const char *path, trx_jsonpath_t *jsonpath);
int	trx_jsonpath_query(const struct trx_json_parse *jp, const char *path, char **output);

#endif /* TREEGIX_ZJSON_H */
