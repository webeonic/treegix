

#ifndef TREEGIX_TRXSERVER_H
#define TREEGIX_TRXSERVER_H

#include "common.h"
#include "db.h"
#include "dbcache.h"
#include "trxjson.h"

#define MACRO_TYPE_MESSAGE_NORMAL	0x00000001
#define MACRO_TYPE_MESSAGE_RECOVERY	0x00000002
#define MACRO_TYPE_TRIGGER_URL		0x00000004
#define MACRO_TYPE_TRIGGER_EXPRESSION	0x00000008
#define MACRO_TYPE_TRIGGER_DESCRIPTION	0x00000010	/* name */
#define MACRO_TYPE_TRIGGER_COMMENTS	0x00000020	/* description */
#define MACRO_TYPE_ITEM_KEY		0x00000040
#define MACRO_TYPE_ITEM_EXPRESSION	0x00000080
#define MACRO_TYPE_INTERFACE_ADDR	0x00000100
#define MACRO_TYPE_COMMON		0x00000400
#define MACRO_TYPE_PARAMS_FIELD		0x00000800
#define MACRO_TYPE_SCRIPT		0x00001000
#define MACRO_TYPE_SNMP_OID		0x00002000
#define MACRO_TYPE_HTTPTEST_FIELD	0x00004000
#define MACRO_TYPE_LLD_FILTER		0x00008000
#define MACRO_TYPE_ALERT		0x00010000
#define MACRO_TYPE_TRIGGER_TAG		0x00020000
#define MACRO_TYPE_JMX_ENDPOINT		0x00040000
#define MACRO_TYPE_MESSAGE_ACK		0x00080000
#define MACRO_TYPE_HTTP_RAW		0x00100000
#define MACRO_TYPE_HTTP_JSON		0x00200000
#define MACRO_TYPE_HTTP_XML		0x00400000
#define MACRO_TYPE_ALLOWED_HOSTS	0x00800000
#define MACRO_TYPE_ITEM_TAG		0x01000000

#define STR_CONTAINS_MACROS(str)	(NULL != strchr(str, '{'))

int	get_N_functionid(const char *expression, int N_functionid, trx_uint64_t *functionid, const char **end);
void	get_functionids(trx_vector_uint64_t *functionids, const char *expression);

int	evaluate_function(char *value, DC_ITEM *item, const char *function, const char *parameters,
		const trx_timespec_t *ts, char **error);

int	substitute_simple_macros(trx_uint64_t *actionid, const DB_EVENT *event, const DB_EVENT *r_event,
		trx_uint64_t *userid, const trx_uint64_t *hostid, const DC_HOST *dc_host, const DC_ITEM *dc_item,
		DB_ALERT *alert, const DB_ACKNOWLEDGE *ack, char **data, int macro_type, char *error, int maxerrlen);

void	evaluate_expressions(trx_vector_ptr_t *triggers);

void	trx_format_value(char *value, size_t max_len, trx_uint64_t valuemapid,
		const char *units, unsigned char value_type);

void	trx_determine_items_in_expressions(trx_vector_ptr_t *trigger_order, const trx_uint64_t *itemids, int item_num);

/* lld macro context */
#define TRX_MACRO_ANY		(TRX_TOKEN_LLD_MACRO | TRX_TOKEN_LLD_FUNC_MACRO | TRX_TOKEN_USER_MACRO)
#define TRX_MACRO_NUMERIC	(TRX_MACRO_ANY | TRX_TOKEN_NUMERIC)
#define TRX_MACRO_JSON		(TRX_MACRO_ANY | TRX_TOKEN_JSON)
#define TRX_MACRO_XML		(TRX_MACRO_ANY | TRX_TOKEN_XML)
#define TRX_MACRO_SIMPLE	(TRX_MACRO_ANY | TRX_TOKEN_SIMPLE_MACRO)
#define TRX_MACRO_FUNC		(TRX_MACRO_ANY | TRX_TOKEN_FUNC_MACRO)

int	substitute_lld_macros(char **data, const struct trx_json_parse *jp_row, const trx_vector_ptr_t *lld_macro_paths,
		int flags, char *error, size_t max_error_len);
int	substitute_key_macros(char **data, trx_uint64_t *hostid, DC_ITEM *dc_item, const struct trx_json_parse *jp_row,
		const trx_vector_ptr_t *lld_macro_paths, int macro_type, char *error, size_t mexerrlen);
int	substitute_function_lld_param(const char *e, size_t len, unsigned char key_in_param,
		char **exp, size_t *exp_alloc, size_t *exp_offset, const struct trx_json_parse *jp_row,
		const trx_vector_ptr_t *lld_macro_paths, char *error, size_t max_error_len);
int	substitute_macros_xml(char **data, const DC_ITEM *item, const struct trx_json_parse *jp_row,
		const trx_vector_ptr_t *lld_macro_paths, char *error, int maxerrlen);
int	trx_substitute_item_name_macros(DC_ITEM *dc_item, const char *name, char **replace_to);
int	substitute_macros_in_json_pairs(char **data, const struct trx_json_parse *jp_row,
		const trx_vector_ptr_t *lld_macro_paths, char *error, int maxerrlen);
int	xml_xpath_check(const char *xpath, char *error, size_t errlen);

#endif
