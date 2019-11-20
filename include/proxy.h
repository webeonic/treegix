

#ifndef TREEGIX_PROXY_H
#define TREEGIX_PROXY_H

#include "trxjson.h"
#include "comms.h"
#include "dbcache.h"

#define TRX_PROXYMODE_ACTIVE	0
#define TRX_PROXYMODE_PASSIVE	1

#define TRX_MAX_HRECORDS	1000
#define TRX_MAX_HRECORDS_TOTAL	10000

#define TRX_PROXY_DATA_DONE	0
#define TRX_PROXY_DATA_MORE	1

int	get_active_proxy_from_request(struct trx_json_parse *jp, DC_PROXY *proxy, char **error);
int	trx_proxy_check_permissions(const DC_PROXY *proxy, const trx_socket_t *sock, char **error);
int	check_access_passive_proxy(trx_socket_t *sock, int send_response, const char *req);

void	update_proxy_lastaccess(const trx_uint64_t hostid, time_t last_access);

int	get_proxyconfig_data(trx_uint64_t proxy_hostid, struct trx_json *j, char **error);
void	process_proxyconfig(struct trx_json_parse *jp_data);

int	get_host_availability_data(struct trx_json *j, int *ts);
int	process_host_availability(struct trx_json_parse *jp_data, char **error);

int	proxy_get_hist_data(struct trx_json *j, trx_uint64_t *lastid, int *more);
int	proxy_get_dhis_data(struct trx_json *j, trx_uint64_t *lastid, int *more);
int	proxy_get_areg_data(struct trx_json *j, trx_uint64_t *lastid, int *more);
void	proxy_set_hist_lastid(const trx_uint64_t lastid);
void	proxy_set_dhis_lastid(const trx_uint64_t lastid);
void	proxy_set_areg_lastid(const trx_uint64_t lastid);

void	calc_timestamp(const char *line, int *timestamp, const char *format);

int	process_history_data(DC_ITEM *items, trx_agent_value_t *values, int *errcodes, size_t values_num);
int	process_auto_registration(struct trx_json_parse *jp, trx_uint64_t proxy_hostid, trx_timespec_t *ts, char **error);

int	lld_process_discovery_rule(trx_uint64_t lld_ruleid, const char *value, char **error);

int	proxy_get_history_count(void);

int	trx_get_proxy_protocol_version(struct trx_json_parse *jp);
void	trx_update_proxy_data(DC_PROXY *proxy, int version, int lastaccess, int compress);

int	process_proxy_history_data(const DC_PROXY *proxy, struct trx_json_parse *jp, trx_timespec_t *ts, char **info);
int	process_agent_history_data(trx_socket_t *sock, struct trx_json_parse *jp, trx_timespec_t *ts, char **info);
int	process_sender_history_data(trx_socket_t *sock, struct trx_json_parse *jp, trx_timespec_t *ts, char **info);
int	process_proxy_data(const DC_PROXY *proxy, struct trx_json_parse *jp, trx_timespec_t *ts, char **error);
int	trx_check_protocol_version(DC_PROXY *proxy);

#endif
