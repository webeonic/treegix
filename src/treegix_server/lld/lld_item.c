

#include "lld.h"
#include "db.h"
#include "log.h"
#include "trxalgo.h"
#include "trxserver.h"
#include "trxregexp.h"
#include "trxprometheus.h"

typedef struct
{
	trx_uint64_t		itemid;
	trx_uint64_t		valuemapid;
	trx_uint64_t		interfaceid;
	trx_uint64_t		master_itemid;
	char			*name;
	char			*key;
	char			*delay;
	char			*history;
	char			*trends;
	char			*trapper_hosts;
	char			*units;
	char			*formula;
	char			*logtimefmt;
	char			*params;
	char			*ipmi_sensor;
	char			*snmp_community;
	char			*snmp_oid;
	char			*snmpv3_securityname;
	char			*snmpv3_authpassphrase;
	char			*snmpv3_privpassphrase;
	char			*snmpv3_contextname;
	char			*username;
	char			*password;
	char			*publickey;
	char			*privatekey;
	char			*description;
	char			*port;
	char			*jmx_endpoint;
	char			*timeout;
	char			*url;
	char			*query_fields;
	char			*posts;
	char			*status_codes;
	char			*http_proxy;
	char			*headers;
	char			*ssl_cert_file;
	char			*ssl_key_file;
	char			*ssl_key_password;
	unsigned char		verify_peer;
	unsigned char		verify_host;
	unsigned char		follow_redirects;
	unsigned char		post_type;
	unsigned char		retrieve_mode;
	unsigned char		request_method;
	unsigned char		output_format;
	unsigned char		type;
	unsigned char		value_type;
	unsigned char		status;
	unsigned char		snmpv3_securitylevel;
	unsigned char		snmpv3_authprotocol;
	unsigned char		snmpv3_privprotocol;
	unsigned char		authtype;
	unsigned char		allow_traps;
	trx_vector_ptr_t	lld_rows;
	trx_vector_ptr_t	applications;
	trx_vector_ptr_t	preproc_ops;
}
trx_lld_item_prototype_t;

#define	TRX_DEPENDENT_ITEM_MAX_COUNT	29999
#define	TRX_DEPENDENT_ITEM_MAX_LEVELS	3

typedef struct
{
	trx_uint64_t		itemid;
	trx_uint64_t		master_itemid;
	unsigned char		item_flags;
}
trx_item_dependence_t;

typedef struct
{
	trx_uint64_t		itemid;
	trx_uint64_t		parent_itemid;
	trx_uint64_t		master_itemid;
#define TRX_FLAG_LLD_ITEM_UNSET				__UINT64_C(0x0000000000000000)
#define TRX_FLAG_LLD_ITEM_DISCOVERED			__UINT64_C(0x0000000000000001)
#define TRX_FLAG_LLD_ITEM_UPDATE_NAME			__UINT64_C(0x0000000000000002)
#define TRX_FLAG_LLD_ITEM_UPDATE_KEY			__UINT64_C(0x0000000000000004)
#define TRX_FLAG_LLD_ITEM_UPDATE_TYPE			__UINT64_C(0x0000000000000008)
#define TRX_FLAG_LLD_ITEM_UPDATE_VALUE_TYPE		__UINT64_C(0x0000000000000010)
#define TRX_FLAG_LLD_ITEM_UPDATE_DELAY			__UINT64_C(0x0000000000000040)
#define TRX_FLAG_LLD_ITEM_UPDATE_HISTORY		__UINT64_C(0x0000000000000100)
#define TRX_FLAG_LLD_ITEM_UPDATE_TRENDS			__UINT64_C(0x0000000000000200)
#define TRX_FLAG_LLD_ITEM_UPDATE_TRAPPER_HOSTS		__UINT64_C(0x0000000000000400)
#define TRX_FLAG_LLD_ITEM_UPDATE_UNITS			__UINT64_C(0x0000000000000800)
#define TRX_FLAG_LLD_ITEM_UPDATE_FORMULA		__UINT64_C(0x0000000000004000)
#define TRX_FLAG_LLD_ITEM_UPDATE_LOGTIMEFMT		__UINT64_C(0x0000000000008000)
#define TRX_FLAG_LLD_ITEM_UPDATE_VALUEMAPID		__UINT64_C(0x0000000000010000)
#define TRX_FLAG_LLD_ITEM_UPDATE_PARAMS			__UINT64_C(0x0000000000020000)
#define TRX_FLAG_LLD_ITEM_UPDATE_IPMI_SENSOR		__UINT64_C(0x0000000000040000)
#define TRX_FLAG_LLD_ITEM_UPDATE_SNMP_COMMUNITY		__UINT64_C(0x0000000000080000)
#define TRX_FLAG_LLD_ITEM_UPDATE_SNMP_OID		__UINT64_C(0x0000000000100000)
#define TRX_FLAG_LLD_ITEM_UPDATE_PORT			__UINT64_C(0x0000000000200000)
#define TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_SECURITYNAME	__UINT64_C(0x0000000000400000)
#define TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_SECURITYLEVEL	__UINT64_C(0x0000000000800000)
#define TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_AUTHPROTOCOL	__UINT64_C(0x0000000001000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_AUTHPASSPHRASE	__UINT64_C(0x0000000002000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_PRIVPROTOCOL	__UINT64_C(0x0000000004000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_PRIVPASSPHRASE	__UINT64_C(0x0000000008000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_AUTHTYPE		__UINT64_C(0x0000000010000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_USERNAME		__UINT64_C(0x0000000020000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_PASSWORD		__UINT64_C(0x0000000040000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_PUBLICKEY		__UINT64_C(0x0000000080000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_PRIVATEKEY		__UINT64_C(0x0000000100000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_DESCRIPTION		__UINT64_C(0x0000000200000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_INTERFACEID		__UINT64_C(0x0000000400000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_CONTEXTNAME	__UINT64_C(0x0000000800000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_JMX_ENDPOINT		__UINT64_C(0x0000001000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_MASTER_ITEM		__UINT64_C(0x0000002000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_TIMEOUT		__UINT64_C(0x0000004000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_URL			__UINT64_C(0x0000008000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_QUERY_FIELDS		__UINT64_C(0x0000010000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_POSTS			__UINT64_C(0x0000020000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_STATUS_CODES		__UINT64_C(0x0000040000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_FOLLOW_REDIRECTS	__UINT64_C(0x0000080000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_POST_TYPE		__UINT64_C(0x0000100000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_HTTP_PROXY		__UINT64_C(0x0000200000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_HEADERS		__UINT64_C(0x0000400000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_RETRIEVE_MODE		__UINT64_C(0x0000800000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_REQUEST_METHOD		__UINT64_C(0x0001000000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_OUTPUT_FORMAT		__UINT64_C(0x0002000000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_SSL_CERT_FILE		__UINT64_C(0x0004000000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_FILE		__UINT64_C(0x0008000000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_PASSWORD	__UINT64_C(0x0010000000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_VERIFY_PEER		__UINT64_C(0x0020000000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_VERIFY_HOST		__UINT64_C(0x0040000000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE_ALLOW_TRAPS		__UINT64_C(0x0080000000000000)
#define TRX_FLAG_LLD_ITEM_UPDATE			(~TRX_FLAG_LLD_ITEM_DISCOVERED)
	trx_uint64_t		flags;
	char			*key_proto;
	char			*name;
	char			*name_proto;
	char			*key;
	char			*key_orig;
	char			*delay;
	char			*delay_orig;
	char			*history;
	char			*history_orig;
	char			*trends;
	char			*trends_orig;
	char			*units;
	char			*units_orig;
	char			*params;
	char			*params_orig;
	char			*username;
	char			*username_orig;
	char			*password;
	char			*password_orig;
	char			*ipmi_sensor;
	char			*ipmi_sensor_orig;
	char			*snmp_oid;
	char			*snmp_oid_orig;
	char			*description;
	char			*description_orig;
	char			*jmx_endpoint;
	char			*jmx_endpoint_orig;
	char			*timeout;
	char			*timeout_orig;
	char			*url;
	char			*url_orig;
	char			*query_fields;
	char			*query_fields_orig;
	char			*posts;
	char			*posts_orig;
	char			*status_codes;
	char			*status_codes_orig;
	char			*http_proxy;
	char			*http_proxy_orig;
	char			*headers;
	char			*headers_orig;
	char			*ssl_cert_file;
	char			*ssl_cert_file_orig;
	char			*ssl_key_file;
	char			*ssl_key_file_orig;
	char			*ssl_key_password;
	char			*ssl_key_password_orig;
	int			lastcheck;
	int			ts_delete;
	const trx_lld_row_t	*lld_row;
	trx_vector_ptr_t	preproc_ops;
	trx_vector_ptr_t	dependent_items;
	unsigned char		type;
}
trx_lld_item_t;

typedef struct
{
	trx_uint64_t	item_preprocid;
	int		step;
	int		type;
	int		error_handler;
	char		*params;
	char		*error_handler_params;

#define TRX_FLAG_LLD_ITEM_PREPROC_UNSET				__UINT64_C(0x00)
#define TRX_FLAG_LLD_ITEM_PREPROC_DISCOVERED			__UINT64_C(0x01)
#define TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_TYPE			__UINT64_C(0x02)
#define TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_PARAMS			__UINT64_C(0x04)
#define TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER		__UINT64_C(0x08)
#define TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER_PARAMS	__UINT64_C(0x10)
#define TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_STEP			__UINT64_C(0x20)
#define TRX_FLAG_LLD_ITEM_PREPROC_UPDATE				\
		(TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_TYPE |		\
		TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_PARAMS |		\
		TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER |	\
		TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER_PARAMS |	\
		TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_STEP			\
		)
	trx_uint64_t	flags;
}
trx_lld_item_preproc_t;

/* item index by prototype (parent) id and lld row */
typedef struct
{
	trx_uint64_t	parent_itemid;
	trx_lld_row_t	*lld_row;
	trx_lld_item_t	*item;
}
trx_lld_item_index_t;

typedef struct
{
	trx_uint64_t	application_prototypeid;
	trx_uint64_t	itemid;
	char		*name;
}
trx_lld_application_prototype_t;

typedef struct
{
	trx_uint64_t		applicationid;
	trx_uint64_t		application_prototypeid;
	trx_uint64_t		application_discoveryid;
	int			lastcheck;
	int			ts_delete;
#define TRX_FLAG_LLD_APPLICATION_UNSET			__UINT64_C(0x0000000000000000)
#define TRX_FLAG_LLD_APPLICATION_DISCOVERED		__UINT64_C(0x0000000000000001)
#define TRX_FLAG_LLD_APPLICATION_UPDATE_NAME		__UINT64_C(0x0000000000000002)
#define TRX_FLAG_LLD_APPLICATION_ADD_DISCOVERY		__UINT64_C(0x0000000100000000)
#define TRX_FLAG_LLD_APPLICATION_REMOVE_DISCOVERY	__UINT64_C(0x0000000200000000)
#define TRX_FLAG_LLD_APPLICATION_REMOVE			__UINT64_C(0x0000000400000000)
	trx_uint64_t		flags;
	char			*name;
	char			*name_proto;
	char			*name_orig;
	const trx_lld_row_t	*lld_row;
}
trx_lld_application_t;

/* reference to an item either by its id (existing items) or structure (new items) */
typedef struct
{
	trx_uint64_t	itemid;
	trx_lld_item_t	*item;
}
trx_lld_item_ref_t;

/* reference to an application either by its id (existing applications) or structure (new applications) */
typedef struct
{
	trx_uint64_t		applicationid;
	trx_lld_application_t	*application;
}
trx_lld_application_ref_t;

/* item prototype-application link reference by application id (existing applications) */
/* or application prototype structure (application prototypes)                         */
typedef struct
{
	trx_lld_application_prototype_t	*application_prototype;
	trx_uint64_t			applicationid;
}
trx_lld_item_application_ref_t;

/* item-application link */
typedef struct
{
	trx_uint64_t			itemappid;
	trx_lld_item_ref_t		item_ref;
	trx_lld_application_ref_t	application_ref;
#define TRX_FLAG_LLD_ITEM_APPLICATION_UNSET		__UINT64_C(0x0000000000000000)
#define TRX_FLAG_LLD_ITEM_APPLICATION_DISCOVERED	__UINT64_C(0x0000000000000001)
	trx_uint64_t			flags;
}
trx_lld_item_application_t;

/* application index by prototypeid and lld row */
typedef struct
{
	trx_uint64_t		application_prototypeid;
	const trx_lld_row_t	*lld_row;
	trx_lld_application_t	*application;
}
trx_lld_application_index_t;

/* items index hashset support functions */
static trx_hash_t	lld_item_index_hash_func(const void *data)
{
	trx_lld_item_index_t	*item_index = (trx_lld_item_index_t *)data;
	trx_hash_t		hash;

	hash = TRX_DEFAULT_UINT64_HASH_ALGO(&item_index->parent_itemid,
			sizeof(item_index->parent_itemid), TRX_DEFAULT_HASH_SEED);
	return TRX_DEFAULT_PTR_HASH_ALGO(&item_index->lld_row, sizeof(item_index->lld_row), hash);
}

static int	lld_item_index_compare_func(const void *d1, const void *d2)
{
	trx_lld_item_index_t	*i1 = (trx_lld_item_index_t *)d1;
	trx_lld_item_index_t	*i2 = (trx_lld_item_index_t *)d2;

	TRX_RETURN_IF_NOT_EQUAL(i1->parent_itemid, i2->parent_itemid);
	TRX_RETURN_IF_NOT_EQUAL(i1->lld_row, i2->lld_row);

	return 0;
}

/* application index hashset support functions */
static trx_hash_t	lld_application_index_hash_func(const void *data)
{
	trx_lld_application_index_t	*application_index = (trx_lld_application_index_t *)data;
	trx_hash_t			hash;

	hash = TRX_DEFAULT_UINT64_HASH_ALGO(&application_index->application_prototypeid,
			sizeof(application_index->application_prototypeid), TRX_DEFAULT_HASH_SEED);
	return TRX_DEFAULT_PTR_HASH_ALGO(&application_index->lld_row, sizeof(application_index->lld_row), hash);
}

static int	lld_application_index_compare_func(const void *d1, const void *d2)
{
	trx_lld_application_index_t	*i1 = (trx_lld_application_index_t *)d1;
	trx_lld_application_index_t	*i2 = (trx_lld_application_index_t *)d2;

	TRX_RETURN_IF_NOT_EQUAL(i1->application_prototypeid, i2->application_prototypeid);
	TRX_RETURN_IF_NOT_EQUAL(i1->lld_row, i2->lld_row);

	return 0;
}

/* comparison function for discovered application lookup by name */
static int	lld_application_compare_name(const void *d1, const void *d2)
{
	const trx_lld_application_t	*a1 = *(trx_lld_application_t **)d1;
	const trx_lld_application_t	*a2 = *(trx_lld_application_t **)d2;

	if (0 == (a1->flags & a2->flags))
		return -1;

	if (NULL == a1->name || NULL == a2->name)
		return -1;

	return strcmp(a1->name, a2->name);
}

/* comparison function for discovered application lookup by original name name */
static int	lld_application_compare_name_orig(const void *d1, const void *d2)
{
	const trx_lld_application_t	*a1 = *(trx_lld_application_t **)d1;
	const trx_lld_application_t	*a2 = *(trx_lld_application_t **)d2;

	if (0 == (a1->flags & a2->flags))
		return -1;

	if (NULL == a1->name_orig || NULL == a2->name_orig)
		return -1;

	return strcmp(a1->name_orig, a2->name_orig);
}

/* string pointer hashset (used to check for duplicate item keys) support functions */
static trx_hash_t	lld_items_keys_hash_func(const void *data)
{
	return TRX_DEFAULT_STRING_HASH_FUNC(*(char **)data);
}

static int	lld_items_keys_compare_func(const void *d1, const void *d2)
{
	return TRX_DEFAULT_STR_COMPARE_FUNC(d1, d2);
}

/* items - applications hashset support */
static trx_hash_t	lld_item_application_hash_func(const void *data)
{
	const trx_lld_item_application_t	*item_application = (trx_lld_item_application_t *)data;
	trx_hash_t				hash;

	hash = TRX_DEFAULT_HASH_ALGO(&item_application->item_ref, sizeof(item_application->item_ref),
			TRX_DEFAULT_HASH_SEED);
	return TRX_DEFAULT_HASH_ALGO(&item_application->application_ref, sizeof(item_application->application_ref),
			hash);
}

static int	lld_item_application_compare_func(const void *d1, const void *d2)
{
	const trx_lld_item_application_t	*ia1 = (trx_lld_item_application_t *)d1;
	const trx_lld_item_application_t	*ia2 = (trx_lld_item_application_t *)d2;

	TRX_RETURN_IF_NOT_EQUAL(ia1->item_ref.itemid, ia2->item_ref.itemid);
	TRX_RETURN_IF_NOT_EQUAL(ia1->item_ref.item, ia2->item_ref.item);
	TRX_RETURN_IF_NOT_EQUAL(ia1->application_ref.applicationid, ia2->application_ref.applicationid);
	TRX_RETURN_IF_NOT_EQUAL(ia1->application_ref.application, ia2->application_ref.application);

	return 0;
}

static int	lld_item_preproc_sort_by_step(const void *d1, const void *d2)
{
	trx_lld_item_preproc_t	*op1 = *(trx_lld_item_preproc_t **)d1;
	trx_lld_item_preproc_t	*op2 = *(trx_lld_item_preproc_t **)d2;

	TRX_RETURN_IF_NOT_EQUAL(op1->step, op2->step);
	return 0;
}

static void	lld_application_prototype_free(trx_lld_application_prototype_t *application_prototype)
{
	trx_free(application_prototype->name);
	trx_free(application_prototype);
}

static void	lld_application_free(trx_lld_application_t *application)
{
	trx_free(application->name_orig);
	trx_free(application->name_proto);
	trx_free(application->name);
	trx_free(application);
}

static void	lld_item_preproc_free(trx_lld_item_preproc_t *op)
{
	trx_free(op->params);
	trx_free(op->error_handler_params);
	trx_free(op);
}

static void	lld_item_prototype_free(trx_lld_item_prototype_t *item_prototype)
{
	trx_free(item_prototype->name);
	trx_free(item_prototype->key);
	trx_free(item_prototype->delay);
	trx_free(item_prototype->history);
	trx_free(item_prototype->trends);
	trx_free(item_prototype->trapper_hosts);
	trx_free(item_prototype->units);
	trx_free(item_prototype->formula);
	trx_free(item_prototype->logtimefmt);
	trx_free(item_prototype->params);
	trx_free(item_prototype->ipmi_sensor);
	trx_free(item_prototype->snmp_community);
	trx_free(item_prototype->snmp_oid);
	trx_free(item_prototype->snmpv3_securityname);
	trx_free(item_prototype->snmpv3_authpassphrase);
	trx_free(item_prototype->snmpv3_privpassphrase);
	trx_free(item_prototype->snmpv3_contextname);
	trx_free(item_prototype->username);
	trx_free(item_prototype->password);
	trx_free(item_prototype->publickey);
	trx_free(item_prototype->privatekey);
	trx_free(item_prototype->description);
	trx_free(item_prototype->port);
	trx_free(item_prototype->jmx_endpoint);
	trx_free(item_prototype->timeout);
	trx_free(item_prototype->url);
	trx_free(item_prototype->query_fields);
	trx_free(item_prototype->posts);
	trx_free(item_prototype->status_codes);
	trx_free(item_prototype->http_proxy);
	trx_free(item_prototype->headers);
	trx_free(item_prototype->ssl_cert_file);
	trx_free(item_prototype->ssl_key_file);
	trx_free(item_prototype->ssl_key_password);

	trx_vector_ptr_destroy(&item_prototype->lld_rows);

	trx_vector_ptr_clear_ext(&item_prototype->applications, trx_default_mem_free_func);
	trx_vector_ptr_destroy(&item_prototype->applications);

	trx_vector_ptr_clear_ext(&item_prototype->preproc_ops, (trx_clean_func_t)lld_item_preproc_free);
	trx_vector_ptr_destroy(&item_prototype->preproc_ops);

	trx_free(item_prototype);
}

static void	lld_item_free(trx_lld_item_t *item)
{
	trx_free(item->key_proto);
	trx_free(item->name);
	trx_free(item->name_proto);
	trx_free(item->key);
	trx_free(item->key_orig);
	trx_free(item->delay);
	trx_free(item->delay_orig);
	trx_free(item->history);
	trx_free(item->history_orig);
	trx_free(item->trends);
	trx_free(item->trends_orig);
	trx_free(item->units);
	trx_free(item->units_orig);
	trx_free(item->params);
	trx_free(item->params_orig);
	trx_free(item->ipmi_sensor);
	trx_free(item->ipmi_sensor_orig);
	trx_free(item->snmp_oid);
	trx_free(item->snmp_oid_orig);
	trx_free(item->username);
	trx_free(item->username_orig);
	trx_free(item->password);
	trx_free(item->password_orig);
	trx_free(item->description);
	trx_free(item->description_orig);
	trx_free(item->jmx_endpoint);
	trx_free(item->jmx_endpoint_orig);
	trx_free(item->timeout);
	trx_free(item->timeout_orig);
	trx_free(item->url);
	trx_free(item->url_orig);
	trx_free(item->query_fields);
	trx_free(item->query_fields_orig);
	trx_free(item->posts);
	trx_free(item->posts_orig);
	trx_free(item->status_codes);
	trx_free(item->status_codes_orig);
	trx_free(item->http_proxy);
	trx_free(item->http_proxy_orig);
	trx_free(item->headers);
	trx_free(item->headers_orig);
	trx_free(item->ssl_cert_file);
	trx_free(item->ssl_cert_file_orig);
	trx_free(item->ssl_key_file);
	trx_free(item->ssl_key_file_orig);
	trx_free(item->ssl_key_password);
	trx_free(item->ssl_key_password_orig);

	trx_vector_ptr_clear_ext(&item->preproc_ops, (trx_clean_func_t)lld_item_preproc_free);
	trx_vector_ptr_destroy(&item->preproc_ops);
	trx_vector_ptr_destroy(&item->dependent_items);

	trx_free(item);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_get                                                    *
 *                                                                            *
 * Purpose: retrieves existing items for the specified item prototypes        *
 *                                                                            *
 * Parameters: item_prototypes - [IN] item prototypes                         *
 *             items           - [OUT] list of items                          *
 *                                                                            *
 ******************************************************************************/
static void	lld_items_get(const trx_vector_ptr_t *item_prototypes, trx_vector_ptr_t *items)
{
	DB_RESULT			result;
	DB_ROW				row;
	trx_lld_item_t			*item, *master;
	trx_lld_item_preproc_t		*preproc_op;
	const trx_lld_item_prototype_t	*item_prototype;
	trx_uint64_t			db_valuemapid, db_interfaceid, itemid, master_itemid;
	trx_vector_uint64_t		parent_itemids;
	int				i, index;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&parent_itemids);
	trx_vector_uint64_reserve(&parent_itemids, item_prototypes->values_num);

	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (const trx_lld_item_prototype_t *)item_prototypes->values[i];

		trx_vector_uint64_append(&parent_itemids, item_prototype->itemid);
	}

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select id.itemid,id.key_,id.lastcheck,id.ts_delete,i.name,i.key_,i.type,i.value_type,"
				"i.delay,i.history,i.trends,i.trapper_hosts,i.units,"
				"i.formula,i.logtimefmt,i.valuemapid,i.params,i.ipmi_sensor,"
				"i.snmp_community,i.snmp_oid,i.port,i.snmpv3_securityname,i.snmpv3_securitylevel,"
				"i.snmpv3_authprotocol,i.snmpv3_authpassphrase,i.snmpv3_privprotocol,"
				"i.snmpv3_privpassphrase,i.authtype,i.username,i.password,i.publickey,i.privatekey,"
				"i.description,i.interfaceid,i.snmpv3_contextname,i.jmx_endpoint,i.master_itemid,"
				"i.timeout,i.url,i.query_fields,i.posts,i.status_codes,i.follow_redirects,i.post_type,"
				"i.http_proxy,i.headers,i.retrieve_mode,i.request_method,i.output_format,"
				"i.ssl_cert_file,i.ssl_key_file,i.ssl_key_password,i.verify_peer,i.verify_host,"
				"id.parent_itemid,i.allow_traps"
			" from item_discovery id"
				" join items i"
					" on id.itemid=i.itemid"
			" where");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "id.parent_itemid", parent_itemids.values,
			parent_itemids.values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(itemid, row[54]);

		if (FAIL == (index = trx_vector_ptr_bsearch(item_prototypes, &itemid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item_prototype = (const trx_lld_item_prototype_t *)item_prototypes->values[index];

		item = (trx_lld_item_t *)trx_malloc(NULL, sizeof(trx_lld_item_t));

		TRX_STR2UINT64(item->itemid, row[0]);
		item->parent_itemid = itemid;
		item->key_proto = trx_strdup(NULL, row[1]);
		item->lastcheck = atoi(row[2]);
		item->ts_delete = atoi(row[3]);
		item->name = trx_strdup(NULL, row[4]);
		item->name_proto = NULL;
		item->key = trx_strdup(NULL, row[5]);
		item->key_orig = NULL;
		item->flags = TRX_FLAG_LLD_ITEM_UNSET;

		item->type = item_prototype->type;
		if ((unsigned char)atoi(row[6]) != item_prototype->type)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_TYPE;

		if ((unsigned char)atoi(row[7]) != item_prototype->value_type)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_VALUE_TYPE;

		item->delay = trx_strdup(NULL, row[8]);
		item->delay_orig = NULL;

		item->history = trx_strdup(NULL, row[9]);
		item->history_orig = NULL;

		item->trends = trx_strdup(NULL, row[10]);
		item->trends_orig = NULL;

		if (0 != strcmp(row[11], item_prototype->trapper_hosts))
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_TRAPPER_HOSTS;

		item->units = trx_strdup(NULL, row[12]);
		item->units_orig = NULL;

		if (0 != strcmp(row[13], item_prototype->formula))
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_FORMULA;

		if (0 != strcmp(row[14], item_prototype->logtimefmt))
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_LOGTIMEFMT;

		TRX_DBROW2UINT64(db_valuemapid, row[15]);
		if (db_valuemapid != item_prototype->valuemapid)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_VALUEMAPID;

		item->params = trx_strdup(NULL, row[16]);
		item->params_orig = NULL;

		item->ipmi_sensor = trx_strdup(NULL, row[17]);
		item->ipmi_sensor_orig = NULL;

		if (0 != strcmp(row[18], item_prototype->snmp_community))
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_SNMP_COMMUNITY;

		item->snmp_oid = trx_strdup(NULL, row[19]);
		item->snmp_oid_orig = NULL;

		if (0 != strcmp(row[20], item_prototype->port))
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_PORT;

		if (0 != strcmp(row[21], item_prototype->snmpv3_securityname))
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_SECURITYNAME;

		if ((unsigned char)atoi(row[22]) != item_prototype->snmpv3_securitylevel)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_SECURITYLEVEL;

		if ((unsigned char)atoi(row[23]) != item_prototype->snmpv3_authprotocol)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_AUTHPROTOCOL;

		if (0 != strcmp(row[24], item_prototype->snmpv3_authpassphrase))
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_AUTHPASSPHRASE;

		if ((unsigned char)atoi(row[25]) != item_prototype->snmpv3_privprotocol)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_PRIVPROTOCOL;

		if (0 != strcmp(row[26], item_prototype->snmpv3_privpassphrase))
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_PRIVPASSPHRASE;

		if ((unsigned char)atoi(row[27]) != item_prototype->authtype)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_AUTHTYPE;

		item->username = trx_strdup(NULL, row[28]);
		item->username_orig = NULL;

		item->password = trx_strdup(NULL, row[29]);
		item->password_orig = NULL;

		if (0 != strcmp(row[30], item_prototype->publickey))
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_PUBLICKEY;

		if (0 != strcmp(row[31], item_prototype->privatekey))
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_PRIVATEKEY;

		item->description = trx_strdup(NULL, row[32]);
		item->description_orig = NULL;

		TRX_DBROW2UINT64(db_interfaceid, row[33]);
		if (db_interfaceid != item_prototype->interfaceid)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_INTERFACEID;

		if (0 != strcmp(row[34], item_prototype->snmpv3_contextname))
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_CONTEXTNAME;

		item->jmx_endpoint = trx_strdup(NULL, row[35]);
		item->jmx_endpoint_orig = NULL;

		TRX_DBROW2UINT64(item->master_itemid, row[36]);

		item->timeout = trx_strdup(NULL, row[37]);
		item->timeout_orig = NULL;

		item->url = trx_strdup(NULL, row[38]);
		item->url_orig = NULL;

		item->query_fields = trx_strdup(NULL, row[39]);
		item->query_fields_orig = NULL;

		item->posts = trx_strdup(NULL, row[40]);
		item->posts_orig = NULL;

		item->status_codes = trx_strdup(NULL, row[41]);
		item->status_codes_orig = NULL;

		if ((unsigned char)atoi(row[42]) != item_prototype->follow_redirects)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_FOLLOW_REDIRECTS;

		if ((unsigned char)atoi(row[43]) != item_prototype->post_type)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_POST_TYPE;

		item->http_proxy = trx_strdup(NULL, row[44]);
		item->http_proxy_orig = NULL;

		item->headers = trx_strdup(NULL, row[45]);
		item->headers_orig = NULL;

		if ((unsigned char)atoi(row[46]) != item_prototype->retrieve_mode)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_RETRIEVE_MODE;

		if ((unsigned char)atoi(row[47]) != item_prototype->request_method)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_REQUEST_METHOD;

		if ((unsigned char)atoi(row[48]) != item_prototype->output_format)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_OUTPUT_FORMAT;

		item->ssl_cert_file = trx_strdup(NULL, row[49]);
		item->ssl_cert_file_orig = NULL;

		item->ssl_key_file = trx_strdup(NULL, row[50]);
		item->ssl_key_file_orig = NULL;

		item->ssl_key_password = trx_strdup(NULL, row[51]);
		item->ssl_key_password_orig = NULL;

		if ((unsigned char)atoi(row[52]) != item_prototype->verify_peer)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_VERIFY_PEER;

		if ((unsigned char)atoi(row[53]) != item_prototype->verify_host)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_VERIFY_HOST;

		if ((unsigned char)atoi(row[55]) != item_prototype->allow_traps)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_ALLOW_TRAPS;

		item->lld_row = NULL;

		trx_vector_ptr_create(&item->preproc_ops);
		trx_vector_ptr_create(&item->dependent_items);

		trx_vector_ptr_append(items, item);
	}
	DBfree_result(result);

	trx_vector_ptr_sort(items, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	if (0 == items->values_num)
		goto out;

	for (i = items->values_num - 1; i >= 0; i--)
	{
		item = (trx_lld_item_t *)items->values[i];
		master_itemid = item->master_itemid;

		if (0 != master_itemid && FAIL != (index = trx_vector_ptr_bsearch(items, &master_itemid,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			/* dependent items based on prototypes should contain prototype itemid */
			master = (trx_lld_item_t *)items->values[index];
			master_itemid = master->parent_itemid;
		}

		if (FAIL == (index = trx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item_prototype = (const trx_lld_item_prototype_t *)item_prototypes->values[index];

		if (master_itemid != item_prototype->master_itemid)
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_MASTER_ITEM;

		item->master_itemid = item_prototype->master_itemid;
	}

	sql_offset = 0;
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select ip.item_preprocid,ip.itemid,ip.step,ip.type,ip.params,ip.error_handler,"
				"ip.error_handler_params"
			" from item_discovery id"
				" join item_preproc ip"
					" on id.itemid=ip.itemid"
			" where");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "id.parent_itemid", parent_itemids.values,
			parent_itemids.values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(itemid, row[1]);

		if (FAIL == (index = trx_vector_ptr_bsearch(items, &itemid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item = (trx_lld_item_t *)items->values[index];

		preproc_op = (trx_lld_item_preproc_t *)trx_malloc(NULL, sizeof(trx_lld_item_preproc_t));
		preproc_op->flags = TRX_FLAG_LLD_ITEM_PREPROC_UNSET;
		TRX_STR2UINT64(preproc_op->item_preprocid, row[0]);
		preproc_op->step = atoi(row[2]);
		preproc_op->type = atoi(row[3]);
		preproc_op->params = trx_strdup(NULL, row[4]);
		preproc_op->error_handler = atoi(row[5]);
		preproc_op->error_handler_params = trx_strdup(NULL, row[6]);
		trx_vector_ptr_append(&item->preproc_ops, preproc_op);
	}
	DBfree_result(result);
out:
	trx_free(sql);
	trx_vector_uint64_destroy(&parent_itemids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: is_user_macro                                                    *
 *                                                                            *
 * Purpose: checks if string is user macro                                    *
 *                                                                            *
 * Parameters: str - [IN] string to validate                                  *
 *                                                                            *
 * Returns: SUCCEED - either "{$MACRO}" or "{$MACRO:"{#MACRO}"}"              *
 *          FAIL    - not user macro or contains other characters for example:*
 *                    "dummy{$MACRO}", "{$MACRO}dummy" or "{$MACRO}{$MACRO}"  *
 *                                                                            *
 ******************************************************************************/
static int	is_user_macro(const char *str)
{
	trx_token_t	token;

	if (FAIL == trx_token_find(str, 0, &token, TRX_TOKEN_SEARCH_BASIC) ||
			0 == (token.type & TRX_TOKEN_USER_MACRO) ||
			0 != token.loc.l || '\0' != str[token.loc.r + 1])
	{
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_validate_item_field                                          *
 *                                                                            *
 ******************************************************************************/
static void	lld_validate_item_field(trx_lld_item_t *item, char **field, char **field_orig, trx_uint64_t flag,
		size_t field_len, char **error)
{
	if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
		return;

	/* only new items or items with changed data or item type will be validated */
	if (0 != item->itemid && 0 == (item->flags & flag) && 0 == (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_TYPE))
		return;

	if (SUCCEED != trx_is_utf8(*field))
	{
		trx_replace_invalid_utf8(*field);
		*error = trx_strdcatf(*error, "Cannot %s item: value \"%s\" has invalid UTF-8 sequence.\n",
				(0 != item->itemid ? "update" : "create"), *field);
	}
	else if (trx_strlen_utf8(*field) > field_len)
	{
		*error = trx_strdcatf(*error, "Cannot %s item: value \"%s\" is too long.\n",
				(0 != item->itemid ? "update" : "create"), *field);
	}
	else
	{
		int	value;
		char	*errmsg = NULL;

		switch (flag)
		{
			case TRX_FLAG_LLD_ITEM_UPDATE_NAME:
				if ('\0' != **field)
					return;

				*error = trx_strdcatf(*error, "Cannot %s item: name is empty.\n",
						(0 != item->itemid ? "update" : "create"));
				break;
			case TRX_FLAG_LLD_ITEM_UPDATE_DELAY:
				switch (item->type)
				{
					case ITEM_TYPE_TRAPPER:
					case ITEM_TYPE_SNMPTRAP:
					case ITEM_TYPE_DEPENDENT:
						return;
				}

				if (SUCCEED == trx_validate_interval(*field, &errmsg))
					return;

				*error = trx_strdcatf(*error, "Cannot %s item: %s\n",
						(0 != item->itemid ? "update" : "create"), errmsg);
				trx_free(errmsg);

				/* delay alone cannot be rolled back as it depends on item type, revert all updates */
				if (0 != item->itemid)
				{
					item->flags &= TRX_FLAG_LLD_ITEM_DISCOVERED;
					return;
				}
				break;
			case TRX_FLAG_LLD_ITEM_UPDATE_HISTORY:
				if (SUCCEED == is_user_macro(*field))
					return;

				if (SUCCEED == is_time_suffix(*field, &value, TRX_LENGTH_UNLIMITED) && (0 == value ||
						(TRX_HK_HISTORY_MIN <= value && TRX_HK_PERIOD_MAX >= value)))
				{
					return;
				}

				*error = trx_strdcatf(*error, "Cannot %s item: invalid history storage period"
						" \"%s\".\n", (0 != item->itemid ? "update" : "create"), *field);
				break;
			case TRX_FLAG_LLD_ITEM_UPDATE_TRENDS:
				if (SUCCEED == is_user_macro(*field))
					return;

				if (SUCCEED == is_time_suffix(*field, &value, TRX_LENGTH_UNLIMITED) && (0 == value ||
						(TRX_HK_TRENDS_MIN <= value && TRX_HK_PERIOD_MAX >= value)))
				{
					return;
				}

				*error = trx_strdcatf(*error, "Cannot %s item: invalid trends storage period"
						" \"%s\".\n", (0 != item->itemid ? "update" : "create"), *field);
				break;
			default:
				return;
		}
	}

	if (0 != item->itemid)
		lld_field_str_rollback(field, field_orig, &item->flags, flag);
	else
		item->flags &= ~TRX_FLAG_LLD_ITEM_DISCOVERED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_dependence_add                                          *
 *                                                                            *
 * Purpose: add a new dependency                                              *
 *                                                                            *
 * Parameters: item_dependencies - [IN\OUT] list of dependencies              *
 *             itemid            - [IN] item id                               *
 *             master_itemid     - [IN] master item id                        *
 *             item_flags        - [IN] item flags (TRX_FLAG_DISCOVERY_*)     *
 *                                                                            *
 * Returns: item dependence                                                   *
 *                                                                            *
 * Comments: Memory is allocated to store item dependence. This memory must   *
 *           be freed by the caller.                                          *
 *                                                                            *
 ******************************************************************************/
static trx_item_dependence_t	*lld_item_dependence_add(trx_vector_ptr_t *item_dependencies, trx_uint64_t itemid,
		trx_uint64_t master_itemid, unsigned int item_flags)
{
	trx_item_dependence_t	*dependence = (trx_item_dependence_t *)trx_malloc(NULL, sizeof(trx_item_dependence_t));

	dependence->itemid = itemid;
	dependence->master_itemid = master_itemid;
	dependence->item_flags = item_flags;

	trx_vector_ptr_append(item_dependencies, dependence);

	return dependence;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_dependencies_get                                        *
 *                                                                            *
 * Purpose: recursively get dependencies with dependent items taking into     *
 *          account item prototypes                                           *
 *                                                                            *
 * Parameters: item_prototypes   - [IN] item prototypes                       *
 *             item_dependencies - [OUT] list of dependencies                 *
 *                                                                            *
 ******************************************************************************/
static void	lld_item_dependencies_get(const trx_vector_ptr_t *item_prototypes, trx_vector_ptr_t *item_dependencies)
{
#define NEXT_CHECK_BY_ITEM_IDS		0
#define NEXT_CHECK_BY_MASTERITEM_IDS	1

	int			i, check_type;
	trx_vector_uint64_t	processed_masterid, processed_itemid, next_check_itemids, next_check_masterids,
				*check_ids;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset;
	DB_RESULT		result;
	DB_ROW			row;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&processed_masterid);
	trx_vector_uint64_create(&processed_itemid);
	trx_vector_uint64_create(&next_check_itemids);
	trx_vector_uint64_create(&next_check_masterids);

	/* collect the item id of prototypes for searching dependencies into database */
	for (i = 0; i < item_prototypes->values_num; i++)
	{
		const trx_lld_item_prototype_t	*item_prototype;

		item_prototype = (const trx_lld_item_prototype_t *)item_prototypes->values[i];

		if (0 != item_prototype->master_itemid)
		{
			lld_item_dependence_add(item_dependencies, item_prototype->itemid,
					item_prototype->master_itemid, TRX_FLAG_DISCOVERY_PROTOTYPE);
			trx_vector_uint64_append(&next_check_itemids, item_prototype->master_itemid);
			trx_vector_uint64_append(&next_check_masterids, item_prototype->master_itemid);
		}
	}

	/* search dependency in two directions (masteritem_id->itemid and itemid->masteritem_id) */
	while (0 < next_check_itemids.values_num || 0 < next_check_masterids.values_num)
	{
		if (0 < next_check_itemids.values_num)
		{
			check_type = NEXT_CHECK_BY_ITEM_IDS;
			check_ids = &next_check_itemids;
		}
		else
		{
			check_type = NEXT_CHECK_BY_MASTERITEM_IDS;
			check_ids = &next_check_masterids;
		}

		sql_offset = 0;
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select itemid,master_itemid,flags from items where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset,
				NEXT_CHECK_BY_ITEM_IDS == check_type ? "itemid" : "master_itemid",
				check_ids->values, check_ids->values_num);

		if (NEXT_CHECK_BY_ITEM_IDS == check_type)
			trx_vector_uint64_append_array(&processed_itemid, check_ids->values, check_ids->values_num);
		else
			trx_vector_uint64_append_array(&processed_masterid, check_ids->values, check_ids->values_num);

		trx_vector_uint64_clear(check_ids);

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			trx_item_dependence_t	*dependence = NULL;
			trx_uint64_t		itemid, master_itemid;
			unsigned int		item_flags;

			TRX_STR2UINT64(itemid, row[0]);
			TRX_DBROW2UINT64(master_itemid, row[1]);
			TRX_STR2UCHAR(item_flags, row[2]);

			for (i = 0; i < item_dependencies->values_num; i++)
			{
				dependence = (trx_item_dependence_t *)item_dependencies->values[i];
				if (dependence->itemid == itemid && dependence->master_itemid == master_itemid)
					break;
			}

			if (i == item_dependencies->values_num)
			{
				dependence = lld_item_dependence_add(item_dependencies, itemid, master_itemid,
						item_flags);
			}

			if (FAIL == trx_vector_uint64_search(&processed_masterid, dependence->itemid,
					TRX_DEFAULT_UINT64_COMPARE_FUNC))
			{
				trx_vector_uint64_append(&next_check_masterids, dependence->itemid);
			}

			if (NEXT_CHECK_BY_ITEM_IDS != check_type || 0 == dependence->master_itemid)
				continue;

			if (FAIL == trx_vector_uint64_search(&processed_itemid, dependence->master_itemid,
					TRX_DEFAULT_UINT64_COMPARE_FUNC))
			{
				trx_vector_uint64_append(&next_check_itemids, dependence->master_itemid);
			}
		}
		DBfree_result(result);
	}
	trx_free(sql);

	trx_vector_uint64_destroy(&processed_masterid);
	trx_vector_uint64_destroy(&processed_itemid);
	trx_vector_uint64_destroy(&next_check_itemids);
	trx_vector_uint64_destroy(&next_check_masterids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

#undef NEXT_CHECK_BY_ITEM_IDS
#undef NEXT_CHECK_BY_MASTERITEM_IDS
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_dependencies_count                                      *
 *                                                                            *
 * Purpose: recursively count the number of dependencies                      *
 *                                                                            *
 * Parameters: itemid            - [IN] item ID to be checked                 *
 *             dependencies      - [IN] item dependencies                     *
 *             processed_itemids - [IN\OUT] list of checked item ids          *
 *             dependencies_num  - [IN\OUT] number of dependencies            *
 *             depth_level       - [IN\OUT] depth level                       *
 *                                                                            *
 * Returns: SUCCEED - the number of dependencies was successfully counted     *
 *          FAIL    - the limit of dependencies is reached                    *
 *                                                                            *
 ******************************************************************************/
static int	lld_item_dependencies_count(const trx_uint64_t itemid, const trx_vector_ptr_t *dependencies,
		trx_vector_uint64_t *processed_itemids, int *dependencies_num, unsigned char *depth_level)
{
	int	ret = FAIL, i, curr_depth_calculated = 0;

	for (i = 0; i < dependencies->values_num; i++)
	{
		trx_item_dependence_t	*dep = (trx_item_dependence_t *)dependencies->values[i];

		/* check if item is a master for someone else */
		if (dep->master_itemid != itemid)
			continue;

		/* check the limit of dependent items */
		if (0 == (dep->item_flags & TRX_FLAG_DISCOVERY_PROTOTYPE) &&
				TRX_DEPENDENT_ITEM_MAX_COUNT <= ++(*dependencies_num))
		{
			goto out;
		}

		/* check the depth level */
		if (0 == curr_depth_calculated)
		{
			curr_depth_calculated = 1;

			if (TRX_DEPENDENT_ITEM_MAX_LEVELS < ++(*depth_level))
			{
				/* API shouldn't allow to create dependencies deeper */
				THIS_SHOULD_NEVER_HAPPEN;
				goto out;
			}
		}

		/* check if item was calculated in previous iterations */
		if (FAIL != trx_vector_uint64_search(processed_itemids, dep->itemid, TRX_DEFAULT_UINT64_COMPARE_FUNC))
			continue;

		if (SUCCEED != lld_item_dependencies_count(dep->itemid, dependencies, processed_itemids,
				dependencies_num, depth_level))
		{
			goto out;
		}

		/* add counted item id */
		trx_vector_uint64_append(processed_itemids, dep->itemid);
	}

	ret = SUCCEED;
out:
	if (1 == curr_depth_calculated)
		(*depth_level)--;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_dependencies_check                                      *
 *                                                                            *
 * Purpose: check the limits of dependent items                               *
 *                                                                            *
 * Parameters: item             - [IN] discovered item                        *
 *             item_prototype   - [IN] item prototype to be checked for limit *
 *             dependencies     - [IN] item dependencies                      *
 *                                                                            *
 * Returns: SUCCEED - the check was successful                                *
 *          FAIL    - the limit of dependencies is exceeded                   *
 *                                                                            *
 ******************************************************************************/
static int	lld_item_dependencies_check(const trx_lld_item_t *item, const trx_lld_item_prototype_t *item_prototype,
		trx_vector_ptr_t *dependencies)
{
	trx_item_dependence_t	*dependence = NULL, *top_dependence = NULL, *tmp_dep;
	int 			ret = FAIL, i, dependence_num = 0, item_in_deps = FAIL;
	unsigned char		depth_level = 0;
	trx_vector_uint64_t	processed_itemids;

	/* find the dependency of the item by item id */
	for (i = 0; i < dependencies->values_num; i++)
	{
		dependence = (trx_item_dependence_t *)dependencies->values[i];
		if (item_prototype->itemid == dependence->itemid)
			break;
	}

	if (NULL == dependence || i == dependencies->values_num)
		return SUCCEED;

	/* find the top dependency that doesn't have a master item id */
	while (NULL == top_dependence)
	{
		for (i = 0; i < dependencies->values_num; i++)
		{
			tmp_dep = (trx_item_dependence_t *)dependencies->values[i];

			if (item->itemid == tmp_dep->itemid)
				item_in_deps = SUCCEED;

			if (dependence->master_itemid == tmp_dep->itemid)
			{
				dependence = tmp_dep;
				break;
			}
		}

		if (0 == dependence->master_itemid)
		{
			top_dependence = dependence;
		}
		else if (TRX_DEPENDENT_ITEM_MAX_LEVELS < ++depth_level)
		{
			/* API shouldn't allow to create dependencies deeper than TRX_DEPENDENT_ITEM_MAX_LEVELS */
			THIS_SHOULD_NEVER_HAPPEN;
			goto out;
		}
	}

	depth_level = 0;
	trx_vector_uint64_create(&processed_itemids);

	ret = lld_item_dependencies_count(top_dependence->itemid, dependencies, &processed_itemids, &dependence_num,
			&depth_level);

	trx_vector_uint64_destroy(&processed_itemids);

	if (SUCCEED == ret && SUCCEED != item_in_deps
			&& 0 == (top_dependence->item_flags & TRX_FLAG_DISCOVERY_PROTOTYPE))
	{
		lld_item_dependence_add(dependencies, item_prototype->itemid, item->master_itemid,
				TRX_FLAG_DISCOVERY_CREATED);
	}

out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_preproc_step_validate                                  *
 *                                                                            *
 * Purpose: validation of a item preprocessing step expressions for discovery *
 *          process                                                           *
 *                                                                            *
 * Parameters: pp       - [IN] the item preprocessing step                    *
 *             itemid   - [IN] item ID for logging                            *
 *             error    - [IN/OUT] the lld error message                      *
 *                                                                            *
 * Return value: SUCCEED - if preprocessing step is valid                     *
 *               FAIL    - if preprocessing step is not valid                 *
 *                                                                            *
 ******************************************************************************/
static int	lld_items_preproc_step_validate(const trx_lld_item_preproc_t * pp, trx_uint64_t itemid, char ** error)
{
	int		ret = SUCCEED;
	trx_token_t	token;
	char		err[MAX_STRING_LEN], *errmsg = NULL;
	char		param1[ITEM_PREPROC_PARAMS_LEN * TRX_MAX_BYTES_IN_UTF8_CHAR + 1], *param2;
	const char*	regexp_err = NULL;
	trx_uint64_t	value_ui64;
	trx_jsonpath_t	jsonpath;

	*err = '\0';

	if (0 == (pp->flags & TRX_FLAG_LLD_ITEM_PREPROC_UPDATE)
			|| (SUCCEED == trx_token_find(pp->params, 0, &token, TRX_TOKEN_SEARCH_BASIC)
			&& 0 != (token.type & TRX_TOKEN_USER_MACRO)))
	{
		return SUCCEED;
	}

	switch (pp->type)
	{
		case TRX_PREPROC_REGSUB:
			/* break; is not missing here */
		case TRX_PREPROC_ERROR_FIELD_REGEX:
			trx_strlcpy(param1, pp->params, sizeof(param1));
			if (NULL == (param2 = strchr(param1, '\n')))
			{
				trx_snprintf(err, sizeof(err), "cannot find second parameter: %s", pp->params);
				ret = FAIL;
				break;
			}

			*param2 = '\0';

			if (FAIL == (ret = trx_regexp_compile(param1, NULL, &regexp_err)))
			{
				trx_strlcpy(err, regexp_err, sizeof(err));
			}
			break;
		case TRX_PREPROC_JSONPATH:
			/* break; is not missing here */
		case TRX_PREPROC_ERROR_FIELD_JSON:
			if (FAIL == (ret = trx_jsonpath_compile(pp->params, &jsonpath)))
				trx_strlcpy(err, trx_json_strerror(), sizeof(err));
			else
				trx_jsonpath_clear(&jsonpath);
			break;
		case TRX_PREPROC_XPATH:
			/* break; is not missing here */
		case TRX_PREPROC_ERROR_FIELD_XML:
			ret = xml_xpath_check(pp->params, err, sizeof(err));
			break;
		case TRX_PREPROC_MULTIPLIER:
			if (FAIL == (ret = is_double(pp->params)))
				trx_snprintf(err, sizeof(err), "value is not numeric: %s", pp->params);
			break;
		case TRX_PREPROC_VALIDATE_RANGE:
			trx_strlcpy(param1, pp->params, sizeof(param1));
			if (NULL == (param2 = strchr(param1, '\n')))
			{
				trx_snprintf(err, sizeof(err), "cannot find second parameter: %s", pp->params);
				ret = FAIL;
				break;
			}
			*param2++ = '\0';
			trx_lrtrim(param1, " ");
			trx_lrtrim(param2, " ");

			if ('\0' != *param1 && FAIL == (ret = is_double(param1)))
			{
				trx_snprintf(err, sizeof(err), "first parameter is not numeric: %s", param1);
			}
			else if ('\0' != *param2 && FAIL == (ret = is_double(param2)))
			{
				trx_snprintf(err, sizeof(err), "second parameter is not numeric: %s", param2);
			}
			else if ('\0' == *param1 && '\0' == *param2)
			{
				trx_snprintf(err, sizeof(err), "at least one parameter must be defined: %s", pp->params);
				ret = FAIL;
			}
			else if ('\0' != *param1 && '\0' != *param2)
			{
				/* use variants to handle uint64 and double values */
				trx_variant_t	min, max;

				trx_variant_set_numeric(&min, param1);
				trx_variant_set_numeric(&max, param2);

				if (0 < trx_variant_compare(&min, &max))
				{
					trx_snprintf(err, sizeof(err), "first parameter '%s' must be less than second "
							"'%s'", param1, param2);
					ret = FAIL;
				}

				trx_variant_clear(&min);
				trx_variant_clear(&max);
			}

			break;
		case TRX_PREPROC_VALIDATE_REGEX:
			/* break; is not missing here */
		case TRX_PREPROC_VALIDATE_NOT_REGEX:
			if (FAIL == (ret = trx_regexp_compile(pp->params, NULL, &regexp_err)))
				trx_strlcpy(err, regexp_err, sizeof(err));
			break;
		case TRX_PREPROC_THROTTLE_TIMED_VALUE:
			if (SUCCEED != str2uint64(pp->params, "smhdw", &value_ui64) || 0 == value_ui64)
			{
				trx_snprintf(err, sizeof(err), "invalid time interval: %s", pp->params);
				ret = FAIL;
			}
			break;
		case TRX_PREPROC_PROMETHEUS_PATTERN:
			trx_strlcpy(param1, pp->params, sizeof(param1));
			if (NULL == (param2 = strchr(param1, '\n')))
			{
				trx_snprintf(err, sizeof(err), "cannot find second parameter: %s", pp->params);
				ret = FAIL;
				break;
			}
			*param2++ = '\0';

			if (FAIL == trx_prometheus_validate_filter(param1, &errmsg))
			{
				trx_snprintf(err, sizeof(err), "invalid pattern: %s", param1);
				trx_free(errmsg);
				ret = FAIL;
				break;
			}

			if (FAIL == trx_prometheus_validate_label(param2))
			{
				trx_snprintf(err, sizeof(err), "invalid label name: %s", param2);
				ret = FAIL;
				break;
			}

			break;
		case TRX_PREPROC_PROMETHEUS_TO_JSON:
			if (FAIL == trx_prometheus_validate_filter(pp->params, &errmsg))
			{
				trx_snprintf(err, sizeof(err), "invalid pattern: %s", pp->params);
				trx_free(errmsg);
				ret = FAIL;
				break;
			}
	}

	if (SUCCEED != ret)
	{
		*error = trx_strdcatf(*error, "Cannot %s item: invalid value for preprocessing step #%d: %s.\n",
				(0 != itemid ? "update" : "create"), pp->step, err);
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_validate                                               *
 *                                                                            *
 * Parameters: hostid            - [IN] host id                               *
 *             items             - [IN] list of items                         *
 *             item_prototypes   - [IN] the item prototypes                   *
 *             item_dependencies - [IN] list of dependencies                  *
 *             error             - [IN/OUT] the lld error message             *
 *                                                                            *
 *****************************************************************************/
static void	lld_items_validate(trx_uint64_t hostid, trx_vector_ptr_t *items, trx_vector_ptr_t *item_prototypes,
		trx_vector_ptr_t *item_dependencies, char **error)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			i, j;
	trx_lld_item_t		*item;
	trx_vector_uint64_t	itemids;
	trx_vector_str_t	keys;
	trx_hashset_t		items_keys;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&itemids);
	trx_vector_str_create(&keys);		/* list of item keys */

	/* check an item name validity */
	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_lld_item_t *)items->values[i];

		lld_validate_item_field(item, &item->name, &item->name_proto,
				TRX_FLAG_LLD_ITEM_UPDATE_NAME, ITEM_NAME_LEN, error);
		lld_validate_item_field(item, &item->key, &item->key_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_KEY, ITEM_KEY_LEN, error);
		lld_validate_item_field(item, &item->delay, &item->delay_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_DELAY, ITEM_DELAY_LEN, error);
		lld_validate_item_field(item, &item->history, &item->history_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_HISTORY, ITEM_HISTORY_LEN, error);
		lld_validate_item_field(item, &item->trends, &item->trends_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_TRENDS, ITEM_TRENDS_LEN, error);
		lld_validate_item_field(item, &item->units, &item->units_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_UNITS, ITEM_UNITS_LEN, error);
		lld_validate_item_field(item, &item->params, &item->params_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_PARAMS, ITEM_PARAM_LEN, error);
		lld_validate_item_field(item, &item->ipmi_sensor, &item->ipmi_sensor_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_IPMI_SENSOR, ITEM_IPMI_SENSOR_LEN, error);
		lld_validate_item_field(item, &item->snmp_oid, &item->snmp_oid_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_SNMP_OID, ITEM_SNMP_OID_LEN, error);
		lld_validate_item_field(item, &item->username, &item->username_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_USERNAME, ITEM_USERNAME_LEN, error);
		lld_validate_item_field(item, &item->password, &item->password_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_PASSWORD, ITEM_PASSWORD_LEN, error);
		lld_validate_item_field(item, &item->description, &item->description_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_DESCRIPTION, ITEM_DESCRIPTION_LEN, error);
		lld_validate_item_field(item, &item->jmx_endpoint, &item->jmx_endpoint_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_JMX_ENDPOINT, ITEM_JMX_ENDPOINT_LEN, error);
		lld_validate_item_field(item, &item->timeout, &item->timeout_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_TIMEOUT, ITEM_TIMEOUT_LEN, error);
		lld_validate_item_field(item, &item->url, &item->url_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_URL, ITEM_URL_LEN, error);
		lld_validate_item_field(item, &item->query_fields, &item->query_fields_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_QUERY_FIELDS, ITEM_QUERY_FIELDS_LEN, error);
		lld_validate_item_field(item, &item->posts, &item->posts_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_POSTS, ITEM_POSTS_LEN, error);
		lld_validate_item_field(item, &item->status_codes, &item->status_codes_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_STATUS_CODES, ITEM_STATUS_CODES_LEN, error);
		lld_validate_item_field(item, &item->http_proxy, &item->http_proxy_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_HTTP_PROXY, ITEM_HTTP_PROXY_LEN, error);
		lld_validate_item_field(item, &item->headers, &item->headers_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_HEADERS, ITEM_HEADERS_LEN, error);
		lld_validate_item_field(item, &item->ssl_cert_file, &item->ssl_cert_file_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_SSL_CERT_FILE, ITEM_SSL_CERT_FILE_LEN, error);
		lld_validate_item_field(item, &item->ssl_key_file, &item->ssl_key_file_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_FILE, ITEM_SSL_KEY_FILE_LEN, error);
		lld_validate_item_field(item, &item->ssl_key_password, &item->ssl_key_password_orig,
				TRX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_PASSWORD, ITEM_SSL_KEY_PASSWORD_LEN, error);
	}

	/* check duplicated item keys */

	trx_hashset_create(&items_keys, items->values_num, lld_items_keys_hash_func, lld_items_keys_compare_func);

	/* add 'good' (existing, discovered and not updated) keys to the hashset */
	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_lld_item_t *)items->values[i];

		if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		/* skip new or updated item keys */
		if (0 == item->itemid || 0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_KEY))
			continue;

		trx_hashset_insert(&items_keys, &item->key, sizeof(char *));
	}

	/* check new and updated keys for duplicated keys in discovered items */
	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_lld_item_t *)items->values[i];

		if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		/* only new items or items with changed key will be validated */
		if (0 != item->itemid && 0 == (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_KEY))
			continue;

		if (NULL != trx_hashset_search(&items_keys, &item->key))
		{
			*error = trx_strdcatf(*error, "Cannot %s item: item with the same key \"%s\" already exists.\n",
						(0 != item->itemid ? "update" : "create"), item->key);

			if (0 != item->itemid)
			{
				lld_field_str_rollback(&item->key, &item->key_orig, &item->flags,
						TRX_FLAG_LLD_ITEM_UPDATE_KEY);
			}
			else
				item->flags &= ~TRX_FLAG_LLD_ITEM_DISCOVERED;
		}
		else
			trx_hashset_insert(&items_keys, &item->key, sizeof(char *));
	}

	trx_hashset_destroy(&items_keys);

	/* check preprocessing steps for new and updated discovered items */
	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_lld_item_t *)items->values[i];

		if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		for (j = 0; j < item->preproc_ops.values_num; j++)
		{
			if (SUCCEED != lld_items_preproc_step_validate(item->preproc_ops.values[j], item->itemid,
					error))
			{
				item->flags &= ~TRX_FLAG_LLD_ITEM_DISCOVERED;
				break;
			}
		}
	}


	/* check duplicated keys in DB */
	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_lld_item_t *)items->values[i];

		if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		if (0 != item->itemid)
			trx_vector_uint64_append(&itemids, item->itemid);

		if (0 != item->itemid && 0 == (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_KEY))
			continue;

		trx_vector_str_append(&keys, item->key);
	}

	if (0 != keys.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 256, sql_offset = 0;

		sql = (char *)trx_malloc(sql, sql_alloc);

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select key_"
				" from items"
				" where hostid=" TRX_FS_UI64
					" and",
				hostid);
		DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "key_",
				(const char **)keys.values, keys.values_num);

		if (0 != itemids.values_num)
		{
			trx_vector_uint64_sort(&itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and not");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid",
					itemids.values, itemids.values_num);
		}

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			for (i = 0; i < items->values_num; i++)
			{
				item = (trx_lld_item_t *)items->values[i];

				if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
					continue;

				if (0 == strcmp(item->key, row[0]))
				{
					*error = trx_strdcatf(*error, "Cannot %s item:"
							" item with the same key \"%s\" already exists.\n",
							(0 != item->itemid ? "update" : "create"), item->key);

					if (0 != item->itemid)
					{
						lld_field_str_rollback(&item->key, &item->key_orig, &item->flags,
								TRX_FLAG_LLD_ITEM_UPDATE_KEY);
					}
					else
						item->flags &= ~TRX_FLAG_LLD_ITEM_DISCOVERED;

					continue;
				}
			}
		}
		DBfree_result(result);

		trx_free(sql);
	}

	trx_vector_str_destroy(&keys);
	trx_vector_uint64_destroy(&itemids);

	/* check limit of dependent items in the dependency tree */
	if (0 != item_dependencies->values_num)
	{
		for (i = 0; i < items->values_num; i++)
		{
			int				index;
			const trx_lld_item_prototype_t	*item_prototype;

			item = (trx_lld_item_t *)items->values[i];

			if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED) || 0 == item->master_itemid
					|| (0 != item->itemid && 0 == (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_TYPE)))
			{
				continue;
			}

			if (FAIL == (index = trx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			item_prototype = (trx_lld_item_prototype_t *)item_prototypes->values[index];

			if (SUCCEED != lld_item_dependencies_check(item, item_prototype, item_dependencies))
			{
				*error = trx_strdcatf(*error,
						"Cannot create item \"%s\": maximum dependent item count reached.\n",
						item->key);

				item->flags &= ~TRX_FLAG_LLD_ITEM_DISCOVERED;
			}
		}
	}

	/* check for broken dependent items */
	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_lld_item_t *)items->values[i];

		if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
		{
			for (j = 0; j < item->dependent_items.values_num; j++)
			{
				trx_lld_item_t	*dependent;

				dependent = (trx_lld_item_t *)item->dependent_items.values[j];
				dependent->flags &= ~TRX_FLAG_LLD_ITEM_DISCOVERED;
			}
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: substitute_formula_macros                                        *
 *                                                                            *
 * Purpose: substitutes lld macros in calculated item formula expression      *
 *                                                                            *
 * Parameters: data            - [IN/OUT] the expression                      *
 *             jp_row          - [IN] the lld data row                        *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *             error           - [IN] pointer to string for reporting errors  *
 *             max_error_len   - [IN] size of 'error' string                  *
 *                                                                            *
 ******************************************************************************/
static int	substitute_formula_macros(char **data, const struct trx_json_parse *jp_row,
		const trx_vector_ptr_t *lld_macro_paths, char *error, size_t max_error_len)
{
	char		*exp, *tmp, *e;
	size_t		exp_alloc = 128, exp_offset = 0, tmp_alloc = 128, tmp_offset = 0, f_pos, par_l, par_r;
	int		ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	exp = (char *)trx_malloc(NULL, exp_alloc);
	tmp = (char *)trx_malloc(NULL, tmp_alloc);

	for (e = *data; SUCCEED == trx_function_find(e, &f_pos, &par_l, &par_r, error, max_error_len); e += par_r + 1)
	{
		/* substitute LLD macros in the part of the string preceding function parameters */

		trx_strncpy_alloc(&tmp, &tmp_alloc, &tmp_offset, e, par_l + 1);
		if (SUCCEED != substitute_lld_macros(&tmp, jp_row, lld_macro_paths, TRX_MACRO_NUMERIC, error,
				max_error_len))
		{
			goto out;
		}

		tmp_offset = strlen(tmp);
		trx_strncpy_alloc(&exp, &exp_alloc, &exp_offset, tmp, tmp_offset);

		tmp_alloc = tmp_offset + 1;
		tmp_offset = 0;

		/* substitute LLD macros in function parameters */

		if (SUCCEED != substitute_function_lld_param(e + par_l + 1, par_r - (par_l + 1), 1,
				&exp, &exp_alloc, &exp_offset, jp_row, lld_macro_paths, error, max_error_len))
		{
			goto out;
		}

		trx_strcpy_alloc(&exp, &exp_alloc, &exp_offset, ")");
	}

	if (par_l > par_r)
		goto out;

	/* substitute LLD macros in the remaining part */

	trx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, e);
	if (SUCCEED != substitute_lld_macros(&tmp, jp_row, lld_macro_paths, TRX_MACRO_NUMERIC, error, max_error_len))
		goto out;

	trx_strcpy_alloc(&exp, &exp_alloc, &exp_offset, tmp);

	ret = SUCCEED;
out:
	trx_free(tmp);

	if (SUCCEED == ret)
	{
		trx_free(*data);
		*data = exp;
	}
	else
		trx_free(exp);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_make                                                    *
 *                                                                            *
 * Purpose: creates a new item based on item prototype and lld data row       *
 *                                                                            *
 * Parameters: item_prototype  - [IN] the item prototype                      *
 *             lld_row         - [IN] the lld row                             *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *                                                                            *
 * Returns: The created item or NULL if cannot create new item from prototype *
 *                                                                            *
 ******************************************************************************/
static trx_lld_item_t	*lld_item_make(const trx_lld_item_prototype_t *item_prototype, const trx_lld_row_t *lld_row,
		const trx_vector_ptr_t *lld_macro_paths, char **error)
{
	trx_lld_item_t			*item;
	const struct trx_json_parse	*jp_row = (struct trx_json_parse *)&lld_row->jp_row;
	char				err[MAX_STRING_LEN];
	int				ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	item = (trx_lld_item_t *)trx_malloc(NULL, sizeof(trx_lld_item_t));

	item->itemid = 0;
	item->parent_itemid = item_prototype->itemid;
	item->lastcheck = 0;
	item->ts_delete = 0;
	item->type = item_prototype->type;
	item->key_proto = NULL;
	item->master_itemid = item_prototype->master_itemid;

	item->name = trx_strdup(NULL, item_prototype->name);
	item->name_proto = NULL;
	substitute_lld_macros(&item->name, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(item->name, TRX_WHITESPACE);

	item->key = trx_strdup(NULL, item_prototype->key);
	item->key_orig = NULL;
	ret = substitute_key_macros(&item->key, NULL, NULL, jp_row, lld_macro_paths, MACRO_TYPE_ITEM_KEY, err, sizeof(err));

	item->delay = trx_strdup(NULL, item_prototype->delay);
	item->delay_orig = NULL;
	substitute_lld_macros(&item->delay, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(item->delay, TRX_WHITESPACE);

	item->history = trx_strdup(NULL, item_prototype->history);
	item->history_orig = NULL;
	substitute_lld_macros(&item->history, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(item->history, TRX_WHITESPACE);

	item->trends = trx_strdup(NULL, item_prototype->trends);
	item->trends_orig = NULL;
	substitute_lld_macros(&item->trends, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(item->trends, TRX_WHITESPACE);

	item->units = trx_strdup(NULL, item_prototype->units);
	item->units_orig = NULL;
	substitute_lld_macros(&item->units, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(item->units, TRX_WHITESPACE);

	item->params = trx_strdup(NULL, item_prototype->params);
	item->params_orig = NULL;

	if (ITEM_TYPE_CALCULATED == item_prototype->type)
	{
		if (SUCCEED == ret)
			ret = substitute_formula_macros(&item->params, jp_row, lld_macro_paths, err, sizeof(err));
	}
	else
		substitute_lld_macros(&item->params, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);

	trx_lrtrim(item->params, TRX_WHITESPACE);

	item->ipmi_sensor = trx_strdup(NULL, item_prototype->ipmi_sensor);
	item->ipmi_sensor_orig = NULL;
	substitute_lld_macros(&item->ipmi_sensor, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(item->ipmi_sensor, TRX_WHITESPACE); is not missing here */

	item->snmp_oid = trx_strdup(NULL, item_prototype->snmp_oid);
	item->snmp_oid_orig = NULL;
	if (SUCCEED == ret && (ITEM_TYPE_SNMPv1 == item_prototype->type || ITEM_TYPE_SNMPv2c == item_prototype->type ||
			ITEM_TYPE_SNMPv3 == item_prototype->type))
	{
		ret = substitute_key_macros(&item->snmp_oid, NULL, NULL, jp_row, lld_macro_paths, MACRO_TYPE_SNMP_OID, err, sizeof(err));
	}
	trx_lrtrim(item->snmp_oid, TRX_WHITESPACE);

	item->username = trx_strdup(NULL, item_prototype->username);
	item->username_orig = NULL;
	substitute_lld_macros(&item->username, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(item->username, TRX_WHITESPACE); is not missing here */

	item->password = trx_strdup(NULL, item_prototype->password);
	item->password_orig = NULL;
	substitute_lld_macros(&item->password, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(item->password, TRX_WHITESPACE); is not missing here */

	item->description = trx_strdup(NULL, item_prototype->description);
	item->description_orig = NULL;
	substitute_lld_macros(&item->description, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(item->description, TRX_WHITESPACE);

	item->jmx_endpoint = trx_strdup(NULL, item_prototype->jmx_endpoint);
	item->jmx_endpoint_orig = NULL;
	substitute_lld_macros(&item->jmx_endpoint, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(item->ipmi_sensor, TRX_WHITESPACE); is not missing here */

	item->timeout = trx_strdup(NULL, item_prototype->timeout);
	item->timeout_orig = NULL;
	substitute_lld_macros(&item->timeout, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(item->timeout, TRX_WHITESPACE);

	item->url = trx_strdup(NULL, item_prototype->url);
	item->url_orig = NULL;
	substitute_lld_macros(&item->url, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(item->url, TRX_WHITESPACE);

	item->query_fields = trx_strdup(NULL, item_prototype->query_fields);
	item->query_fields_orig = NULL;

	if (SUCCEED == ret)
		ret = substitute_macros_in_json_pairs(&item->query_fields, jp_row, lld_macro_paths, err, sizeof(err));

	item->posts = trx_strdup(NULL, item_prototype->posts);
	item->posts_orig = NULL;

	switch (item_prototype->post_type)
	{
		case TRX_POSTTYPE_JSON:
			substitute_lld_macros(&item->posts, jp_row, lld_macro_paths, TRX_MACRO_JSON, NULL, 0);
			break;
		case TRX_POSTTYPE_XML:
			if (SUCCEED == ret && FAIL == (ret = substitute_macros_xml(&item->posts, NULL, jp_row,
					lld_macro_paths, err, sizeof(err))))
			{
				trx_lrtrim(err, TRX_WHITESPACE);
			}
			break;
		default:
			substitute_lld_macros(&item->posts, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
			/* trx_lrtrim(item->posts, TRX_WHITESPACE); is not missing here */
			break;
	}

	item->status_codes = trx_strdup(NULL, item_prototype->status_codes);
	item->status_codes_orig = NULL;
	substitute_lld_macros(&item->status_codes, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(item->status_codes, TRX_WHITESPACE);

	item->http_proxy = trx_strdup(NULL, item_prototype->http_proxy);
	item->http_proxy_orig = NULL;
	substitute_lld_macros(&item->http_proxy, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(item->http_proxy, TRX_WHITESPACE);

	item->headers = trx_strdup(NULL, item_prototype->headers);
	item->headers_orig = NULL;
	substitute_lld_macros(&item->headers, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(item->headers, TRX_WHITESPACE); is not missing here */

	item->ssl_cert_file = trx_strdup(NULL, item_prototype->ssl_cert_file);
	item->ssl_cert_file_orig = NULL;
	substitute_lld_macros(&item->ssl_cert_file, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(item->ipmi_sensor, TRX_WHITESPACE); is not missing here */

	item->ssl_key_file = trx_strdup(NULL, item_prototype->ssl_key_file);
	item->ssl_key_file_orig = NULL;
	substitute_lld_macros(&item->ssl_key_file, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(item->ipmi_sensor, TRX_WHITESPACE); is not missing here */

	item->ssl_key_password = trx_strdup(NULL, item_prototype->ssl_key_password);
	item->ssl_key_password_orig = NULL;
	substitute_lld_macros(&item->ssl_key_password, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(item->ipmi_sensor, TRX_WHITESPACE); is not missing here */

	item->flags = TRX_FLAG_LLD_ITEM_DISCOVERED;
	item->lld_row = lld_row;

	trx_vector_ptr_create(&item->preproc_ops);
	trx_vector_ptr_create(&item->dependent_items);

	if (SUCCEED != ret)
	{
		*error = trx_strdcatf(*error, "Cannot create item: %s.\n", err);
		lld_item_free(item);
		item = NULL;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return item;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_update                                                  *
 *                                                                            *
 * Purpose: updates an existing item based on item prototype and lld data row *
 *                                                                            *
 * Parameters: item_prototype  - [IN] the item prototype                      *
 *             lld_row         - [IN] the lld row                             *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *             item            - [IN] an existing item or NULL                *
 *                                                                            *
 ******************************************************************************/
static void	lld_item_update(const trx_lld_item_prototype_t *item_prototype, const trx_lld_row_t *lld_row,
		const trx_vector_ptr_t *lld_macro_paths, trx_lld_item_t *item, char **error)
{
	char			*buffer = NULL, err[MAX_STRING_LEN];
	struct trx_json_parse	*jp_row = (struct trx_json_parse *)&lld_row->jp_row;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	buffer = trx_strdup(buffer, item_prototype->name);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(buffer, TRX_WHITESPACE);
	if (0 != strcmp(item->name, buffer))
	{
		item->name_proto = item->name;
		item->name = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_NAME;
	}

	if (0 != strcmp(item->key_proto, item_prototype->key))
	{
		buffer = trx_strdup(buffer, item_prototype->key);

		if (SUCCEED == substitute_key_macros(&buffer, NULL, NULL, jp_row, lld_macro_paths,
				MACRO_TYPE_ITEM_KEY, err, sizeof(err)))
		{
			item->key_orig = item->key;
			item->key = buffer;
			buffer = NULL;
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_KEY;
		}
		else
			*error = trx_strdcatf(*error, "Cannot update item: %s.\n", err);
	}

	buffer = trx_strdup(buffer, item_prototype->delay);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(buffer, TRX_WHITESPACE);
	if (0 != strcmp(item->delay, buffer))
	{
		item->delay_orig = item->delay;
		item->delay = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_DELAY;
	}

	buffer = trx_strdup(buffer, item_prototype->history);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(buffer, TRX_WHITESPACE);
	if (0 != strcmp(item->history, buffer))
	{
		item->history_orig = item->history;
		item->history = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_HISTORY;
	}

	buffer = trx_strdup(buffer, item_prototype->trends);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(buffer, TRX_WHITESPACE);
	if (0 != strcmp(item->trends, buffer))
	{
		item->trends_orig = item->trends;
		item->trends = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_TRENDS;
	}

	buffer = trx_strdup(buffer, item_prototype->units);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(buffer, TRX_WHITESPACE);
	if (0 != strcmp(item->units, buffer))
	{
		item->units_orig = item->units;
		item->units = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_UNITS;
	}

	buffer = trx_strdup(buffer, item_prototype->params);

	if (ITEM_TYPE_CALCULATED == item_prototype->type)
	{
		if (SUCCEED == substitute_formula_macros(&buffer, jp_row, lld_macro_paths, err, sizeof(err)))
		{
			trx_lrtrim(buffer, TRX_WHITESPACE);

			if (0 != strcmp(item->params, buffer))
			{
				item->params_orig = item->params;
				item->params = buffer;
				buffer = NULL;
				item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_PARAMS;
			}
		}
		else
			*error = trx_strdcatf(*error, "Cannot update item: %s.\n", err);
	}
	else
	{
		substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(buffer, TRX_WHITESPACE);

		if (0 != strcmp(item->params, buffer))
		{
			item->params_orig = item->params;
			item->params = buffer;
			buffer = NULL;
			item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_PARAMS;
		}
	}

	buffer = trx_strdup(buffer, item_prototype->ipmi_sensor);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(buffer, TRX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->ipmi_sensor, buffer))
	{
		item->ipmi_sensor_orig = item->ipmi_sensor;
		item->ipmi_sensor = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_IPMI_SENSOR;
	}

	buffer = trx_strdup(buffer, item_prototype->snmp_oid);
	substitute_key_macros(&buffer, NULL, NULL, jp_row, lld_macro_paths, MACRO_TYPE_SNMP_OID, NULL, 0);
	trx_lrtrim(buffer, TRX_WHITESPACE);
	if (0 != strcmp(item->snmp_oid, buffer))
	{
		item->snmp_oid_orig = item->snmp_oid;
		item->snmp_oid = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_SNMP_OID;
	}

	buffer = trx_strdup(buffer, item_prototype->username);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(buffer, TRX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->username, buffer))
	{
		item->username_orig = item->username;
		item->username = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_USERNAME;
	}

	buffer = trx_strdup(buffer, item_prototype->password);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(buffer, TRX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->password, buffer))
	{
		item->password_orig = item->password;
		item->password = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_PASSWORD;
	}

	buffer = trx_strdup(buffer, item_prototype->description);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(buffer, TRX_WHITESPACE);
	if (0 != strcmp(item->description, buffer))
	{
		item->description_orig = item->description;
		item->description = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_DESCRIPTION;
	}

	buffer = trx_strdup(buffer, item_prototype->jmx_endpoint);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(buffer, TRX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->jmx_endpoint, buffer))
	{
		item->jmx_endpoint_orig = item->jmx_endpoint;
		item->jmx_endpoint = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_JMX_ENDPOINT;
	}

	buffer = trx_strdup(buffer, item_prototype->timeout);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(buffer, TRX_WHITESPACE);
	if (0 != strcmp(item->timeout, buffer))
	{
		item->timeout_orig = item->timeout;
		item->timeout = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_TIMEOUT;
	}

	buffer = trx_strdup(buffer, item_prototype->url);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(buffer, TRX_WHITESPACE);
	if (0 != strcmp(item->url, buffer))
	{
		item->url_orig = item->url;
		item->url = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_URL;
	}

	buffer = trx_strdup(buffer, item_prototype->query_fields);

	if (FAIL == substitute_macros_in_json_pairs(&buffer, jp_row, lld_macro_paths, err, sizeof(err)))
		*error = trx_strdcatf(*error, "Cannot update item: %s.\n", err);

	if (0 != strcmp(item->query_fields, buffer))
	{
		item->query_fields_orig = item->query_fields;
		item->query_fields = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_QUERY_FIELDS;
	}

	buffer = trx_strdup(buffer, item_prototype->posts);

	if (TRX_POSTTYPE_JSON == item_prototype->post_type)
	{
		substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_JSON, NULL, 0);
	}
	else if (TRX_POSTTYPE_XML == item_prototype->post_type)
	{
		if (FAIL == substitute_macros_xml(&buffer, NULL, jp_row, lld_macro_paths, err, sizeof(err)))
		{
			trx_lrtrim(err, TRX_WHITESPACE);
			*error = trx_strdcatf(*error, "Cannot update item: %s.\n", err);
		}
	}
	else
		substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(buffer, TRX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->posts, buffer))
	{
		item->posts_orig = item->posts;
		item->posts = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_POSTS;
	}

	buffer = trx_strdup(buffer, item_prototype->status_codes);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(buffer, TRX_WHITESPACE);
	if (0 != strcmp(item->status_codes, buffer))
	{
		item->status_codes_orig = item->status_codes;
		item->status_codes = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_STATUS_CODES;
	}

	buffer = trx_strdup(buffer, item_prototype->http_proxy);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	trx_lrtrim(buffer, TRX_WHITESPACE);
	if (0 != strcmp(item->http_proxy, buffer))
	{
		item->http_proxy_orig = item->http_proxy;
		item->http_proxy = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_HTTP_PROXY;
	}

	buffer = trx_strdup(buffer, item_prototype->headers);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/*trx_lrtrim(buffer, TRX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->headers, buffer))
	{
		item->headers_orig = item->headers;
		item->headers = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_HEADERS;
	}

	buffer = trx_strdup(buffer, item_prototype->ssl_cert_file);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(buffer, TRX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->ssl_cert_file, buffer))
	{
		item->ssl_cert_file_orig = item->ssl_cert_file;
		item->ssl_cert_file = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_SSL_CERT_FILE;
	}

	buffer = trx_strdup(buffer, item_prototype->ssl_key_file);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(buffer, TRX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->ssl_key_file, buffer))
	{
		item->ssl_key_file_orig = item->ssl_key_file;
		item->ssl_key_file = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_FILE;
	}

	buffer = trx_strdup(buffer, item_prototype->ssl_key_password);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
	/* trx_lrtrim(buffer, TRX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->ssl_key_password, buffer))
	{
		item->ssl_key_password_orig = item->ssl_key_password;
		item->ssl_key_password = buffer;
		buffer = NULL;
		item->flags |= TRX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_PASSWORD;
	}

	item->flags |= TRX_FLAG_LLD_ITEM_DISCOVERED;
	item->lld_row = lld_row;

	trx_free(buffer);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_make                                                   *
 *                                                                            *
 * Purpose: updates existing items and creates new ones based on item         *
 *          item prototypes and lld data                                      *
 *                                                                            *
 * Parameters: item_prototypes - [IN] the item prototypes                     *
 *             lld_rows        - [IN] the lld data rows                       *
 *             lld_macro_paths - [IN] use json path to extract from jp_row  *
 *             items           - [IN/OUT] sorted list of items                *
 *             items_index     - [OUT] index of items based on prototype ids  *
 *                                     and lld rows. Used to quckly find an   *
 *                                     item by prototype and lld_row.         *
 *             error           - [IN/OUT] the lld error message               *
 *                                                                            *
 ******************************************************************************/
static void	lld_items_make(const trx_vector_ptr_t *item_prototypes, const trx_vector_ptr_t *lld_rows,
		const trx_vector_ptr_t *lld_macro_paths, trx_vector_ptr_t *items, trx_hashset_t *items_index,
		char **error)
{
	int				i, j, index;
	trx_lld_item_prototype_t	*item_prototype;
	trx_lld_item_t			*item;
	trx_lld_row_t			*lld_row;
	trx_lld_item_index_t		*item_index, item_index_local;
	char				*buffer = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* create the items index */
	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (trx_lld_item_prototype_t *)item_prototypes->values[i];

		for (j = 0; j < lld_rows->values_num; j++)
			trx_vector_ptr_append(&item_prototype->lld_rows, lld_rows->values[j]);
	}

	/* Iterate in reverse order because usually the items are created in the same order as     */
	/* incoming lld rows. Iterating in reverse optimizes lld_row removal from item prototypes. */
	for (i = items->values_num - 1; i >= 0; i--)
	{
		item = (trx_lld_item_t *)items->values[i];

		if (FAIL == (index = trx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item_prototype = (trx_lld_item_prototype_t *)item_prototypes->values[index];

		for (j = item_prototype->lld_rows.values_num - 1; j >= 0; j--)
		{
			lld_row = (trx_lld_row_t *)item_prototype->lld_rows.values[j];

			buffer = trx_strdup(buffer, item->key_proto);

			if (SUCCEED != substitute_key_macros(&buffer, NULL, NULL, &lld_row->jp_row, lld_macro_paths,
					MACRO_TYPE_ITEM_KEY, NULL, 0))
			{
				continue;
			}

			if (0 == strcmp(item->key, buffer))
			{
				item_index_local.parent_itemid = item->parent_itemid;
				item_index_local.lld_row = lld_row;
				item_index_local.item = item;
				trx_hashset_insert(items_index, &item_index_local, sizeof(item_index_local));

				trx_vector_ptr_remove_noorder(&item_prototype->lld_rows, j);
				break;
			}
		}
	}

	trx_free(buffer);

	/* update/create discovered items */
	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (trx_lld_item_prototype_t *)item_prototypes->values[i];
		item_index_local.parent_itemid = item_prototype->itemid;

		for (j = 0; j < lld_rows->values_num; j++)
		{
			item_index_local.lld_row = (trx_lld_row_t *)lld_rows->values[j];

			if (NULL == (item_index = (trx_lld_item_index_t *)trx_hashset_search(items_index, &item_index_local)))
			{
				if (NULL != (item = lld_item_make(item_prototype, item_index_local.lld_row,
						lld_macro_paths, error)))
				{
					/* add the created item to items vector and update index */
					trx_vector_ptr_append(items, item);
					item_index_local.item = item;
					trx_hashset_insert(items_index, &item_index_local, sizeof(item_index_local));
				}
			}
			else
				lld_item_update(item_prototype, item_index_local.lld_row, lld_macro_paths, item_index->item, error);
		}
	}

	trx_vector_ptr_sort(items, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d items", __func__, items->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: substitute_lld_macors_in_preproc_params                          *
 *                                                                            *
 * Purpose: escaping of a symbols in items preprocessing steps for discovery  *
 *          process                                                           *
 *                                                                            *
 * Parameters: type            - [IN] the item preprocessing step type        *
 *             lld_row         - [IN] lld source value                        *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *             sub_params      - [IN/OUT] the preprocessing parameters        *
 *                                                                            *
 ******************************************************************************/
static void	substitute_lld_macros_in_preproc_params(int type, const trx_lld_row_t *lld_row,
		const trx_vector_ptr_t *lld_macro_paths, char **sub_params)
{
	int	params_num = 1, flags1, flags2;

	switch (type)
	{
		case TRX_PREPROC_REGSUB:
		case TRX_PREPROC_ERROR_FIELD_REGEX:
			flags1 = TRX_MACRO_ANY | TRX_TOKEN_REGEXP;
			flags2 = TRX_MACRO_ANY | TRX_TOKEN_REGEXP_OUTPUT;
			params_num = 2;
			break;
		case TRX_PREPROC_VALIDATE_REGEX:
		case TRX_PREPROC_VALIDATE_NOT_REGEX:
			flags1 = TRX_MACRO_ANY | TRX_TOKEN_REGEXP;
			params_num = 1;
			break;
		case TRX_PREPROC_XPATH:
		case TRX_PREPROC_ERROR_FIELD_XML:
			flags1 = TRX_MACRO_ANY | TRX_TOKEN_XPATH;
			params_num = 1;
			break;
		case TRX_PREPROC_PROMETHEUS_PATTERN:
		case TRX_PREPROC_PROMETHEUS_TO_JSON:
			flags1 = TRX_MACRO_ANY | TRX_TOKEN_PROMETHEUS;
			params_num = 1;
			break;
		default:
			flags1 = TRX_MACRO_ANY;
			params_num = 1;
	}

	if (2 == params_num)
	{
		char	*param1, *param2;
		size_t	params_alloc, params_offset = 0;

		trx_strsplit(*sub_params, '\n', &param1, &param2);

		if (NULL == param2)
		{
			trx_free(param1);
			treegix_log(LOG_LEVEL_ERR, "Invalid preprocessing parameters: %s.", *sub_params);
			THIS_SHOULD_NEVER_HAPPEN;
			return;
		}

		substitute_lld_macros(&param1, &lld_row->jp_row, lld_macro_paths, flags1, NULL, 0);
		substitute_lld_macros(&param2, &lld_row->jp_row, lld_macro_paths, flags2, NULL, 0);

		params_alloc = strlen(param1) + strlen(param2) + 2;
		*sub_params = (char*)trx_realloc(*sub_params, params_alloc);

		trx_strcpy_alloc(sub_params, &params_alloc, &params_offset, param1);
		trx_chrcpy_alloc(sub_params, &params_alloc, &params_offset, '\n');
		trx_strcpy_alloc(sub_params, &params_alloc, &params_offset, param2);

		trx_free(param1);
		trx_free(param2);
	}
	else
		substitute_lld_macros(sub_params, &lld_row->jp_row, lld_macro_paths, flags1, NULL, 0);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_preproc_make                                           *
 *                                                                            *
 * Purpose: updates existing items preprocessing operations and create new    *
 *          based on item item prototypes                                     *
 *                                                                            *
 * Parameters: item_prototypes - [IN] the item prototypes                     *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *             items           - [IN/OUT] sorted list of items                *
 *                                                                            *
 ******************************************************************************/
static void	lld_items_preproc_make(const trx_vector_ptr_t *item_prototypes,
		const trx_vector_ptr_t *lld_macro_paths, trx_vector_ptr_t *items)
{
	int				i, j, index, preproc_num;
	trx_lld_item_t			*item;
	trx_lld_item_prototype_t	*item_proto;
	trx_lld_item_preproc_t		*ppsrc, *ppdst;
	char				*buffer = NULL;

	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_lld_item_t *)items->values[i];

		if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		if (FAIL == (index = trx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		trx_vector_ptr_sort(&item->preproc_ops, lld_item_preproc_sort_by_step);

		item_proto = (trx_lld_item_prototype_t *)item_prototypes->values[index];

		preproc_num = MAX(item->preproc_ops.values_num, item_proto->preproc_ops.values_num);

		for (j = 0; j < preproc_num; j++)
		{
			if (j >= item->preproc_ops.values_num)
			{
				ppsrc = (trx_lld_item_preproc_t *)item_proto->preproc_ops.values[j];
				ppdst = (trx_lld_item_preproc_t *)trx_malloc(NULL, sizeof(trx_lld_item_preproc_t));
				ppdst->item_preprocid = 0;
				ppdst->flags = TRX_FLAG_LLD_ITEM_PREPROC_DISCOVERED | TRX_FLAG_LLD_ITEM_PREPROC_UPDATE;
				ppdst->step = ppsrc->step;
				ppdst->type = ppsrc->type;
				ppdst->params = trx_strdup(NULL, ppsrc->params);
				ppdst->error_handler = ppsrc->error_handler;
				ppdst->error_handler_params = trx_strdup(NULL, ppsrc->error_handler_params);

				substitute_lld_macros_in_preproc_params(ppsrc->type, item->lld_row, lld_macro_paths,
						&ppdst->params);
				substitute_lld_macros(&ppdst->error_handler_params, &item->lld_row->jp_row,
						lld_macro_paths, TRX_MACRO_ANY, NULL, 0);

				trx_vector_ptr_append(&item->preproc_ops, ppdst);
				continue;
			}

			ppdst = (trx_lld_item_preproc_t *)item->preproc_ops.values[j];

			if (j >= item_proto->preproc_ops.values_num)
			{
				ppdst->flags &= ~TRX_FLAG_LLD_ITEM_PREPROC_DISCOVERED;
				continue;
			}

			ppsrc = (trx_lld_item_preproc_t *)item_proto->preproc_ops.values[j];

			ppdst->flags |= TRX_FLAG_LLD_ITEM_PREPROC_DISCOVERED;

			if (ppdst->type != ppsrc->type)
			{
				ppdst->type = ppsrc->type;
				ppdst->flags |= TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_TYPE;
			}

			if (ppdst->step != ppsrc->step)
			{
				/* this should never happen */
				ppdst->step = ppsrc->step;
				ppdst->flags |= TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_STEP;
			}

			buffer = trx_strdup(buffer, ppsrc->params);
			substitute_lld_macros_in_preproc_params(ppsrc->type, item->lld_row, lld_macro_paths, &buffer);

			if (0 != strcmp(ppdst->params, buffer))
			{
				trx_free(ppdst->params);
				ppdst->params = buffer;
				buffer = NULL;
				ppdst->flags |= TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_PARAMS;
			}

			if (ppdst->error_handler != ppsrc->error_handler)
			{
				ppdst->error_handler = ppsrc->error_handler;
				ppdst->flags |= TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER;
			}

			buffer = trx_strdup(buffer, ppsrc->error_handler_params);
			substitute_lld_macros(&buffer, &item->lld_row->jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);

			if (0 != strcmp(ppdst->error_handler_params, buffer))
			{
				trx_free(ppdst->error_handler_params);
				ppdst->error_handler_params = buffer;
				buffer = NULL;
				ppdst->flags |= TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER_PARAMS;
			}
			else
				trx_free(buffer);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_save                                                    *
 *                                                                            *
 * Purpose: recursively prepare LLD item bulk insert if any and               *
 *          update dependent items with their masters                         *
 *                                                                            *
 * Parameters: hostid               - [IN] parent host id                     *
 *             item_prototypes      - [IN] item prototypes                    *
 *             item                 - [IN/OUT] item to be saved and set       *
 *                                             master for dependentent items  *
 *             itemid               - [IN/OUT] item id used for insert        *
 *                                             operations                     *
 *             itemdiscoveryid      - [IN/OUT] item discovery id used for     *
 *                                             insert operations              *
 *             db_insert_items      - [IN] prepared item bulk insert          *
 *             db_insert_idiscovery - [IN] prepared item discovery bulk       *
 *                                         insert                             *
 *             db_insert_irtdata    - [IN] prepared item real-time data bulk  *
 *                                         insert                             *
 *                                                                            *
 ******************************************************************************/
static void	lld_item_save(trx_uint64_t hostid, const trx_vector_ptr_t *item_prototypes, trx_lld_item_t *item,
		trx_uint64_t *itemid, trx_uint64_t *itemdiscoveryid, trx_db_insert_t *db_insert_items,
		trx_db_insert_t *db_insert_idiscovery, trx_db_insert_t *db_insert_irtdata)
{
	int	index;

	if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
		return;

	if (FAIL == (index = trx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
			TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return;
	}

	if (0 == item->itemid)
	{
		const trx_lld_item_prototype_t	*item_prototype;

		item_prototype = (trx_lld_item_prototype_t *)item_prototypes->values[index];
		item->itemid = (*itemid)++;

		trx_db_insert_add_values(db_insert_items, item->itemid, item->name, item->key, hostid,
				(int)item_prototype->type, (int)item_prototype->value_type,
				item->delay, item->history, item->trends,
				(int)item_prototype->status, item_prototype->trapper_hosts, item->units,
				item_prototype->formula, item_prototype->logtimefmt, item_prototype->valuemapid,
				item->params, item->ipmi_sensor, item_prototype->snmp_community, item->snmp_oid,
				item_prototype->port, item_prototype->snmpv3_securityname,
				(int)item_prototype->snmpv3_securitylevel,
				(int)item_prototype->snmpv3_authprotocol, item_prototype->snmpv3_authpassphrase,
				(int)item_prototype->snmpv3_privprotocol, item_prototype->snmpv3_privpassphrase,
				(int)item_prototype->authtype, item->username,
				item->password, item_prototype->publickey, item_prototype->privatekey,
				item->description, item_prototype->interfaceid, (int)TRX_FLAG_DISCOVERY_CREATED,
				item_prototype->snmpv3_contextname, item->jmx_endpoint, item->master_itemid,
				item->timeout, item->url, item->query_fields, item->posts, item->status_codes,
				item_prototype->follow_redirects, item_prototype->post_type, item->http_proxy,
				item->headers, item_prototype->retrieve_mode, item_prototype->request_method,
				item_prototype->output_format, item->ssl_cert_file, item->ssl_key_file,
				item->ssl_key_password, item_prototype->verify_peer, item_prototype->verify_host,
				item_prototype->allow_traps);

		trx_db_insert_add_values(db_insert_idiscovery, (*itemdiscoveryid)++, item->itemid,
				item->parent_itemid, item_prototype->key);

		trx_db_insert_add_values(db_insert_irtdata, item->itemid);
	}

	for (index = 0; index < item->dependent_items.values_num; index++)
	{
		trx_lld_item_t	*dependent;

		dependent = (trx_lld_item_t *)item->dependent_items.values[index];
		dependent->master_itemid = item->itemid;
		lld_item_save(hostid, item_prototypes, dependent, itemid, itemdiscoveryid, db_insert_items,
				db_insert_idiscovery, db_insert_irtdata);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_prepare_update                                          *
 *                                                                            *
 * Purpose: prepare sql to update LLD item                                    *
 *                                                                            *
 * Parameters: item_prototype       - [IN] item prototype                     *
 *             item                 - [IN] item to be updated                 *
 *             sql                  - [IN/OUT] sql buffer pointer used for    *
 *                                             update operations              *
 *             sql_alloc            - [IN/OUT] sql buffer already allocated   *
 *                                             memory                         *
 *             sql_offset           - [IN/OUT] offset for writing within sql  *
 *                                             buffer                         *
 *                                                                            *
 ******************************************************************************/
static void	lld_item_prepare_update(const trx_lld_item_prototype_t *item_prototype, const trx_lld_item_t *item,
		char **sql, size_t *sql_alloc, size_t *sql_offset)
{
	char				*value_esc;
	const char			*d = "";

	trx_strcpy_alloc(sql, sql_alloc, sql_offset, "update items set ");
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_NAME))
	{
		value_esc = DBdyn_escape_string(item->name);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "name='%s'", value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_KEY))
	{
		value_esc = DBdyn_escape_string(item->key);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%skey_='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_TYPE))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%stype=%d", d, (int)item_prototype->type);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_VALUE_TYPE))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%svalue_type=%d", d, (int)item_prototype->value_type);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_DELAY))
	{
		value_esc = DBdyn_escape_string(item->delay);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sdelay='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_HISTORY))
	{
		value_esc = DBdyn_escape_string(item->history);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%shistory='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_TRENDS))
	{
		value_esc = DBdyn_escape_string(item->trends);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%strends='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_TRAPPER_HOSTS))
	{
		value_esc = DBdyn_escape_string(item_prototype->trapper_hosts);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%strapper_hosts='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_UNITS))
	{
		value_esc = DBdyn_escape_string(item->units);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sunits='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_FORMULA))
	{
		value_esc = DBdyn_escape_string(item_prototype->formula);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sformula='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_LOGTIMEFMT))
	{
		value_esc = DBdyn_escape_string(item_prototype->logtimefmt);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%slogtimefmt='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_VALUEMAPID))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%svaluemapid=%s",
				d, DBsql_id_ins(item_prototype->valuemapid));
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_PARAMS))
	{
		value_esc = DBdyn_escape_string(item->params);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sparams='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_IPMI_SENSOR))
	{
		value_esc = DBdyn_escape_string(item->ipmi_sensor);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sipmi_sensor='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_SNMP_COMMUNITY))
	{
		value_esc = DBdyn_escape_string(item_prototype->snmp_community);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%ssnmp_community='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_SNMP_OID))
	{
		value_esc = DBdyn_escape_string(item->snmp_oid);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%ssnmp_oid='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_PORT))
	{
		value_esc = DBdyn_escape_string(item_prototype->port);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sport='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_SECURITYNAME))
	{
		value_esc = DBdyn_escape_string(item_prototype->snmpv3_securityname);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%ssnmpv3_securityname='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_SECURITYLEVEL))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%ssnmpv3_securitylevel=%d", d,
				(int)item_prototype->snmpv3_securitylevel);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_AUTHPROTOCOL))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%ssnmpv3_authprotocol=%d", d,
				(int)item_prototype->snmpv3_authprotocol);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_AUTHPASSPHRASE))
	{
		value_esc = DBdyn_escape_string(item_prototype->snmpv3_authpassphrase);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%ssnmpv3_authpassphrase='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_PRIVPROTOCOL))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%ssnmpv3_privprotocol=%d", d,
				(int)item_prototype->snmpv3_privprotocol);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_PRIVPASSPHRASE))
	{
		value_esc = DBdyn_escape_string(item_prototype->snmpv3_privpassphrase);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%ssnmpv3_privpassphrase='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_AUTHTYPE))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sauthtype=%d", d, (int)item_prototype->authtype);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_USERNAME))
	{
		value_esc = DBdyn_escape_string(item->username);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%susername='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_PASSWORD))
	{
		value_esc = DBdyn_escape_string(item->password);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%spassword='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_PUBLICKEY))
	{
		value_esc = DBdyn_escape_string(item_prototype->publickey);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%spublickey='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_PRIVATEKEY))
	{
		value_esc = DBdyn_escape_string(item_prototype->privatekey);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sprivatekey='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_DESCRIPTION))
	{
		value_esc = DBdyn_escape_string(item->description);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sdescription='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";

	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_INTERFACEID))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sinterfaceid=%s",
				d, DBsql_id_ins(item_prototype->interfaceid));
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_SNMPV3_CONTEXTNAME))
	{
		value_esc = DBdyn_escape_string(item_prototype->snmpv3_contextname);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%ssnmpv3_contextname='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_JMX_ENDPOINT))
	{
		value_esc = DBdyn_escape_string(item->jmx_endpoint);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sjmx_endpoint='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_MASTER_ITEM))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%smaster_itemid=%s",
				d, DBsql_id_ins(item->master_itemid));
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_TIMEOUT))
	{
		value_esc = DBdyn_escape_string(item->timeout);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%stimeout='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_URL))
	{
		value_esc = DBdyn_escape_string(item->url);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%surl='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_QUERY_FIELDS))
	{
		value_esc = DBdyn_escape_string(item->query_fields);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%squery_fields='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_POSTS))
	{
		value_esc = DBdyn_escape_string(item->posts);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sposts='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_STATUS_CODES))
	{
		value_esc = DBdyn_escape_string(item->status_codes);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sstatus_codes='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_FOLLOW_REDIRECTS))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sfollow_redirects=%d", d,
				(int)item_prototype->follow_redirects);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_POST_TYPE))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%spost_type=%d", d, (int)item_prototype->post_type);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_HTTP_PROXY))
	{
		value_esc = DBdyn_escape_string(item->http_proxy);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%shttp_proxy='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_HEADERS))
	{
		value_esc = DBdyn_escape_string(item->headers);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sheaders='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_RETRIEVE_MODE))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sretrieve_mode=%d", d,
				(int)item_prototype->retrieve_mode);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_REQUEST_METHOD))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%srequest_method=%d", d,
				(int)item_prototype->request_method);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_OUTPUT_FORMAT))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%soutput_format=%d", d,
				(int)item_prototype->output_format);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_SSL_CERT_FILE))
	{
		value_esc = DBdyn_escape_string(item->ssl_cert_file);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sssl_cert_file='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_FILE))
	{
		value_esc = DBdyn_escape_string(item->ssl_key_file);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sssl_key_file='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_PASSWORD))
	{
		value_esc = DBdyn_escape_string(item->ssl_key_password);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sssl_key_password='%s'", d, value_esc);
		trx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_VERIFY_PEER))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sverify_peer=%d", d, (int)item_prototype->verify_peer);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_VERIFY_HOST))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sverify_host=%d", d, (int)item_prototype->verify_host);
		d = ",";
	}
	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_ALLOW_TRAPS))
	{
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sallow_traps=%d", d, (int)item_prototype->allow_traps);
	}

	trx_snprintf_alloc(sql, sql_alloc, sql_offset, " where itemid=" TRX_FS_UI64 ";\n", item->itemid);

	DBexecute_overflowed_sql(sql, sql_alloc, sql_offset);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_discovery_prepare_update                                *
 *                                                                            *
 * Purpose: prepare sql to update key in LLD item discovery                   *
 *                                                                            *
 * Parameters: item_prototype       - [IN] item prototype                     *
 *             item                 - [IN] item to be updated                 *
 *             sql                  - [IN/OUT] sql buffer pointer used for    *
 *                                             update operations              *
 *             sql_alloc            - [IN/OUT] sql buffer already allocated   *
 *                                             memory                         *
 *             sql_offset           - [IN/OUT] offset for writing within sql  *
 *                                             buffer                         *
 *                                                                            *
 ******************************************************************************/
static void lld_item_discovery_prepare_update(const trx_lld_item_prototype_t *item_prototype,
		const trx_lld_item_t *item, char **sql, size_t *sql_alloc, size_t *sql_offset)
{
	char	*value_esc;

	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_KEY))
	{
		value_esc = DBdyn_escape_string(item_prototype->key);
		trx_snprintf_alloc(sql, sql_alloc, sql_offset,
				"update item_discovery"
				" set key_='%s'"
				" where itemid=" TRX_FS_UI64 ";\n",
				value_esc, item->itemid);
		trx_free(value_esc);

		DBexecute_overflowed_sql(sql, sql_alloc, sql_offset);
	}
}


/******************************************************************************
 *                                                                            *
 * Function: lld_items_save                                                   *
 *                                                                            *
 * Parameters: hostid          - [IN] parent host id                          *
 *             item_prototypes - [IN] item prototypes                         *
 *             items           - [IN/OUT] items to save                       *
 *             items_index     - [IN] LLD item index                          *
 *             host_locked     - [IN/OUT] host record is locked               *
 *                                                                            *
 * Return value: SUCCEED - if items were successfully saved or saving was not *
 *                         necessary                                          *
 *               FAIL    - items cannot be saved                              *
 *                                                                            *
 ******************************************************************************/
static int	lld_items_save(trx_uint64_t hostid, const trx_vector_ptr_t *item_prototypes, trx_vector_ptr_t *items,
		trx_hashset_t *items_index, int *host_locked)
{
	int			ret = SUCCEED, i, new_items = 0, upd_items = 0;
	trx_lld_item_t		*item;
	trx_uint64_t		itemid, itemdiscoveryid;
	trx_db_insert_t		db_insert_items, db_insert_idiscovery, db_insert_irtdata;
	trx_lld_item_index_t	item_index_local;
	trx_vector_uint64_t	upd_keys;
	char			*sql = NULL;
	size_t			sql_alloc = 8 * TRX_KIBIBYTE, sql_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&upd_keys);

	if (0 == items->values_num)
		goto out;

	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_lld_item_t *)items->values[i];

		if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		if (0 == item->itemid)
		{
			new_items++;
		}
		else if (0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE))
		{
			upd_items++;
			if(0 != (item->flags & TRX_FLAG_LLD_ITEM_UPDATE_KEY))
				trx_vector_uint64_append(&upd_keys, item->itemid);
		}
	}

	if (0 == new_items && 0 == upd_items)
		goto out;

	if (0 == *host_locked)
	{
		if (SUCCEED != DBlock_hostid(hostid))
		{
			/* the host was removed while processing lld rule */
			ret = FAIL;
			goto out;
		}

		*host_locked = 1;
	}

	if (0 != upd_items)
		sql = (char*)trx_malloc(NULL, sql_alloc);

	if (0 != upd_keys.values_num)
	{
		sql_offset = 0;

		trx_vector_uint64_sort(&upd_keys, TRX_DEFAULT_UINT64_COMPARE_FUNC);

#ifdef HAVE_MYSQL
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update items set key_=concat('#',key_) where");
#else
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update items set key_='#'||key_ where");
#endif
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", upd_keys.values,
				upd_keys.values_num);

		if (TRX_DB_OK > DBexecute("%s", sql))
		{
			ret = FAIL;
			goto out;
		}

	}

	if (0 != new_items)
	{
		itemid = DBget_maxid_num("items", new_items);
		itemdiscoveryid = DBget_maxid_num("item_discovery", new_items);

		trx_db_insert_prepare(&db_insert_items, "items", "itemid", "name", "key_", "hostid", "type",
				"value_type", "delay", "history", "trends", "status", "trapper_hosts",
				"units", "formula", "logtimefmt", "valuemapid", "params",
				"ipmi_sensor", "snmp_community", "snmp_oid", "port", "snmpv3_securityname",
				"snmpv3_securitylevel", "snmpv3_authprotocol", "snmpv3_authpassphrase",
				"snmpv3_privprotocol", "snmpv3_privpassphrase", "authtype", "username", "password",
				"publickey", "privatekey", "description", "interfaceid", "flags", "snmpv3_contextname",
				"jmx_endpoint", "master_itemid", "timeout", "url", "query_fields", "posts",
				"status_codes", "follow_redirects", "post_type", "http_proxy", "headers",
				"retrieve_mode", "request_method", "output_format", "ssl_cert_file", "ssl_key_file",
				"ssl_key_password", "verify_peer", "verify_host", "allow_traps", NULL);

		trx_db_insert_prepare(&db_insert_idiscovery, "item_discovery", "itemdiscoveryid", "itemid",
				"parent_itemid", "key_", NULL);

		trx_db_insert_prepare(&db_insert_irtdata, "item_rtdata", "itemid", NULL);
	}

	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_lld_item_t *)items->values[i];

		/* dependent items based on item prototypes are saved within recursive lld_item_save calls while */
		/* saving master item */
		if (0 == item->master_itemid)
		{
			lld_item_save(hostid, item_prototypes, item, &itemid, &itemdiscoveryid, &db_insert_items,
					&db_insert_idiscovery, &db_insert_irtdata);
		}
		else
		{
			item_index_local.parent_itemid = item->master_itemid;
			item_index_local.lld_row = (trx_lld_row_t *)item->lld_row;

			/* dependent item based on host item should be saved */
			if (NULL == trx_hashset_search(items_index, &item_index_local))
			{
				lld_item_save(hostid, item_prototypes, item, &itemid, &itemdiscoveryid,
						&db_insert_items, &db_insert_idiscovery, &db_insert_irtdata);
			}
		}

	}

	if (0 != new_items)
	{
		trx_db_insert_execute(&db_insert_items);
		trx_db_insert_clean(&db_insert_items);

		trx_db_insert_execute(&db_insert_idiscovery);
		trx_db_insert_clean(&db_insert_idiscovery);

		trx_db_insert_execute(&db_insert_irtdata);
		trx_db_insert_clean(&db_insert_irtdata);

		trx_vector_ptr_sort(items, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	if (0 != upd_items)
	{
		trx_lld_item_prototype_t	*item_prototype;
		int				index;

		sql_offset = 0;

		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		for (i = 0; i < items->values_num; i++)
		{
			item = (trx_lld_item_t *)items->values[i];

			if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED) ||
					0 == (item->flags & TRX_FLAG_LLD_ITEM_UPDATE))
			{
				continue;
			}

			if (FAIL == (index = trx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			item_prototype = item_prototypes->values[index];

			lld_item_prepare_update(item_prototype, item, &sql, &sql_alloc, &sql_offset);
			lld_item_discovery_prepare_update(item_prototype, item, &sql, &sql_alloc, &sql_offset);
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);
		if (sql_offset > 16)
			DBexecute("%s", sql);
	}
out:
	trx_free(sql);
	trx_vector_uint64_destroy(&upd_keys);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_preproc_save                                           *
 *                                                                            *
 * Purpose: saves/updates/removes item preprocessing operations               *
 *                                                                            *
 * Parameters: hostid      - [IN] parent host id                              *
 *             items       - [IN] items                                       *
 *             host_locked - [IN/OUT] host record is locked                   *
 *                                                                            *
 ******************************************************************************/
static int	lld_items_preproc_save(trx_uint64_t hostid, trx_vector_ptr_t *items, int *host_locked)
{
	int			ret = SUCCEED, i, j, new_preproc_num = 0, update_preproc_num = 0, delete_preproc_num = 0;
	trx_lld_item_t		*item;
	trx_lld_item_preproc_t	*preproc_op;
	trx_vector_uint64_t	deleteids;
	trx_db_insert_t		db_insert;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&deleteids);

	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_lld_item_t *)items->values[i];

		if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		for (j = 0; j < item->preproc_ops.values_num; j++)
		{
			preproc_op = (trx_lld_item_preproc_t *)item->preproc_ops.values[j];
			if (0 == (preproc_op->flags & TRX_FLAG_LLD_ITEM_PREPROC_DISCOVERED))
			{
				trx_vector_uint64_append(&deleteids, preproc_op->item_preprocid);
				continue;
			}

			if (0 == preproc_op->item_preprocid)
			{
				new_preproc_num++;
				continue;
			}

			if (0 == (preproc_op->flags & TRX_FLAG_LLD_ITEM_PREPROC_UPDATE))
				continue;

			update_preproc_num++;
		}
	}

	if (0 == *host_locked && (0 != update_preproc_num || 0 != new_preproc_num || 0 != deleteids.values_num))
	{
		if (SUCCEED != DBlock_hostid(hostid))
		{
			/* the host was removed while processing lld rule */
			ret = FAIL;
			goto out;
		}

		*host_locked = 1;
	}

	if (0 != update_preproc_num)
	{
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	if (0 != new_preproc_num)
	{
		trx_db_insert_prepare(&db_insert, "item_preproc", "item_preprocid", "itemid", "step", "type", "params",
				"error_handler", "error_handler_params", NULL);
	}

	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_lld_item_t *)items->values[i];

		if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		for (j = 0; j < item->preproc_ops.values_num; j++)
		{
			char	delim = ' ';

			preproc_op = (trx_lld_item_preproc_t *)item->preproc_ops.values[j];

			if (0 == preproc_op->item_preprocid)
			{
				trx_db_insert_add_values(&db_insert, __UINT64_C(0), item->itemid, preproc_op->step,
						preproc_op->type, preproc_op->params, preproc_op->error_handler,
						preproc_op->error_handler_params);
				continue;
			}

			if (0 == (preproc_op->flags & TRX_FLAG_LLD_ITEM_PREPROC_UPDATE))
				continue;

			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update item_preproc set");

			if (0 != (preproc_op->flags & TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_TYPE))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%ctype=%d", delim, preproc_op->type);
				delim = ',';
			}

			if (0 != (preproc_op->flags & TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_STEP))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cstep=%d", delim, preproc_op->step);
				delim = ',';
			}

			if (0 != (preproc_op->flags & TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_PARAMS))
			{
				char	*params_esc;

				params_esc = DBdyn_escape_string(preproc_op->params);
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cparams='%s'", delim, params_esc);

				trx_free(params_esc);
				delim = ',';
			}

			if (0 != (preproc_op->flags & TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cerror_handler=%d", delim,
						preproc_op->error_handler);
				delim = ',';
			}

			if (0 != (preproc_op->flags & TRX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER_PARAMS))
			{
				char	*params_esc;

				params_esc = DBdyn_escape_string(preproc_op->error_handler_params);
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cerror_handler_params='%s'", delim,
						params_esc);

				trx_free(params_esc);
			}

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where item_preprocid=" TRX_FS_UI64 ";\n",
					preproc_op->item_preprocid);

			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
		}
	}

	if (0 != update_preproc_num)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)	/* in ORACLE always present begin..end; */
			DBexecute("%s", sql);
	}

	if (0 != new_preproc_num)
	{
		trx_db_insert_autoincrement(&db_insert, "item_preprocid");
		trx_db_insert_execute(&db_insert);
		trx_db_insert_clean(&db_insert);
	}

	if (0 != deleteids.values_num)
	{
		sql_offset = 0;
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from item_preproc where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "item_preprocid", deleteids.values,
				deleteids.values_num);
		DBexecute("%s", sql);

		delete_preproc_num = deleteids.values_num;
	}
out:
	trx_free(sql);
	trx_vector_uint64_destroy(&deleteids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() added:%d updated:%d removed:%d", __func__, new_preproc_num,
			update_preproc_num, delete_preproc_num);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_applications_save                                            *
 *                                                                            *
 * Parameters: hostid                 - [IN] host id                          *
 *             applications           - [IN/OUT] applications to save         *
 *             application_prototypes - [IN] the application prototypes       *
 *             host_locked            - [IN/OUT] host record is locked        *
 *                                                                            *
 ******************************************************************************/
static int	lld_applications_save(trx_uint64_t hostid, trx_vector_ptr_t *applications,
		const trx_vector_ptr_t *application_prototypes, int *host_locked)
{
	int					ret = SUCCEED, i, new_applications = 0, new_discoveries = 0, index;
	trx_lld_application_t			*application;
	const trx_lld_application_prototype_t	*application_prototype;
	trx_uint64_t				applicationid, application_discoveryid;
	trx_db_insert_t				db_insert, db_insert_discovery;
	trx_vector_uint64_t			del_applicationids, del_discoveryids;
	char					*sql_a = NULL, *sql_ad = NULL, *name;
	size_t					sql_a_alloc = 0, sql_a_offset = 0, sql_ad_alloc = 0, sql_ad_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == applications->values_num)
		goto out;

	if (0 == *host_locked)
	{
		if (SUCCEED != DBlock_hostid(hostid))
		{
			/* the host was removed while processing lld rule */
			ret = FAIL;
			goto out;
		}

		*host_locked = 1;
	}

	trx_vector_uint64_create(&del_applicationids);
	trx_vector_uint64_create(&del_discoveryids);

	/* Count new applications and application discoveries.                      */
	/* Note that an application might have been discovered by another lld rule. */
	/* In this case the discovered items will be linked to this application and */
	/* new application discovery record, linking the prototype to this          */
	/* application, will be created.                                            */
	for (i = 0; i < applications->values_num; i++)
	{
		application = (trx_lld_application_t *)applications->values[i];

		if (0 != (application->flags & TRX_FLAG_LLD_APPLICATION_REMOVE))
		{
			trx_vector_uint64_append(&del_applicationids, application->applicationid);
			continue;
		}

		if (0 == (application->flags & TRX_FLAG_LLD_APPLICATION_DISCOVERED))
			continue;

		if (0 == application->applicationid)
			new_applications++;

		if (0 != (application->flags & TRX_FLAG_LLD_APPLICATION_ADD_DISCOVERY))
			new_discoveries++;
	}

	/* insert new applications, application discoveries and prepare a list of applications to be removed */

	if (0 != new_applications)
	{
		applicationid = DBget_maxid_num("applications", new_applications);
		trx_db_insert_prepare(&db_insert, "applications", "applicationid", "hostid", "name", "flags", NULL);
	}

	if (0 != new_discoveries)
	{
		application_discoveryid = DBget_maxid_num("application_discovery", new_discoveries);
		trx_db_insert_prepare(&db_insert_discovery, "application_discovery", "application_discoveryid",
				"applicationid", "application_prototypeid", "name", NULL);
	}

	for (i = 0; i < applications->values_num; i++)
	{
		application = (trx_lld_application_t *)applications->values[i];

		if (0 != (application->flags & TRX_FLAG_LLD_APPLICATION_REMOVE_DISCOVERY))
		{
			trx_vector_uint64_append(&del_discoveryids, application->application_discoveryid);
			continue;
		}

		if (0 == (application->flags & TRX_FLAG_LLD_APPLICATION_DISCOVERED))
			continue;

		if (FAIL == (index = trx_vector_ptr_search(application_prototypes,
				&application->application_prototypeid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		application_prototype = (trx_lld_application_prototype_t *)application_prototypes->values[index];

		if (0 == application->applicationid)
		{
			application->applicationid = applicationid++;
			trx_db_insert_add_values(&db_insert, application->applicationid, hostid, application->name,
					TRX_FLAG_DISCOVERY_CREATED);
		}

		if (0 != (application->flags & TRX_FLAG_LLD_APPLICATION_UPDATE_NAME))
		{
			if (NULL == sql_a)
				DBbegin_multiple_update(&sql_a, &sql_a_alloc, &sql_a_offset);
			if (NULL == sql_ad)
				DBbegin_multiple_update(&sql_ad, &sql_ad_alloc, &sql_ad_offset);

			name = DBdyn_escape_string(application->name);
			trx_snprintf_alloc(&sql_a, &sql_a_alloc, &sql_a_offset,
					"update applications set name='%s'"
					" where applicationid=" TRX_FS_UI64 ";\n",
					name, application->applicationid);
			trx_free(name);

			name = DBdyn_escape_string(application_prototype->name);
			trx_snprintf_alloc(&sql_ad, &sql_ad_alloc, &sql_ad_offset,
					"update application_discovery set name='%s'"
					" where application_discoveryid=" TRX_FS_UI64 ";\n",
					name, application->application_discoveryid);
			trx_free(name);

			DBexecute_overflowed_sql(&sql_a, &sql_a_alloc, &sql_a_offset);
			DBexecute_overflowed_sql(&sql_ad, &sql_ad_alloc, &sql_ad_offset);

			continue;
		}

		if (0 == (application->flags & TRX_FLAG_LLD_APPLICATION_ADD_DISCOVERY))
			continue;

		application->application_discoveryid = application_discoveryid++;
		trx_db_insert_add_values(&db_insert_discovery, application->application_discoveryid,
				application->applicationid, application->application_prototypeid,
				application_prototype->name);
	}

	if (NULL != sql_a)
	{
		DBend_multiple_update(&sql_a, &sql_a_alloc, &sql_a_offset);

		if (16 < sql_a_offset)	/* in ORACLE always present begin..end; */
			DBexecute("%s", sql_a);
	}

	if (NULL != sql_ad)
	{
		DBend_multiple_update(&sql_ad, &sql_ad_alloc, &sql_ad_offset);

		if (16 < sql_ad_offset)
			DBexecute("%s", sql_ad);
	}

	if (0 != del_applicationids.values_num)
	{
		sql_a_offset = 0;

		trx_strcpy_alloc(&sql_a, &sql_a_alloc, &sql_a_offset, "delete from applications where");
		DBadd_condition_alloc(&sql_a, &sql_a_alloc, &sql_a_offset, "applicationid", del_applicationids.values,
				del_applicationids.values_num);
		trx_strcpy_alloc(&sql_a, &sql_a_alloc, &sql_a_offset, ";\n");

		DBexecute("%s", sql_a);
	}

	if (0 != del_discoveryids.values_num)
	{
		sql_ad_offset = 0;

		trx_strcpy_alloc(&sql_a, &sql_a_alloc, &sql_a_offset, "delete from application_discovery where");
		DBadd_condition_alloc(&sql_a, &sql_a_alloc, &sql_a_offset, "application_discoveryid",
				del_discoveryids.values, del_discoveryids.values_num);
		trx_strcpy_alloc(&sql_a, &sql_a_alloc, &sql_a_offset, ";\n");

		DBexecute("%s", sql_ad);
	}

	trx_free(sql_a);
	trx_free(sql_ad);

	if (0 != new_applications)
	{
		trx_db_insert_execute(&db_insert);
		trx_db_insert_clean(&db_insert);

		trx_vector_ptr_sort(applications, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	if (0 != new_discoveries)
	{
		trx_db_insert_execute(&db_insert_discovery);
		trx_db_insert_clean(&db_insert_discovery);
	}

	trx_vector_uint64_destroy(&del_discoveryids);
	trx_vector_uint64_destroy(&del_applicationids);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_application_validate                                    *
 *                                                                            *
 * Purpose: validates undiscovered item-application link to determine if it   *
 *          should be removed                                                 *
 *                                                                            *
 * Parameters: items_application - [IN] an item-application link to validate  *
 *             items             - [IN] the related items                     *
 *             applications      - [IN] the related applications              *
 *                                                                            *
 * Return value: SUCCEED - item-application link should not be removed        *
 *               FAIL    - item-application link should be removed            *
 *                                                                            *
 * Comments: Undiscovered item-application link must be removed if either the *
 *           application was not discovered or item was discovered.           *
 *           The only case when undiscovered item-application link is not     *
 *           removed is when we have valid application and undiscovered item. *
 *           In this case we leave item-application link untouched and it     *
 *           will 'expire' together with item.                                *
 *                                                                            *
 ******************************************************************************/
static int	lld_item_application_validate(const trx_lld_item_application_t *item_application,
		const trx_vector_ptr_t *items, const trx_vector_ptr_t *applications)
{
	const trx_lld_application_t	*application;
	const trx_lld_item_t		*item;
	int				index;

	if (FAIL == (index = trx_vector_ptr_bsearch(applications, &item_application->application_ref.applicationid,
			TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
	{
		/* Applications vector contains only discovered applications and  */
		/* apparently the item was linked to a normal application.        */
		/* Undiscovered item-application links to normal application must */
		/* be removed if item has been also discovered - this means that  */
		/* the item prototype - application link was removed by frontend. */
		goto check_item;
	}

	application = (trx_lld_application_t *)applications->values[index];

	if (0 == (application->flags & TRX_FLAG_LLD_APPLICATION_DISCOVERED))
		return FAIL;

check_item:
	if (FAIL == (index = trx_vector_ptr_bsearch(items, &item_application->item_ref.itemid,
			TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

	item = (trx_lld_item_t *)items->values[index];

	if (0 != (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_applications_save                                      *
 *                                                                            *
 * Parameters: items_applications - [IN] item-application links               *
 *                                                                            *
 ******************************************************************************/
static void	lld_items_applications_save(trx_hashset_t *items_applications, const trx_vector_ptr_t *items,
		const trx_vector_ptr_t *applications)
{
	trx_hashset_iter_t		iter;
	trx_lld_item_application_t	*item_application;
	trx_vector_uint64_t		del_itemappids;
	int				new_item_applications = 0;
	trx_uint64_t			itemappid, applicationid, itemid;
	trx_db_insert_t			db_insert;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == items_applications->num_data)
		goto out;

	trx_vector_uint64_create(&del_itemappids);

	/* count new item-application links */
	trx_hashset_iter_reset(items_applications, &iter);

	while (NULL != (item_application = (trx_lld_item_application_t *)trx_hashset_iter_next(&iter)))
	{
		if (0 == item_application->itemappid)
			new_item_applications++;
	}

	if (0 != new_item_applications)
	{
		itemappid = DBget_maxid_num("items_applications", new_item_applications);
		trx_db_insert_prepare(&db_insert, "items_applications", "itemappid", "applicationid", "itemid", NULL);
	}

	trx_hashset_iter_reset(items_applications, &iter);

	while (NULL != (item_application = (trx_lld_item_application_t *)trx_hashset_iter_next(&iter)))
	{
		if (0 != item_application->itemappid)
		{
			/* add for removal the old links that aren't discovered and can be removed */
			if (0 == (item_application->flags & TRX_FLAG_LLD_ITEM_APPLICATION_DISCOVERED) &&
					FAIL == lld_item_application_validate(item_application, items, applications))
			{
				trx_vector_uint64_append(&del_itemappids, item_application->itemappid);
			}

			continue;
		}

		if (0 == (applicationid = item_application->application_ref.applicationid))
			applicationid = item_application->application_ref.application->applicationid;

		if (0 == (itemid = item_application->item_ref.itemid))
			itemid = item_application->item_ref.item->itemid;

		item_application->itemappid = itemappid++;
		trx_db_insert_add_values(&db_insert, item_application->itemappid, applicationid, itemid);
	}

	if (0 != new_item_applications)
	{
		trx_db_insert_execute(&db_insert);
		trx_db_insert_clean(&db_insert);
	}

	/* remove deprecated links */
	if (0 != del_itemappids.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from items_applications where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemappid", del_itemappids.values,
				del_itemappids.values_num);

		DBexecute("%s", sql);

		trx_free(sql);
	}

	trx_vector_uint64_destroy(&del_itemappids);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_remove_lost_items                                            *
 *                                                                            *
 * Purpose: updates item_discovery.lastcheck and item_discovery.ts_delete     *
 *          fields; removes lost resources                                    *
 *                                                                            *
 ******************************************************************************/
static void	lld_remove_lost_items(const trx_vector_ptr_t *items, int lifetime, int lastcheck)
{
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;
	trx_lld_item_t			*item;
	trx_vector_uint64_t		del_itemids, lc_itemids, ts_itemids;
	trx_vector_uint64_pair_t	discovery_itemts;
	int				i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == items->values_num)
		goto out;

	trx_vector_uint64_create(&del_itemids);
	trx_vector_uint64_create(&lc_itemids);
	trx_vector_uint64_create(&ts_itemids);
	trx_vector_uint64_pair_create(&discovery_itemts);

	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_lld_item_t *)items->values[i];

		if (0 == item->itemid)
			continue;

		if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
		{
			int	ts_delete = lld_end_of_life(item->lastcheck, lifetime);

			if (lastcheck > ts_delete)
			{
				trx_vector_uint64_append(&del_itemids, item->itemid);
			}
			else if (item->ts_delete != ts_delete)
			{
				trx_uint64_pair_t	itemts;

				itemts.first = item->itemid;
				itemts.second = ts_delete;
				trx_vector_uint64_pair_append(&discovery_itemts, itemts);
			}
		}
		else
		{
			trx_vector_uint64_append(&lc_itemids, item->itemid);
			if (0 != item->ts_delete)
				trx_vector_uint64_append(&ts_itemids, item->itemid);
		}
	}

	if (0 == discovery_itemts.values_num && 0 == lc_itemids.values_num && 0 == ts_itemids.values_num &&
			0 == del_itemids.values_num)
	{
		goto clean;
	}

	/* update item discovery table */

	DBbegin();

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	for (i = 0; i < discovery_itemts.values_num; i++)
	{
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"update item_discovery"
				" set ts_delete=%d"
				" where itemid=" TRX_FS_UI64 ";\n",
				(int)discovery_itemts.values[i].second, discovery_itemts.values[i].first);

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	if (0 != lc_itemids.values_num)
	{
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update item_discovery set lastcheck=%d where",
				lastcheck);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid",
				lc_itemids.values, lc_itemids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	if (0 != ts_itemids.values_num)
	{
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update item_discovery set ts_delete=0 where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid",
				ts_itemids.values, ts_itemids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)	/* in ORACLE always present begin..end; */
		DBexecute("%s", sql);

	trx_free(sql);

	/* remove 'lost' items */
	if (0 != del_itemids.values_num)
	{
		trx_vector_uint64_sort(&del_itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
		DBdelete_items(&del_itemids);
	}

	DBcommit();
clean:
	trx_vector_uint64_pair_destroy(&discovery_itemts);
	trx_vector_uint64_destroy(&ts_itemids);
	trx_vector_uint64_destroy(&lc_itemids);
	trx_vector_uint64_destroy(&del_itemids);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_remove_lost_applications                                     *
 *                                                                            *
 * Purpose: updates application_discovery lastcheck and ts_delete fields,     *
 *          removes lost resources                                            *
 *                                                                            *
 ******************************************************************************/
static void	lld_remove_lost_applications(trx_uint64_t lld_ruleid, const trx_vector_ptr_t *applications,
		int lifetime, int lastcheck)
{
	DB_RESULT			result;
	DB_ROW				row;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;
	trx_vector_uint64_t		del_applicationids, del_discoveryids, ts_discoveryids, lc_discoveryids;
	trx_vector_uint64_pair_t	discovery_applicationts;
	int				i, index;
	const trx_lld_application_t	*application;
	trx_uint64_t			applicationid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == applications->values_num)
		goto out;

	trx_vector_uint64_create(&del_applicationids);
	trx_vector_uint64_create(&del_discoveryids);
	trx_vector_uint64_create(&ts_discoveryids);
	trx_vector_uint64_create(&lc_discoveryids);
	trx_vector_uint64_pair_create(&discovery_applicationts);

	/* prepare application discovery update vector */
	for (i = 0; i < applications->values_num; i++)
	{
		application = (const trx_lld_application_t *)applications->values[i];

		if (0 == application->applicationid)
			continue;

		if (0 == (application->flags & TRX_FLAG_LLD_APPLICATION_DISCOVERED))
		{
			int	ts_delete = lld_end_of_life(application->lastcheck, lifetime);

			if (lastcheck > ts_delete)
			{
				trx_vector_uint64_append(&del_applicationids, application->applicationid);
				trx_vector_uint64_append(&del_discoveryids, application->application_discoveryid);
			}
			else if (application->ts_delete != ts_delete)
			{
				trx_uint64_pair_t	applicationts;

				applicationts.first = application->application_discoveryid;
				applicationts.second = ts_delete;
				trx_vector_uint64_pair_append(&discovery_applicationts, applicationts);
			}
		}
		else
		{
			trx_vector_uint64_append(&lc_discoveryids, application->application_discoveryid);
			if (0 != application->ts_delete)
				trx_vector_uint64_append(&ts_discoveryids, application->application_discoveryid);
		}
	}

	/* check if the applications are really 'lost' (not discovered by other discovery rules) */
	if (0 != del_applicationids.values_num)
	{
		trx_vector_uint64_sort(&del_applicationids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select ad.applicationid from application_discovery ad,application_prototype ap"
				" where ad.application_prototypeid=ap.application_prototypeid"
					" and ap.itemid<>" TRX_FS_UI64
					" and",
				lld_ruleid);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ad.applicationid", del_applicationids.values,
				del_applicationids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by ad.applicationid desc");

		result = DBselect("%s", sql);

		sql_offset = 0;

		while (NULL != (row = DBfetch(result)))
		{
			TRX_STR2UINT64(applicationid, row[0]);

			if (FAIL != (index = trx_vector_uint64_bsearch(&del_applicationids, applicationid,
					TRX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				trx_vector_uint64_remove(&del_applicationids, index);
			}
		}

		DBfree_result(result);
	}

	if (0 == discovery_applicationts.values_num && 0 == del_applicationids.values_num &&
			0 == del_discoveryids.values_num && 0 == ts_discoveryids.values_num &&
			0 == lc_discoveryids.values_num)
	{
		goto clean;
	}

	/* remove lost applications and update application discovery table */

	DBbegin();

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	for (i = 0; i < discovery_applicationts.values_num; i++)
	{
		trx_uint64_pair_t	*applicationts = &(discovery_applicationts.values[i]);

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"update application_discovery"
				" set ts_delete=%d"
				" where application_discoveryid=" TRX_FS_UI64 ";\n",
				(int)applicationts->second, applicationts->first);

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	if (0 != del_discoveryids.values_num)
	{
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from application_discovery where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "application_discoveryid",
				del_discoveryids.values, del_discoveryids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	if (0 != ts_discoveryids.values_num)
	{
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update application_discovery"
				" set ts_delete=0 where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "application_discoveryid",
				ts_discoveryids.values, ts_discoveryids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	if (0 != lc_discoveryids.values_num)
	{
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update application_discovery"
				" set lastcheck=%d where", lastcheck);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "application_discoveryid",
				lc_discoveryids.values, lc_discoveryids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	if (0 != del_applicationids.values_num)
	{
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from applications where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "applicationid", del_applicationids.values,
				del_applicationids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)	/* in ORACLE always present begin..end; */
		DBexecute("%s", sql);

	DBcommit();
clean:
	trx_free(sql);

	trx_vector_uint64_pair_destroy(&discovery_applicationts);
	trx_vector_uint64_destroy(&lc_discoveryids);
	trx_vector_uint64_destroy(&ts_discoveryids);
	trx_vector_uint64_destroy(&del_discoveryids);
	trx_vector_uint64_destroy(&del_applicationids);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	lld_item_links_populate(const trx_vector_ptr_t *item_prototypes, const trx_vector_ptr_t *lld_rows,
		trx_hashset_t *items_index)
{
	int				i, j;
	trx_lld_item_prototype_t	*item_prototype;
	trx_lld_item_index_t		*item_index, item_index_local;
	trx_lld_item_link_t		*item_link;

	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (trx_lld_item_prototype_t *)item_prototypes->values[i];
		item_index_local.parent_itemid = item_prototype->itemid;

		for (j = 0; j < lld_rows->values_num; j++)
		{
			item_index_local.lld_row = (trx_lld_row_t *)lld_rows->values[j];

			if (NULL == (item_index = (trx_lld_item_index_t *)trx_hashset_search(items_index, &item_index_local)))
				continue;

			if (0 == (item_index->item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
				continue;

			item_link = (trx_lld_item_link_t *)trx_malloc(NULL, sizeof(trx_lld_item_link_t));

			item_link->parent_itemid = item_index->item->parent_itemid;
			item_link->itemid = item_index->item->itemid;

			trx_vector_ptr_append(&item_index_local.lld_row->item_links, item_link);
		}
	}
}

void	lld_item_links_sort(trx_vector_ptr_t *lld_rows)
{
	int	i;

	for (i = 0; i < lld_rows->values_num; i++)
	{
		trx_lld_row_t	*lld_row = (trx_lld_row_t *)lld_rows->values[i];

		trx_vector_ptr_sort(&lld_row->item_links, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_application_prototypes_get                                   *
 *                                                                            *
 * Purpose: gets the discovery rule application prototypes from database      *
 *                                                                            *
 * Parameters: lld_ruleid             - [IN] the discovery rule id            *
 *             application_prototypes - [OUT] the applications prototypes     *
 *                                            defined for the discovery rule, *
 *                                            sorted by prototype id          *
 *                                                                            *
 ******************************************************************************/
static void	lld_application_prototypes_get(trx_uint64_t lld_ruleid, trx_vector_ptr_t *application_prototypes)
{
	DB_RESULT			result;
	DB_ROW				row;
	trx_lld_application_prototype_t	*application_prototype;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select application_prototypeid,name"
			" from application_prototype"
			" where itemid=" TRX_FS_UI64,
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		application_prototype = (trx_lld_application_prototype_t *)trx_malloc(NULL,
				sizeof(trx_lld_application_prototype_t));

		TRX_STR2UINT64(application_prototype->application_prototypeid, row[0]);
		application_prototype->itemid = lld_ruleid;
		application_prototype->name = trx_strdup(NULL, row[1]);

		trx_vector_ptr_append(application_prototypes, application_prototype);
	}
	DBfree_result(result);

	trx_vector_ptr_sort(application_prototypes, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d prototypes", __func__, application_prototypes->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_application_prototypes_get                              *
 *                                                                            *
 * Purpose: gets the discovery rule item-application link prototypes from     *
 *          database                                                          *
 *                                                                            *
 * Parameters: item_prototypes        - [IN/OUT] item prototypes              *
 *             application_prototypes - [IN] the application prototypes       *
 *                                           defined for the discovery rule   *
 *                                                                            *
 ******************************************************************************/
static void	lld_item_application_prototypes_get(const trx_vector_ptr_t *item_prototypes,
		const trx_vector_ptr_t *application_prototypes)
{
	DB_RESULT			result;
	DB_ROW				row;
	int				i, index;
	trx_uint64_t			application_prototypeid, itemid;
	trx_vector_uint64_t		item_prototypeids;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;
	trx_lld_item_application_ref_t	*item_application_prototype;
	trx_lld_item_prototype_t	*item_prototype;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&item_prototypeids);

	/* get item prototype links to application prototypes */

	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (trx_lld_item_prototype_t *)item_prototypes->values[i];

		trx_vector_uint64_append(&item_prototypeids, item_prototype->itemid);
	}

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select application_prototypeid,itemid"
			" from item_application_prototype"
			" where");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid",
			item_prototypeids.values, item_prototypeids.values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(application_prototypeid, row[0]);

		if (FAIL == (index = trx_vector_ptr_search(application_prototypes, &application_prototypeid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item_application_prototype = (trx_lld_item_application_ref_t *)trx_malloc(NULL,
				sizeof(trx_lld_item_application_ref_t));

		item_application_prototype->application_prototype = (trx_lld_application_prototype_t *)application_prototypes->values[index];
		item_application_prototype->applicationid = 0;

		TRX_STR2UINT64(itemid, row[1]);
		index = trx_vector_ptr_bsearch(item_prototypes, &itemid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		item_prototype = (trx_lld_item_prototype_t *)item_prototypes->values[index];

		trx_vector_ptr_append(&item_prototype->applications, item_application_prototype);
	}
	DBfree_result(result);

	/* get item prototype links to real applications */

	sql_offset = 0;
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select applicationid,itemid"
			" from items_applications"
			" where");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid",
			item_prototypeids.values, item_prototypeids.values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		item_application_prototype = (trx_lld_item_application_ref_t *)trx_malloc(NULL,
				sizeof(trx_lld_item_application_ref_t));

		item_application_prototype->application_prototype = NULL;
		TRX_STR2UINT64(item_application_prototype->applicationid, row[0]);

		TRX_STR2UINT64(itemid, row[1]);
		index = trx_vector_ptr_bsearch(item_prototypes, &itemid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		item_prototype = (trx_lld_item_prototype_t *)item_prototypes->values[index];

		trx_vector_ptr_append(&item_prototype->applications, item_application_prototype);
	}
	DBfree_result(result);

	trx_free(sql);
	trx_vector_uint64_destroy(&item_prototypeids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_applications_get                                             *
 *                                                                            *
 * Purpose: gets applications previously discovered by the discovery rule     *
 *                                                                            *
 * Parameters: lld_ruleid   - [IN] the discovery rule id                      *
 *             applications - [OUT] the applications                          *
 *                                                                            *
 ******************************************************************************/
static void	lld_applications_get(trx_uint64_t lld_ruleid, trx_vector_ptr_t *applications)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_lld_application_t	*application;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select a.applicationid,a.name,ap.application_prototypeid,ad.lastcheck,ad.ts_delete,ad.name,"
				"ad.application_discoveryid"
			" from applications a,application_discovery ad,application_prototype ap"
			" where ap.itemid=" TRX_FS_UI64
				" and ad.application_prototypeid=ap.application_prototypeid"
				" and a.applicationid=ad.applicationid",
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		application = (trx_lld_application_t *)trx_malloc(NULL, sizeof(trx_lld_application_t));

		TRX_STR2UINT64(application->applicationid, row[0]);
		TRX_STR2UINT64(application->application_prototypeid, row[2]);
		TRX_STR2UINT64(application->application_discoveryid, row[6]);
		application->name = trx_strdup(NULL, row[1]);
		application->lastcheck = atoi(row[3]);
		application->ts_delete = atoi(row[4]);
		application->name_proto = trx_strdup(NULL, row[5]);
		application->name_orig = NULL;
		application->flags = TRX_FLAG_LLD_APPLICATION_UNSET;
		application->lld_row = NULL;

		trx_vector_ptr_append(applications, application);
	}
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d applications", __func__, applications->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_application_make                                             *
 *                                                                            *
 * Purpose: create a new application or mark an existing application as       *
 *          discovered based on prototype and lld row                         *
 *                                                                            *
 * Parameters: application_prototype - [IN] the application prototype         *
 *             lld_row               - [IN] the lld row                       *
 *             lld_macro_paths       - [IN] use json path to extract from     *
 *                                          lld_row                           *
 *             applications          - [IN/OUT] the applications              *
 *             applications_index    - [IN/OUT] the application index by      *
 *                                              prototype id and lld row      *
 *                                                                            *
 ******************************************************************************/
static void	lld_application_make(const trx_lld_application_prototype_t *application_prototype,
		const trx_lld_row_t *lld_row, const trx_vector_ptr_t *lld_macro_paths, trx_vector_ptr_t *applications,
		trx_hashset_t *applications_index)
{
	trx_lld_application_t		*application;
	trx_lld_application_index_t	*application_index, application_index_local;
	struct trx_json_parse		*jp_row = (struct trx_json_parse *)&lld_row->jp_row;
	char				*buffer = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s(), proto %s", __func__, application_prototype->name);

	application_index_local.application_prototypeid = application_prototype->application_prototypeid;
	application_index_local.lld_row = lld_row;

	if (NULL == (application_index = (trx_lld_application_index_t *)trx_hashset_search(applications_index, &application_index_local)))
	{
		application = (trx_lld_application_t *)trx_malloc(NULL, sizeof(trx_lld_application_t));
		application->applicationid = 0;
		application->application_prototypeid = application_prototype->application_prototypeid;
		application->application_discoveryid = 0;
		application->ts_delete = 0;

		application->name = trx_strdup(NULL, application_prototype->name);
		substitute_lld_macros(&application->name, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(application->name, TRX_WHITESPACE);

		application->name_proto = trx_strdup(NULL, application_prototype->name);
		application->name_orig = NULL;
		application->flags = TRX_FLAG_LLD_APPLICATION_ADD_DISCOVERY;
		application->lld_row = lld_row;

		trx_vector_ptr_append(applications, application);

		application_index_local.application = application;
		trx_hashset_insert(applications_index, &application_index_local, sizeof(trx_lld_application_index_t));

		treegix_log(LOG_LEVEL_TRACE, "%s(): created new application, proto %s, name %s", __func__,
				application_prototype->name, application->name);
	}
	else
	{
		application = application_index->application;

		if (0 == (application->flags & TRX_FLAG_LLD_APPLICATION_UPDATE_NAME))
		{
			buffer = trx_strdup(NULL, application_prototype->name);
			substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
			trx_lrtrim(buffer, TRX_WHITESPACE);

			if (0 != strcmp(application->name, buffer))
			{
				application->name_orig = application->name;
				application->name = buffer;
				application->flags |= TRX_FLAG_LLD_APPLICATION_UPDATE_NAME;
				treegix_log(LOG_LEVEL_TRACE, "%s(): updated application name to %s", __func__,
						application->name);
			}
			else
				trx_free(buffer);
		}
	}

	application->flags |= TRX_FLAG_LLD_APPLICATION_DISCOVERED;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_applications_make                                            *
 *                                                                            *
 * Purpose: makes new applications and marks old applications as discovered   *
 *          based on application prototypes and lld rows                      *
 *                                                                            *
 * Parameters: application_prototypes - [IN] the application prototypes       *
 *             lld_rows               - [IN] the lld rows                     *
 *             applications           - [IN/OUT] the applications             *
 *             applications_index     - [OUT] the application index by        *
 *                                            prototype id and lld row        *
 *                                                                            *
 ******************************************************************************/
static void	lld_applications_make(const trx_vector_ptr_t *application_prototypes,
		const trx_vector_ptr_t *lld_rows, const trx_vector_ptr_t *lld_macro_paths,
		trx_vector_ptr_t *applications, trx_hashset_t *applications_index)
{
	int				i, j;
	trx_lld_application_t		*application;
	trx_lld_row_t			*lld_row;
	trx_lld_application_index_t	application_index_local;
	char				*buffer = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* index existing applications */

	for (i = 0; i < applications->values_num; i++)
	{
		application = (trx_lld_application_t *)applications->values[i];

		for (j = 0; j < lld_rows->values_num; j++)
		{
			lld_row = (trx_lld_row_t *)lld_rows->values[j];

			buffer = trx_strdup(buffer, application->name_proto);
			substitute_lld_macros(&buffer, &lld_row->jp_row, lld_macro_paths, TRX_MACRO_ANY, NULL, 0);
			trx_lrtrim(buffer, TRX_WHITESPACE);

			if (0 == strcmp(application->name, buffer))
			{
				application_index_local.application_prototypeid = application->application_prototypeid;
				application_index_local.lld_row = lld_row;
				application_index_local.application = application;
				trx_hashset_insert(applications_index, &application_index_local,
						sizeof(application_index_local));

				application->lld_row = lld_row;
			}
		}
	}

	trx_free(buffer);

	/* make the applications */
	for (i = 0; i < application_prototypes->values_num; i++)
	{
		for (j = 0; j < lld_rows->values_num; j++)
		{
			lld_application_make((trx_lld_application_prototype_t *)application_prototypes->values[i],
					(trx_lld_row_t *)lld_rows->values[j], lld_macro_paths, applications,
					applications_index);
		}
	}

	trx_vector_ptr_sort(applications, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d applications", __func__, applications->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_applications_validate                                        *
 *                                                                            *
 * Purpose: validates the discovered and renamed applications                 *
 *                                                                            *
 * Parameters: hostid             - [IN] host id                              *
 *             lld_ruleid         - [IN] the discovery rule id                *
 *             applications       - [IN/OUT] the applications                 *
 *             applications_index - [OUT] the application index by            *
 *                                        prototype id and lld row            *
 *             error              - [IN/OUT] the lld error message            *
 *                                                                            *
 ******************************************************************************/
static void	lld_applications_validate(trx_uint64_t hostid, trx_uint64_t lld_ruleid, trx_vector_ptr_t *applications,
		trx_hashset_t *applications_index, char **error)
{
	int				i, j, index;
	DB_RESULT			result;
	DB_ROW				row;
	trx_lld_application_t		*application, *new_application, application_local;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;
	trx_vector_str_t		names_new, names_old;
	trx_lld_application_index_t	*application_index, application_index_local;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == applications->values_num)
		goto out;

	trx_vector_str_create(&names_new);
	trx_vector_str_create(&names_old);

	/* check for conflicting application names in the discovered applications */

	for (i = 0; i < applications->values_num; i++)
	{
		application = (trx_lld_application_t *)applications->values[i];

		if (0 == (application->flags & TRX_FLAG_LLD_APPLICATION_DISCOVERED))
			continue;

		if (0 != application->applicationid && 0 == (application->flags & TRX_FLAG_LLD_APPLICATION_UPDATE_NAME))
			continue;

		/* iterate in reverse order so existing applications would have more priority */
		/* than new applications which have 0 applicationid and therefore are located */
		/* at the beginning of applications vector which is sorted by applicationid   */
		for (j = applications->values_num - 1; j > i; j--)
		{
			trx_lld_application_t	*application_compare = (trx_lld_application_t *)applications->values[j];

			if (0 != strcmp(application->name, application_compare->name))
				continue;

			/* Applications with matching names are validated depending on their prototypes. */
			/* If they are discovered by different prototypes we must fail with appropriate  */
			/* lld error.                                                                    */
			/* Otherwise we 'merge' application by updating index of the validated           */
			/* validated application to point at the application with the same name.         */
			/* In both cases the validated application is flagged as non-discovered.         */
			application->flags &= ~TRX_FLAG_LLD_ITEM_DISCOVERED;

			/* fail if application has different prototype */
			if (application->application_prototypeid != application_compare->application_prototypeid)
			{
				*error = trx_strdcatf(*error, "Cannot %s application:"
						" application with the same name \"%s\" already exists.\n",
						(0 != application->applicationid ? "update" : "create"),
						application->name);

				break;
			}

			/* update application index to use the matching application */

			application_index_local.application_prototypeid = application->application_prototypeid;
			application_index_local.lld_row = application->lld_row;

			if (NULL == (application_index = (trx_lld_application_index_t *)trx_hashset_search(applications_index,
					&application_index_local)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				break;
			}

			application_index->application = application_compare;
			break;
		}

		/* Prepare name lists to resolve naming conflicts with applications */
		/* discovered by other discovery rules:                             */
		/*   names_new - to check if discovered/renamed application names   */
		/*               don't match existing applications discovered by    */
		/*               other discovery rules                              */
		/*   names_old - to check if renamed applications were also         */
		/*               discovered by other discovery rules                */
		if (i == j)
		{
			trx_vector_str_append(&names_new, application->name);

			if (NULL != application->name_orig)
				trx_vector_str_append(&names_old, application->name_orig);
		}
	}

	/* validate new/renamed application names against applications discovered */
	/* by other discovery rules                                               */

	if (0 != names_new.values_num)
	{
		trx_vector_str_sort(&names_new, TRX_DEFAULT_STR_COMPARE_FUNC);

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select applicationid,name,flags"
				" from applications"
				" where hostid=" TRX_FS_UI64
					" and",
				hostid);
		DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "name",
				(const char **)names_new.values, names_new.values_num);

		result = DBselect("%s", sql);

		application_local.flags = TRX_FLAG_LLD_APPLICATION_DISCOVERED;

		while (NULL != (row = DBfetch(result)))
		{
			application_local.name = row[1];

			if (FAIL == (index = trx_vector_ptr_search(applications, &application_local,
					lld_application_compare_name)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			application = (trx_lld_application_t *)applications->values[index];

			/* only discovered applications can be 'shared' between discovery rules */
			if (TRX_FLAG_DISCOVERY_CREATED != atoi(row[2]))
			{
				/* conflicting application name, reset discovery flags */
				application->flags = TRX_FLAG_LLD_APPLICATION_UNSET;

				*error = trx_strdcatf(*error, "Cannot create application:"
						" non-discovered application"
						" with the same name \"%s\" already exists.\n",
						application->name);

				continue;
			}

			if (0 != (application->flags & TRX_FLAG_LLD_APPLICATION_UPDATE_NAME))
			{
				/* During discovery process the application was renamed to an */
				/* application already discovered by another discovery rule.  */
				/* In this case we must delete the old application and relink */
				/* its items to the application we have found.                */

				/* create a pseudo application to remove the renamed application */

				new_application = (trx_lld_application_t *)trx_malloc(NULL,
						sizeof(trx_lld_application_t));

				memset(new_application, 0, sizeof(trx_lld_application_t));
				new_application->applicationid = application->applicationid;
				new_application->flags = TRX_FLAG_LLD_APPLICATION_REMOVE;

				trx_vector_ptr_append(applications, new_application);

				/* update application flags so that instead of renaming it a new */
				/* discovery record is created                                   */

				application->application_discoveryid = 0;
				application->flags &= ~TRX_FLAG_LLD_APPLICATION_UPDATE_NAME;
				application->flags |= TRX_FLAG_LLD_APPLICATION_ADD_DISCOVERY;
			}

			/* reuse application created by another discovery rule */
			TRX_STR2UINT64(application->applicationid, row[0]);
		}
		DBfree_result(result);
	}

	/* if an application shared with other discovery rule has been renamed we must */
	/* create a new application with the new name instead of renaming the old one  */

	if (0 != names_old.values_num)
	{
		sql_offset = 0;

		trx_vector_str_sort(&names_old, TRX_DEFAULT_STR_COMPARE_FUNC);

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select a.name"
				" from applications a,application_discovery ad,application_prototype ap"
				" where a.applicationid=ad.applicationid"
					" and ad.application_prototypeid=ap.application_prototypeid"
					" and a.hostid=" TRX_FS_UI64
					" and ap.itemid<>" TRX_FS_UI64
					" and",
				hostid, lld_ruleid);
		DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "a.name",
				(const char **)names_old.values, names_old.values_num);

		result = DBselect("%s", sql);

		application_local.flags = TRX_FLAG_LLD_APPLICATION_DISCOVERED;

		while (NULL != (row = DBfetch(result)))
		{
			application_local.name_orig = row[0];

			if (FAIL == (index = trx_vector_ptr_search(applications, &application_local,
					lld_application_compare_name_orig)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			application = (trx_lld_application_t *)applications->values[index];

			/* add a pseudo application to remove the application discovery record */
			/* of the shared application and current discovery rule                */
			new_application = (trx_lld_application_t *)trx_malloc(NULL, sizeof(trx_lld_application_t));
			memset(new_application, 0, sizeof(trx_lld_application_t));
			new_application->applicationid = application->applicationid;
			new_application->application_prototypeid = application->application_prototypeid;
			new_application->application_discoveryid = application->application_discoveryid;
			new_application->flags = TRX_FLAG_LLD_APPLICATION_REMOVE_DISCOVERY;
			trx_vector_ptr_append(applications, new_application);

			/* reset applicationid, application_discoveryid and flags             */
			/* so a new application is created instead of renaming the shared one */
			application->applicationid = 0;
			application->application_discoveryid = 0;
			application->flags = TRX_FLAG_LLD_APPLICATION_ADD_DISCOVERY |
					TRX_FLAG_LLD_APPLICATION_DISCOVERED;
		}
		DBfree_result(result);
	}

	trx_vector_str_destroy(&names_old);
	trx_vector_str_destroy(&names_new);

	trx_free(sql);

	trx_vector_ptr_sort(applications, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_applications_get                                       *
 *                                                                            *
 * Purpose: gets item-application links for the lld rule                      *
 *                                                                            *
 * Parameters: lld_rule           - [IN] the lld rule                         *
 *             items_applications - [OUT] the item-application links          *
 *                                                                            *
 ******************************************************************************/
static void	lld_items_applications_get(trx_uint64_t lld_ruleid, trx_hashset_t *items_applications)
{
	DB_RESULT			result;
	DB_ROW				row;
	trx_lld_item_application_t	item_application;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select ia.itemappid,ia.itemid,ia.applicationid"
			" from items_applications ia,item_discovery id1,item_discovery id2"
			" where id1.itemid=ia.itemid"
				" and id1.parent_itemid=id2.itemid"
				" and id2.parent_itemid=" TRX_FS_UI64,
			lld_ruleid);

	item_application.application_ref.application = NULL;
	item_application.item_ref.item = NULL;

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(item_application.itemappid, row[0]);
		TRX_STR2UINT64(item_application.item_ref.itemid, row[1]);
		TRX_STR2UINT64(item_application.application_ref.applicationid, row[2]);
		item_application.flags = TRX_FLAG_LLD_ITEM_APPLICATION_UNSET;

		trx_hashset_insert(items_applications, &item_application, sizeof(trx_lld_item_application_t));
	}
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d links", __func__, items_applications->num_data);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_applications_make                                      *
 *                                                                            *
 * Purpose: makes new item-application links and marks existing links as      *
 *          discovered based on item_prototypes applications links            *
 *                                                                            *
 * Parameters: item_prototypes    - [IN] the item prototypes                  *
 *             items              - [IN] the items                            *
 *             applications_index - [IN] the application index by             *
 *                                       prototype id and lld row             *
 *             items_applications - [IN/OUT] the item-application links       *
 *                                                                            *
 ******************************************************************************/
static void	lld_items_applications_make(const trx_vector_ptr_t *item_prototypes, const trx_vector_ptr_t *items,
		trx_hashset_t *applications_index, trx_hashset_t *items_applications)
{
	int				i, j, index;
	trx_lld_item_application_t	*item_application, item_application_local;
	trx_lld_application_t		*application;
	trx_lld_item_prototype_t	*item_prototype;
	trx_lld_item_t			*item;
	trx_lld_item_application_ref_t	*itemapp_prototype;
	trx_lld_application_index_t	*application_index, application_index_local;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	item_application_local.itemappid = 0;
	item_application_local.flags = TRX_FLAG_LLD_ITEM_APPLICATION_DISCOVERED;

	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_lld_item_t *)items->values[i];

		if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		/* if item is discovered its prototype must be in item_prototypes vector */
		index = trx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		item_prototype = (trx_lld_item_prototype_t *)item_prototypes->values[index];

		application_index_local.lld_row = item->lld_row;

		if (0 == (item_application_local.item_ref.itemid = item->itemid))
			item_application_local.item_ref.item = item;
		else
			item_application_local.item_ref.item = NULL;

		for (j = 0; j < item_prototype->applications.values_num; j++)
		{
			itemapp_prototype = (trx_lld_item_application_ref_t *)item_prototype->applications.values[j];

			if (NULL != itemapp_prototype->application_prototype)
			{
				application_index_local.application_prototypeid =
						itemapp_prototype->application_prototype->application_prototypeid;

				if (NULL == (application_index = (trx_lld_application_index_t *)trx_hashset_search(applications_index,
						&application_index_local)))
				{
					continue;
				}

				application = application_index->application;

				if (0 == (application->flags & TRX_FLAG_LLD_APPLICATION_DISCOVERED))
					continue;

				if (0 == (item_application_local.application_ref.applicationid =
						application->applicationid))
				{
					item_application_local.application_ref.application = application;
				}
				else
					item_application_local.application_ref.application = NULL;
			}
			else
			{
				item_application_local.application_ref.application = NULL;
				item_application_local.application_ref.applicationid = itemapp_prototype->applicationid;
			}

			if (NULL == (item_application = (trx_lld_item_application_t *)trx_hashset_search(items_applications,
					&item_application_local)))
			{
				item_application = (trx_lld_item_application_t *)trx_hashset_insert(items_applications, &item_application_local,
						sizeof(trx_lld_item_application_t));
			}

			item_application->flags = TRX_FLAG_LLD_ITEM_APPLICATION_DISCOVERED;
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d links", __func__, items_applications->num_data);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_prototypes_get                                          *
 *                                                                            *
 * Purpose: load discovery rule item prototypes                               *
 *                                                                            *
 * Parameters: lld_ruleid      - [IN] the discovery rule id                   *
 *             item_prototypes - [OUT] the item prototypes                    *
 *                                                                            *
 ******************************************************************************/
static void	lld_item_prototypes_get(trx_uint64_t lld_ruleid, trx_vector_ptr_t *item_prototypes)
{
	DB_RESULT			result;
	DB_ROW				row;
	trx_lld_item_prototype_t	*item_prototype;
	trx_lld_item_preproc_t		*preproc_op;
	trx_uint64_t			itemid;
	int				index, i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select i.itemid,i.name,i.key_,i.type,i.value_type,i.delay,"
				"i.history,i.trends,i.status,i.trapper_hosts,i.units,i.formula,"
				"i.logtimefmt,i.valuemapid,i.params,i.ipmi_sensor,i.snmp_community,i.snmp_oid,"
				"i.port,i.snmpv3_securityname,i.snmpv3_securitylevel,i.snmpv3_authprotocol,"
				"i.snmpv3_authpassphrase,i.snmpv3_privprotocol,i.snmpv3_privpassphrase,i.authtype,"
				"i.username,i.password,i.publickey,i.privatekey,i.description,i.interfaceid,"
				"i.snmpv3_contextname,i.jmx_endpoint,i.master_itemid,i.timeout,i.url,i.query_fields,"
				"i.posts,i.status_codes,i.follow_redirects,i.post_type,i.http_proxy,i.headers,"
				"i.retrieve_mode,i.request_method,i.output_format,i.ssl_cert_file,i.ssl_key_file,"
				"i.ssl_key_password,i.verify_peer,i.verify_host,i.allow_traps"
			" from items i,item_discovery id"
			" where i.itemid=id.itemid"
				" and id.parent_itemid=" TRX_FS_UI64,
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		item_prototype = (trx_lld_item_prototype_t *)trx_malloc(NULL, sizeof(trx_lld_item_prototype_t));

		TRX_STR2UINT64(item_prototype->itemid, row[0]);
		item_prototype->name = trx_strdup(NULL, row[1]);
		item_prototype->key = trx_strdup(NULL, row[2]);
		TRX_STR2UCHAR(item_prototype->type, row[3]);
		TRX_STR2UCHAR(item_prototype->value_type, row[4]);
		item_prototype->delay = trx_strdup(NULL, row[5]);
		item_prototype->history = trx_strdup(NULL, row[6]);
		item_prototype->trends = trx_strdup(NULL, row[7]);
		TRX_STR2UCHAR(item_prototype->status, row[8]);
		item_prototype->trapper_hosts = trx_strdup(NULL, row[9]);
		item_prototype->units = trx_strdup(NULL, row[10]);
		item_prototype->formula = trx_strdup(NULL, row[11]);
		item_prototype->logtimefmt = trx_strdup(NULL, row[12]);
		TRX_DBROW2UINT64(item_prototype->valuemapid, row[13]);
		item_prototype->params = trx_strdup(NULL, row[14]);
		item_prototype->ipmi_sensor = trx_strdup(NULL, row[15]);
		item_prototype->snmp_community = trx_strdup(NULL, row[16]);
		item_prototype->snmp_oid = trx_strdup(NULL, row[17]);
		item_prototype->port = trx_strdup(NULL, row[18]);
		item_prototype->snmpv3_securityname = trx_strdup(NULL, row[19]);
		TRX_STR2UCHAR(item_prototype->snmpv3_securitylevel, row[20]);
		TRX_STR2UCHAR(item_prototype->snmpv3_authprotocol, row[21]);
		item_prototype->snmpv3_authpassphrase = trx_strdup(NULL, row[22]);
		TRX_STR2UCHAR(item_prototype->snmpv3_privprotocol, row[23]);
		item_prototype->snmpv3_privpassphrase = trx_strdup(NULL, row[24]);
		TRX_STR2UCHAR(item_prototype->authtype, row[25]);
		item_prototype->username = trx_strdup(NULL, row[26]);
		item_prototype->password = trx_strdup(NULL, row[27]);
		item_prototype->publickey = trx_strdup(NULL, row[28]);
		item_prototype->privatekey = trx_strdup(NULL, row[29]);
		item_prototype->description = trx_strdup(NULL, row[30]);
		TRX_DBROW2UINT64(item_prototype->interfaceid, row[31]);
		item_prototype->snmpv3_contextname = trx_strdup(NULL, row[32]);
		item_prototype->jmx_endpoint = trx_strdup(NULL, row[33]);
		TRX_DBROW2UINT64(item_prototype->master_itemid, row[34]);

		item_prototype->timeout = trx_strdup(NULL, row[35]);
		item_prototype->url = trx_strdup(NULL, row[36]);
		item_prototype->query_fields = trx_strdup(NULL, row[37]);
		item_prototype->posts = trx_strdup(NULL, row[38]);
		item_prototype->status_codes = trx_strdup(NULL, row[39]);
		TRX_STR2UCHAR(item_prototype->follow_redirects, row[40]);
		TRX_STR2UCHAR(item_prototype->post_type, row[41]);
		item_prototype->http_proxy = trx_strdup(NULL, row[42]);
		item_prototype->headers = trx_strdup(NULL, row[43]);
		TRX_STR2UCHAR(item_prototype->retrieve_mode, row[44]);
		TRX_STR2UCHAR(item_prototype->request_method, row[45]);
		TRX_STR2UCHAR(item_prototype->output_format, row[46]);
		item_prototype->ssl_cert_file = trx_strdup(NULL, row[47]);
		item_prototype->ssl_key_file = trx_strdup(NULL, row[48]);
		item_prototype->ssl_key_password = trx_strdup(NULL, row[49]);
		TRX_STR2UCHAR(item_prototype->verify_peer, row[50]);
		TRX_STR2UCHAR(item_prototype->verify_host, row[51]);
		TRX_STR2UCHAR(item_prototype->allow_traps, row[52]);

		trx_vector_ptr_create(&item_prototype->lld_rows);
		trx_vector_ptr_create(&item_prototype->applications);
		trx_vector_ptr_create(&item_prototype->preproc_ops);

		trx_vector_ptr_append(item_prototypes, item_prototype);
	}
	DBfree_result(result);

	trx_vector_ptr_sort(item_prototypes, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	if (0 == item_prototypes->values_num)
		goto out;

	/* get item prototype preprocessing options */

	result = DBselect(
			"select ip.itemid,ip.step,ip.type,ip.params,ip.error_handler,ip.error_handler_params"
			" from item_preproc ip,item_discovery id"
			" where ip.itemid=id.itemid"
				" and id.parent_itemid=" TRX_FS_UI64,
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(itemid, row[0]);

		if (FAIL == (index = trx_vector_ptr_bsearch(item_prototypes, &itemid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item_prototype = (trx_lld_item_prototype_t *)item_prototypes->values[index];

		preproc_op = (trx_lld_item_preproc_t *)trx_malloc(NULL, sizeof(trx_lld_item_preproc_t));
		preproc_op->step = atoi(row[1]);
		preproc_op->type = atoi(row[2]);
		preproc_op->params = trx_strdup(NULL, row[3]);
		preproc_op->error_handler = atoi(row[4]);
		preproc_op->error_handler_params = trx_strdup(NULL, row[5]);
		trx_vector_ptr_append(&item_prototype->preproc_ops, preproc_op);
	}
	DBfree_result(result);

	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (trx_lld_item_prototype_t *)item_prototypes->values[i];
		trx_vector_ptr_sort(&item_prototype->preproc_ops, lld_item_preproc_sort_by_step);
	}

out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d prototypes", __func__, item_prototypes->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_link_dependent_items                                         *
 *                                                                            *
 * Purpose: create dependent item index in master item data                   *
 *                                                                            *
 * Parameters: items       - [IN/OUT] the lld items                           *
 *             items_index - [IN] lld item index                              *
 *                                                                            *
 ******************************************************************************/
static void	lld_link_dependent_items(trx_vector_ptr_t *items, trx_hashset_t *items_index)
{
	trx_lld_item_t		*item, *master;
	trx_lld_item_index_t	*item_index, item_index_local;
	int			i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = items->values_num - 1; i >= 0; i--)
	{
		item = (trx_lld_item_t *)items->values[i];
		/* only discovered dependent items should be linked */
		if (0 == (item->flags & TRX_FLAG_LLD_ITEM_DISCOVERED) || 0 == item->master_itemid)
			continue;

		item_index_local.parent_itemid = item->master_itemid;
		item_index_local.lld_row = (trx_lld_row_t *)item->lld_row;

		if (NULL != (item_index = (trx_lld_item_index_t *)trx_hashset_search(items_index, &item_index_local)))
		{
			master = item_index->item;
			trx_vector_ptr_append(&master->dependent_items, item);
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_update_items                                                 *
 *                                                                            *
 * Purpose: add or update discovered items                                    *
 *                                                                            *
 * Return value: SUCCEED - if items were successfully added/updated or        *
 *                         adding/updating was not necessary                  *
 *               FAIL    - items cannot be added/updated                      *
 *                                                                            *
 ******************************************************************************/
int	lld_update_items(trx_uint64_t hostid, trx_uint64_t lld_ruleid, const trx_vector_ptr_t *lld_rows,
		const trx_vector_ptr_t *lld_macro_paths, char **error, int lifetime, int lastcheck)
{
	trx_vector_ptr_t	applications, application_prototypes, items, item_prototypes, item_dependencies;
	trx_hashset_t		applications_index, items_index, items_applications;
	int			ret = SUCCEED, host_record_is_locked = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&item_prototypes);

	lld_item_prototypes_get(lld_ruleid, &item_prototypes);

	if (0 == item_prototypes.values_num)
		goto out;

	trx_vector_ptr_create(&application_prototypes);

	lld_application_prototypes_get(lld_ruleid, &application_prototypes);

	trx_vector_ptr_create(&applications);
	trx_hashset_create(&applications_index, application_prototypes.values_num * lld_rows->values_num,
			lld_application_index_hash_func, lld_application_index_compare_func);

	trx_vector_ptr_create(&items);
	trx_hashset_create(&items_index, item_prototypes.values_num * lld_rows->values_num, lld_item_index_hash_func,
			lld_item_index_compare_func);

	trx_hashset_create(&items_applications, 100, lld_item_application_hash_func, lld_item_application_compare_func);

	lld_applications_get(lld_ruleid, &applications);
	lld_applications_make(&application_prototypes, lld_rows, lld_macro_paths, &applications, &applications_index);
	lld_applications_validate(hostid, lld_ruleid, &applications, &applications_index, error);

	lld_item_application_prototypes_get(&item_prototypes, &application_prototypes);

	lld_items_get(&item_prototypes, &items);
	lld_items_make(&item_prototypes, lld_rows, lld_macro_paths, &items, &items_index, error);
	lld_items_preproc_make(&item_prototypes, lld_macro_paths, &items);

	lld_link_dependent_items(&items, &items_index);

	trx_vector_ptr_create(&item_dependencies);
	lld_item_dependencies_get(&item_prototypes, &item_dependencies);

	lld_items_validate(hostid, &items, &item_prototypes, &item_dependencies, error);

	lld_items_applications_get(lld_ruleid, &items_applications);
	lld_items_applications_make(&item_prototypes, &items, &applications_index, &items_applications);

	DBbegin();

	if (SUCCEED == lld_items_save(hostid, &item_prototypes, &items, &items_index, &host_record_is_locked) &&
			SUCCEED == lld_items_preproc_save(hostid, &items, &host_record_is_locked) &&
			SUCCEED == lld_applications_save(hostid, &applications, &application_prototypes,
					&host_record_is_locked))
	{
		lld_items_applications_save(&items_applications, &items, &applications);
		DBcommit();
	}
	else
	{
		ret = FAIL;
		DBrollback();
		goto clean;
	}

	lld_item_links_populate(&item_prototypes, lld_rows, &items_index);
	lld_remove_lost_items(&items, lifetime, lastcheck);
	lld_remove_lost_applications(lld_ruleid, &applications, lifetime, lastcheck);
clean:
	trx_hashset_destroy(&items_applications);
	trx_hashset_destroy(&items_index);

	trx_vector_ptr_clear_ext(&item_dependencies, trx_ptr_free);
	trx_vector_ptr_destroy(&item_dependencies);

	trx_vector_ptr_clear_ext(&items, (trx_clean_func_t)lld_item_free);
	trx_vector_ptr_destroy(&items);

	trx_hashset_destroy(&applications_index);

	trx_vector_ptr_clear_ext(&applications, (trx_clean_func_t)lld_application_free);
	trx_vector_ptr_destroy(&applications);

	trx_vector_ptr_clear_ext(&application_prototypes, (trx_clean_func_t)lld_application_prototype_free);
	trx_vector_ptr_destroy(&application_prototypes);

	trx_vector_ptr_clear_ext(&item_prototypes, (trx_clean_func_t)lld_item_prototype_free);
out:
	trx_vector_ptr_destroy(&item_prototypes);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}
