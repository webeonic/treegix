

#include <stddef.h>

#include "common.h"
#include "log.h"
#include "threads.h"
#include "dbcache.h"
#include "ipc.h"
#include "mutexs.h"
#include "memalloc.h"
#include "trxserver.h"
#include "trxalgo.h"
#include "trxregexp.h"
#include "cfg.h"
#include "trxtasks.h"
#include "../trxcrypto/tls_tcp_active.h"

#define TRX_DBCONFIG_IMPL
#include "dbconfig.h"
#include "dbsync.h"

int	sync_in_progress = 0;

#define START_SYNC	WRLOCK_CACHE; sync_in_progress = 1
#define FINISH_SYNC	sync_in_progress = 0; UNLOCK_CACHE

#define TRX_LOC_NOWHERE	0
#define TRX_LOC_QUEUE	1
#define TRX_LOC_POLLER	2

#define TRX_SNMP_OID_TYPE_NORMAL	0
#define TRX_SNMP_OID_TYPE_DYNAMIC	1
#define TRX_SNMP_OID_TYPE_MACRO		2

/* trigger is functional unless its expression contains disabled or not monitored items */
#define TRIGGER_FUNCTIONAL_TRUE		0
#define TRIGGER_FUNCTIONAL_FALSE	1

/* trigger contains time functions and is also scheduled by timer queue */
#define TRX_TRIGGER_TIMER_UNKNOWN	0
#define TRX_TRIGGER_TIMER_QUEUE		1

/* item priority in poller queue */
#define TRX_QUEUE_PRIORITY_HIGH		0
#define TRX_QUEUE_PRIORITY_NORMAL	1
#define TRX_QUEUE_PRIORITY_LOW		2

/* shorthand macro for calling in_maintenance_without_data_collection() */
#define DCin_maintenance_without_data_collection(dc_host, dc_item)			\
		in_maintenance_without_data_collection(dc_host->maintenance_status,	\
				dc_host->maintenance_type, dc_item->type)

/******************************************************************************
 *                                                                            *
 * Function: trx_value_validator_func_t                                       *
 *                                                                            *
 * Purpose: validate macro value when expanding user macros                   *
 *                                                                            *
 * Parameters: macro   - [IN] the user macro                                  *
 *             value   - [IN] the macro value                                 *
 *             error   - [OUT] the error message                              *
 *                                                                            *
 * Return value: SUCCEED - the value is valid                                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
typedef int (*trx_value_validator_func_t)(const char *macro, const char *value, char **error);

TRX_DC_CONFIG	*config = NULL;
trx_rwlock_t	config_lock = TRX_RWLOCK_NULL;
static trx_mem_info_t	*config_mem;

extern unsigned char	program_type;
extern int		CONFIG_TIMER_FORKS;

TRX_MEM_FUNC_IMPL(__config, config_mem)

static void	dc_maintenance_precache_nested_groups(void);

/******************************************************************************
 *                                                                            *
 * Function: dc_strdup                                                        *
 *                                                                            *
 * Purpose: copies string into configuration cache shared memory              *
 *                                                                            *
 ******************************************************************************/
static char	*dc_strdup(const char *source)
{
	char	*dst;
	size_t	len;

	len = strlen(source) + 1;
	dst = (char *)__config_mem_malloc_func(NULL, len);
	memcpy(dst, source, len);
	return dst;
}

/******************************************************************************
 *                                                                            *
 * Function: is_item_processed_by_server                                      *
 *                                                                            *
 * Parameters: type - [IN] item type [ITEM_TYPE_* flag]                       *
 *             key  - [IN] item key                                           *
 *                                                                            *
 * Return value: SUCCEED when an item should be processed by server           *
 *               FAIL otherwise                                               *
 *                                                                            *
 * Comments: list of the items, always processed by server                    *
 *           ,------------------+--------------------------------------,      *
 *           | type             | key                                  |      *
 *           +------------------+--------------------------------------+      *
 *           | Treegix internal  | treegix[host,,items]                  |      *
 *           | Treegix internal  | treegix[host,,items_unsupported]      |      *
 *           | Treegix internal  | treegix[host,discovery,interfaces]    |      *
 *           | Treegix internal  | treegix[host,,maintenance]            |      *
 *           | Treegix internal  | treegix[proxy,<proxyname>,lastaccess] |      *
 *           | Treegix aggregate | *                                    |      *
 *           | Calculated       | *                                    |      *
 *           '------------------+--------------------------------------'      *
 *                                                                            *
 ******************************************************************************/
int	is_item_processed_by_server(unsigned char type, const char *key)
{
	int	ret = FAIL;

	switch (type)
	{
		case ITEM_TYPE_AGGREGATE:
		case ITEM_TYPE_CALCULATED:
			ret = SUCCEED;
			break;

		case ITEM_TYPE_INTERNAL:
			if (0 == strncmp(key, "treegix[", 7))
			{
				AGENT_REQUEST	request;
				char		*arg1, *arg2, *arg3;

				init_request(&request);

				if (SUCCEED != parse_item_key(key, &request) || 3 != request.nparam)
					goto clean;

				arg1 = get_rparam(&request, 0);
				arg2 = get_rparam(&request, 1);
				arg3 = get_rparam(&request, 2);

				if (0 == strcmp(arg1, "host"))
				{
					if ('\0' == *arg2)
					{
						if (0 == strcmp(arg3, "maintenance") || 0 == strcmp(arg3, "items") ||
								0 == strcmp(arg3, "items_unsupported"))
						{
							ret = SUCCEED;
						}
					}
					else if (0 == strcmp(arg2, "discovery") && 0 == strcmp(arg3, "interfaces"))
						ret = SUCCEED;
				}
				else if (0 == strcmp(arg1, "proxy") && 0 == strcmp(arg3, "lastaccess"))
					ret = SUCCEED;
clean:
				free_request(&request);
			}
			break;
	}

	return ret;
}

static unsigned char	poller_by_item(unsigned char type, const char *key)
{
	switch (type)
	{
		case ITEM_TYPE_SIMPLE:
			if (SUCCEED == cmp_key_id(key, SERVER_ICMPPING_KEY) ||
					SUCCEED == cmp_key_id(key, SERVER_ICMPPINGSEC_KEY) ||
					SUCCEED == cmp_key_id(key, SERVER_ICMPPINGLOSS_KEY))
			{
				if (0 == CONFIG_PINGER_FORKS)
					break;

				return TRX_POLLER_TYPE_PINGER;
			}
			TRX_FALLTHROUGH;
		case ITEM_TYPE_TREEGIX:
		case ITEM_TYPE_SNMPv1:
		case ITEM_TYPE_SNMPv2c:
		case ITEM_TYPE_SNMPv3:
		case ITEM_TYPE_INTERNAL:
		case ITEM_TYPE_AGGREGATE:
		case ITEM_TYPE_EXTERNAL:
		case ITEM_TYPE_DB_MONITOR:
		case ITEM_TYPE_SSH:
		case ITEM_TYPE_TELNET:
		case ITEM_TYPE_CALCULATED:
		case ITEM_TYPE_HTTPAGENT:
			if (0 == CONFIG_POLLER_FORKS)
				break;

			return TRX_POLLER_TYPE_NORMAL;
		case ITEM_TYPE_IPMI:
			if (0 == CONFIG_IPMIPOLLER_FORKS)
				break;

			return TRX_POLLER_TYPE_IPMI;
		case ITEM_TYPE_JMX:
			if (0 == CONFIG_JAVAPOLLER_FORKS)
				break;

			return TRX_POLLER_TYPE_JAVA;
	}

	return TRX_NO_POLLER;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_is_counted_in_item_queue                                     *
 *                                                                            *
 * Purpose: determine whether the given item type is counted in item queue    *
 *                                                                            *
 * Return value: SUCCEED if item is counted in the queue, FAIL otherwise      *
 *                                                                            *
 ******************************************************************************/
int	trx_is_counted_in_item_queue(unsigned char type, const char *key)
{
	switch (type)
	{
		case ITEM_TYPE_TREEGIX_ACTIVE:
			if (0 == strncmp(key, "log[", 4) ||
					0 == strncmp(key, "logrt[", 6) ||
					0 == strncmp(key, "eventlog[", 9))
			{
				return FAIL;
			}
			break;
		case ITEM_TYPE_TRAPPER:
		case ITEM_TYPE_DEPENDENT:
		case ITEM_TYPE_HTTPTEST:
		case ITEM_TYPE_SNMPTRAP:
			return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: get_item_nextcheck_seed                                          *
 *                                                                            *
 * Purpose: get the seed value to be used for item nextcheck calculations     *
 *                                                                            *
 * Return value: the seed for nextcheck calculations                          *
 *                                                                            *
 * Comments: The seed value is used to spread multiple item nextchecks over   *
 *           the item delay period to even the system load.                   *
 *           Items with the same delay period and seed value will have the    *
 *           same nextcheck values.                                           *
 *                                                                            *
 ******************************************************************************/
static trx_uint64_t	get_item_nextcheck_seed(trx_uint64_t itemid, trx_uint64_t interfaceid, unsigned char type,
		const char *key)
{
	if (ITEM_TYPE_JMX == type)
		return interfaceid;

	if (SUCCEED == is_snmp_type(type))
	{
		TRX_DC_INTERFACE	*interface;

		if (NULL == (interface = (TRX_DC_INTERFACE *)trx_hashset_search(&config->interfaces, &interfaceid)) ||
				SNMP_BULK_ENABLED != interface->bulk)
		{
			return itemid;
		}

		return interfaceid;
	}

	if (ITEM_TYPE_SIMPLE == type)
	{
		if (SUCCEED == cmp_key_id(key, SERVER_ICMPPING_KEY) ||
				SUCCEED == cmp_key_id(key, SERVER_ICMPPINGSEC_KEY) ||
				SUCCEED == cmp_key_id(key, SERVER_ICMPPINGLOSS_KEY))
		{
			return interfaceid;
		}
	}

	return itemid;
}

static int	DCget_disable_until(const TRX_DC_ITEM *item, const TRX_DC_HOST *host);

#define TRX_ITEM_COLLECTED		0x01	/* force item rescheduling after new value collection */
#define TRX_HOST_UNREACHABLE		0x02
#define TRX_ITEM_KEY_CHANGED		0x04
#define TRX_ITEM_TYPE_CHANGED		0x08
#define TRX_ITEM_DELAY_CHANGED		0x10
#define TRX_REFRESH_UNSUPPORTED_CHANGED	0x20

static int	DCitem_nextcheck_update(TRX_DC_ITEM *item, const TRX_DC_HOST *host, unsigned char new_state,
		int flags, int now, char **error)
{
	trx_uint64_t	seed;

	if (0 == (flags & TRX_ITEM_COLLECTED) && 0 != item->nextcheck &&
			0 == (flags & TRX_ITEM_KEY_CHANGED) && 0 == (flags & TRX_ITEM_TYPE_CHANGED) &&
			((ITEM_STATE_NORMAL == new_state && 0 == (flags & TRX_ITEM_DELAY_CHANGED)) ||
			(ITEM_STATE_NOTSUPPORTED == new_state && 0 == (flags & (0 == item->schedulable ?
					TRX_ITEM_DELAY_CHANGED : TRX_REFRESH_UNSUPPORTED_CHANGED)))))
	{
		return SUCCEED;	/* avoid unnecessary nextcheck updates when syncing items in cache */
	}

	seed = get_item_nextcheck_seed(item->itemid, item->interfaceid, item->type, item->key);

	/* for new items, supported items and items that are notsupported due to invalid update interval try to parse */
	/* interval first and then decide whether it should become/remain supported/notsupported */
	if (0 == item->nextcheck || ITEM_STATE_NORMAL == new_state || 0 == item->schedulable)
	{
		int			simple_interval;
		trx_custom_interval_t	*custom_intervals;

		if (SUCCEED != trx_interval_preproc(item->delay, &simple_interval, &custom_intervals, error))
		{
			/* Polling items with invalid update intervals repeatedly does not make sense because they */
			/* can only be healed by editing configuration (either update interval or macros involved) */
			/* and such changes will be detected during configuration synchronization. DCsync_items()  */
			/* detects item configuration changes affecting check scheduling and passes them in flags. */

			item->nextcheck = TRX_JAN_2038;
			item->schedulable = 0;
			return FAIL;
		}

		if (ITEM_STATE_NORMAL == new_state || 0 == item->schedulable)
		{
			int	disable_until;

			if (0 != (flags & TRX_HOST_UNREACHABLE) && 0 != (disable_until =
					DCget_disable_until(item, host)))
			{
				item->nextcheck = calculate_item_nextcheck_unreachable(simple_interval,
						custom_intervals, disable_until);
			}
			else
			{
				/* supported items and items that could not have been scheduled previously, but had */
				/* their update interval fixed, should be scheduled using their update intervals */
				item->nextcheck = calculate_item_nextcheck(seed, item->type, simple_interval,
						custom_intervals, now);
			}
		}
		else
		{
			/* use refresh_unsupported interval for new items that have a valid update interval of their */
			/* own, but were synced from the database in ITEM_STATE_NOTSUPPORTED state */
			item->nextcheck = calculate_item_nextcheck(seed, item->type, config->config->refresh_unsupported,
					NULL, now);
		}

		trx_custom_interval_free(custom_intervals);
	}
	else	/* for items notsupported for other reasons use refresh_unsupported interval */
	{
		item->nextcheck = calculate_item_nextcheck(seed, item->type, config->config->refresh_unsupported, NULL,
				now);
	}

	item->schedulable = 1;

	return SUCCEED;
}

static void	DCitem_poller_type_update(TRX_DC_ITEM *dc_item, const TRX_DC_HOST *dc_host, int flags)
{
	unsigned char	poller_type;

	if (0 != dc_host->proxy_hostid && SUCCEED != is_item_processed_by_server(dc_item->type, dc_item->key))
	{
		dc_item->poller_type = TRX_NO_POLLER;
		return;
	}

	poller_type = poller_by_item(dc_item->type, dc_item->key);

	if (0 != (flags & TRX_HOST_UNREACHABLE))
	{
		if (TRX_POLLER_TYPE_NORMAL == poller_type || TRX_POLLER_TYPE_JAVA == poller_type)
			poller_type = TRX_POLLER_TYPE_UNREACHABLE;

		dc_item->poller_type = poller_type;
		return;
	}

	if (0 != (flags & TRX_ITEM_COLLECTED))
	{
		dc_item->poller_type = poller_type;
		return;
	}

	if (TRX_POLLER_TYPE_UNREACHABLE != dc_item->poller_type ||
			(TRX_POLLER_TYPE_NORMAL != poller_type && TRX_POLLER_TYPE_JAVA != poller_type))
	{
		dc_item->poller_type = poller_type;
	}
}

static int	DCget_disable_until(const TRX_DC_ITEM *item, const TRX_DC_HOST *host)
{
	switch (item->type)
	{
		case ITEM_TYPE_TREEGIX:
			if (0 != host->errors_from)
				return host->disable_until;
			break;
		case ITEM_TYPE_SNMPv1:
		case ITEM_TYPE_SNMPv2c:
		case ITEM_TYPE_SNMPv3:
			if (0 != host->snmp_errors_from)
				return host->snmp_disable_until;
			break;
		case ITEM_TYPE_IPMI:
			if (0 != host->ipmi_errors_from)
				return host->ipmi_disable_until;
			break;
		case ITEM_TYPE_JMX:
			if (0 != host->jmx_errors_from)
				return host->jmx_disable_until;
			break;
		default:
			/* nothing to do */;
	}

	return 0;
}

static void	DCincrease_disable_until(const TRX_DC_ITEM *item, TRX_DC_HOST *host, int now)
{
	switch (item->type)
	{
		case ITEM_TYPE_TREEGIX:
			if (0 != host->errors_from)
				host->disable_until = now + CONFIG_TIMEOUT;
			break;
		case ITEM_TYPE_SNMPv1:
		case ITEM_TYPE_SNMPv2c:
		case ITEM_TYPE_SNMPv3:
			if (0 != host->snmp_errors_from)
				host->snmp_disable_until = now + CONFIG_TIMEOUT;
			break;
		case ITEM_TYPE_IPMI:
			if (0 != host->ipmi_errors_from)
				host->ipmi_disable_until = now + CONFIG_TIMEOUT;
			break;
		case ITEM_TYPE_JMX:
			if (0 != host->jmx_errors_from)
				host->jmx_disable_until = now + CONFIG_TIMEOUT;
			break;
		default:
			/* nothing to do */;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: DCfind_id                                                        *
 *                                                                            *
 * Purpose: Find an element in a hashset by its 'id' or create the element if *
 *          it does not exist                                                 *
 *                                                                            *
 * Parameters:                                                                *
 *     hashset - [IN] hashset to search                                       *
 *     id      - [IN] id of element to search for                             *
 *     size    - [IN] size of element to search for                           *
 *     found   - [OUT flag. 0 - element did not exist, it was created.        *
 *                          1 - existing element was found.                   *
 *                                                                            *
 * Return value: pointer to the found or created element                      *
 *                                                                            *
 ******************************************************************************/
void	*DCfind_id(trx_hashset_t *hashset, trx_uint64_t id, size_t size, int *found)
{
	void		*ptr;
	trx_uint64_t	buffer[1024];	/* adjust buffer size to accommodate any type DCfind_id() can be called for */

	if (NULL == (ptr = trx_hashset_search(hashset, &id)))
	{
		*found = 0;

		buffer[0] = id;
		ptr = trx_hashset_insert(hashset, &buffer[0], size);
	}
	else
	{
		*found = 1;
	}

	return ptr;
}

static TRX_DC_ITEM	*DCfind_item(trx_uint64_t hostid, const char *key)
{
	TRX_DC_ITEM_HK	*item_hk, item_hk_local;

	item_hk_local.hostid = hostid;
	item_hk_local.key = key;

	if (NULL == (item_hk = (TRX_DC_ITEM_HK *)trx_hashset_search(&config->items_hk, &item_hk_local)))
		return NULL;
	else
		return item_hk->item_ptr;
}

static TRX_DC_HOST	*DCfind_host(const char *host)
{
	TRX_DC_HOST_H	*host_h, host_h_local;

	host_h_local.host = host;

	if (NULL == (host_h = (TRX_DC_HOST_H *)trx_hashset_search(&config->hosts_h, &host_h_local)))
		return NULL;
	else
		return host_h->host_ptr;
}

/******************************************************************************
 *                                                                            *
 * Function: DCfind_proxy                                                     *
 *                                                                            *
 * Purpose: Find a record with proxy details in configuration cache using the *
 *          proxy name                                                        *
 *                                                                            *
 * Parameters: host - [IN] proxy name                                         *
 *                                                                            *
 * Return value: pointer to record if found or NULL otherwise                 *
 *                                                                            *
 ******************************************************************************/
static TRX_DC_HOST	*DCfind_proxy(const char *host)
{
	TRX_DC_HOST_H	*host_p, host_p_local;

	host_p_local.host = host;

	if (NULL == (host_p = (TRX_DC_HOST_H *)trx_hashset_search(&config->hosts_p, &host_p_local)))
		return NULL;
	else
		return host_p->host_ptr;
}

/* private strpool functions */

#define	REFCOUNT_FIELD_SIZE	sizeof(trx_uint32_t)

static trx_hash_t	__config_strpool_hash(const void *data)
{
	return TRX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
}

static int	__config_strpool_compare(const void *d1, const void *d2)
{
	return strcmp((char *)d1 + REFCOUNT_FIELD_SIZE, (char *)d2 + REFCOUNT_FIELD_SIZE);
}

static const char	*trx_strpool_intern(const char *str)
{
	void		*record;
	trx_uint32_t	*refcount;

	record = trx_hashset_search(&config->strpool, str - REFCOUNT_FIELD_SIZE);

	if (NULL == record)
	{
		record = trx_hashset_insert_ext(&config->strpool, str - REFCOUNT_FIELD_SIZE,
				REFCOUNT_FIELD_SIZE + strlen(str) + 1, REFCOUNT_FIELD_SIZE);
		*(trx_uint32_t *)record = 0;
	}

	refcount = (trx_uint32_t *)record;
	(*refcount)++;

	return (char *)record + REFCOUNT_FIELD_SIZE;
}

void	trx_strpool_release(const char *str)
{
	trx_uint32_t	*refcount;

	refcount = (trx_uint32_t *)(str - REFCOUNT_FIELD_SIZE);
	if (0 == --(*refcount))
		trx_hashset_remove(&config->strpool, str - REFCOUNT_FIELD_SIZE);
}

static const char	*trx_strpool_acquire(const char *str)
{
	trx_uint32_t	*refcount;

	refcount = (trx_uint32_t *)(str - REFCOUNT_FIELD_SIZE);
	(*refcount)++;

	return str;
}

int	DCstrpool_replace(int found, const char **curr, const char *new_str)
{
	if (1 == found)
	{
		if (0 == strcmp(*curr, new_str))
			return FAIL;

		trx_strpool_release(*curr);
	}

	*curr = trx_strpool_intern(new_str);

	return SUCCEED;	/* indicate that the string has been replaced */
}

static void	DCupdate_item_queue(TRX_DC_ITEM *item, unsigned char old_poller_type, int old_nextcheck)
{
	trx_binary_heap_elem_t	elem;

	if (TRX_LOC_POLLER == item->location)
		return;

	if (TRX_LOC_QUEUE == item->location && old_poller_type != item->poller_type)
	{
		item->location = TRX_LOC_NOWHERE;
		trx_binary_heap_remove_direct(&config->queues[old_poller_type], item->itemid);
	}

	if (item->poller_type == TRX_NO_POLLER)
		return;

	if (TRX_LOC_QUEUE == item->location && old_nextcheck == item->nextcheck)
		return;

	elem.key = item->itemid;
	elem.data = (const void *)item;

	if (TRX_LOC_QUEUE != item->location)
	{
		item->location = TRX_LOC_QUEUE;
		trx_binary_heap_insert(&config->queues[item->poller_type], &elem);
	}
	else
		trx_binary_heap_update_direct(&config->queues[item->poller_type], &elem);
}

static void	DCupdate_proxy_queue(TRX_DC_PROXY *proxy)
{
	trx_binary_heap_elem_t	elem;

	if (TRX_LOC_POLLER == proxy->location)
		return;

	proxy->nextcheck = proxy->proxy_tasks_nextcheck;
	if (proxy->proxy_data_nextcheck < proxy->nextcheck)
		proxy->nextcheck = proxy->proxy_data_nextcheck;
	if (proxy->proxy_config_nextcheck < proxy->nextcheck)
		proxy->nextcheck = proxy->proxy_config_nextcheck;

	elem.key = proxy->hostid;
	elem.data = (const void *)proxy;

	if (TRX_LOC_QUEUE != proxy->location)
	{
		proxy->location = TRX_LOC_QUEUE;
		trx_binary_heap_insert(&config->pqueue, &elem);
	}
	else
		trx_binary_heap_update_direct(&config->pqueue, &elem);
}

/******************************************************************************
 *                                                                            *
 * Function: config_gmacro_add_index                                          *
 *                                                                            *
 * Purpose: adds global macro index                                           *
 *                                                                            *
 * Parameters: gmacro_index - [IN/OUT] a global macro index hashset           *
 *             gmacro       - [IN] the macro to index                         *
 *                                                                            *
 ******************************************************************************/
static void	config_gmacro_add_index(trx_hashset_t *gmacro_index, TRX_DC_GMACRO *gmacro)
{
	TRX_DC_GMACRO_M	*gmacro_m, gmacro_m_local;

	gmacro_m_local.macro = gmacro->macro;

	if (NULL == (gmacro_m = (TRX_DC_GMACRO_M *)trx_hashset_search(gmacro_index, &gmacro_m_local)))
	{
		gmacro_m_local.macro = trx_strpool_acquire(gmacro->macro);
		trx_vector_ptr_create_ext(&gmacro_m_local.gmacros, __config_mem_malloc_func, __config_mem_realloc_func,
				__config_mem_free_func);

		gmacro_m = (TRX_DC_GMACRO_M *)trx_hashset_insert(gmacro_index, &gmacro_m_local, sizeof(TRX_DC_GMACRO_M));
	}

	trx_vector_ptr_append(&gmacro_m->gmacros, gmacro);
}

/******************************************************************************
 *                                                                            *
 * Function: config_gmacro_remove_index                                       *
 *                                                                            *
 * Purpose: removes global macro index                                        *
 *                                                                            *
 * Parameters: gmacro_index - [IN/OUT] a global macro index hashset           *
 *             gmacro       - [IN] the macro to remove                        *
 *                                                                            *
 ******************************************************************************/
static void	config_gmacro_remove_index(trx_hashset_t *gmacro_index, TRX_DC_GMACRO *gmacro)
{
	TRX_DC_GMACRO_M	*gmacro_m, gmacro_m_local;
	int		index;

	gmacro_m_local.macro = gmacro->macro;

	if (NULL != (gmacro_m = (TRX_DC_GMACRO_M *)trx_hashset_search(gmacro_index, &gmacro_m_local)))
	{
		if (FAIL != (index = trx_vector_ptr_search(&gmacro_m->gmacros, gmacro, TRX_DEFAULT_PTR_COMPARE_FUNC)))
			trx_vector_ptr_remove(&gmacro_m->gmacros, index);

		if (0 == gmacro_m->gmacros.values_num)
		{
			trx_strpool_release(gmacro_m->macro);
			trx_vector_ptr_destroy(&gmacro_m->gmacros);
			trx_hashset_remove(gmacro_index, &gmacro_m_local);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: config_hmacro_add_index                                          *
 *                                                                            *
 * Purpose: adds host macro index                                             *
 *                                                                            *
 * Parameters: hmacro_index - [IN/OUT] a host macro index hashset             *
 *             hmacro       - [IN] the macro to index                         *
 *                                                                            *
 ******************************************************************************/
static void	config_hmacro_add_index(trx_hashset_t *hmacro_index, TRX_DC_HMACRO *hmacro)
{
	TRX_DC_HMACRO_HM	*hmacro_hm, hmacro_hm_local;

	hmacro_hm_local.hostid = hmacro->hostid;
	hmacro_hm_local.macro = hmacro->macro;

	if (NULL == (hmacro_hm = (TRX_DC_HMACRO_HM *)trx_hashset_search(hmacro_index, &hmacro_hm_local)))
	{
		hmacro_hm_local.macro = trx_strpool_acquire(hmacro->macro);
		trx_vector_ptr_create_ext(&hmacro_hm_local.hmacros, __config_mem_malloc_func, __config_mem_realloc_func,
				__config_mem_free_func);

		hmacro_hm = (TRX_DC_HMACRO_HM *)trx_hashset_insert(hmacro_index, &hmacro_hm_local, sizeof(TRX_DC_HMACRO_HM));
	}

	trx_vector_ptr_append(&hmacro_hm->hmacros, hmacro);
}

/******************************************************************************
 *                                                                            *
 * Function: config_hmacro_remove_index                                       *
 *                                                                            *
 * Purpose: removes host macro index                                          *
 *                                                                            *
 * Parameters: hmacro_index - [IN/OUT] a host macro index hashset             *
 *             hmacro       - [IN] the macro name to remove                   *
 *                                                                            *
 ******************************************************************************/
static void	config_hmacro_remove_index(trx_hashset_t *hmacro_index, TRX_DC_HMACRO *hmacro)
{
	TRX_DC_HMACRO_HM	*hmacro_hm, hmacro_hm_local;
	int			index;

	hmacro_hm_local.hostid = hmacro->hostid;
	hmacro_hm_local.macro = hmacro->macro;

	if (NULL != (hmacro_hm = (TRX_DC_HMACRO_HM *)trx_hashset_search(hmacro_index, &hmacro_hm_local)))
	{
		if (FAIL != (index = trx_vector_ptr_search(&hmacro_hm->hmacros, hmacro, TRX_DEFAULT_PTR_COMPARE_FUNC)))
			trx_vector_ptr_remove(&hmacro_hm->hmacros, index);

		if (0 == hmacro_hm->hmacros.values_num)
		{
			trx_strpool_release(hmacro_hm->macro);
			trx_vector_ptr_destroy(&hmacro_hm->hmacros);
			trx_hashset_remove(hmacro_index, &hmacro_hm_local);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: set_hk_opt                                                       *
 *                                                                            *
 * Purpose: sets and validates global housekeeping option                     *
 *                                                                            *
 * Parameters: value     - [OUT] housekeeping setting                         *
 *             non_zero  - [IN] 0 if value is allowed to be zero, 1 otherwise *
 *             value_min - [IN] minimal acceptable setting value              *
 *             value_raw - [IN] setting value to validate                     *
 *                                                                            *
 ******************************************************************************/
static int	set_hk_opt(int *value, int non_zero, int value_min, const char *value_raw)
{
	if (SUCCEED != is_time_suffix(value_raw, value, TRX_LENGTH_UNLIMITED))
		return FAIL;

	if (0 != non_zero && 0 == *value)
		return FAIL;

	if (0 != *value && (value_min > *value || TRX_HK_PERIOD_MAX < *value))
		return FAIL;

	return SUCCEED;
}

static int	DCsync_config(trx_dbsync_t *sync, int *flags)
{
	const TRX_TABLE	*config_table;

	const char	*selected_fields[] = {"refresh_unsupported", "discovery_groupid", "snmptrap_logging",
					"severity_name_0", "severity_name_1", "severity_name_2", "severity_name_3",
					"severity_name_4", "severity_name_5", "hk_events_mode", "hk_events_trigger",
					"hk_events_internal", "hk_events_discovery", "hk_events_autoreg",
					"hk_services_mode", "hk_services", "hk_audit_mode", "hk_audit",
					"hk_sessions_mode", "hk_sessions", "hk_history_mode", "hk_history_global",
					"hk_history", "hk_trends_mode", "hk_trends_global", "hk_trends",
					"default_inventory_mode", "db_extension", "autoreg_tls_accept"};	/* sync with trx_dbsync_compare_config() */
	const char	*row[ARRSIZE(selected_fields)];
	size_t		i;
	int		j, found = 1, refresh_unsupported, ret;
	char		**db_row;
	trx_uint64_t	rowid;
	unsigned char	tag;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	*flags = 0;

	if (NULL == config->config)
	{
		found = 0;
		config->config = (TRX_DC_CONFIG_TABLE *)__config_mem_malloc_func(NULL, sizeof(TRX_DC_CONFIG_TABLE));
	}

	if (SUCCEED != (ret = trx_dbsync_next(sync, &rowid, &db_row, &tag)))
	{
		/* load default config data */

		if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
			treegix_log(LOG_LEVEL_ERR, "no records in table 'config'");

		config_table = DBget_table("config");

		for (i = 0; i < ARRSIZE(selected_fields); i++)
			row[i] = DBget_field(config_table, selected_fields[i])->default_value;
	}
	else
	{
		for (i = 0; i < ARRSIZE(selected_fields); i++)
			row[i] = db_row[i];
	}

	/* store the config data */

	if (SUCCEED != is_time_suffix(row[0], &refresh_unsupported, TRX_LENGTH_UNLIMITED))
	{
		treegix_log(LOG_LEVEL_WARNING, "invalid unsupported item refresh interval, restoring default");

		config_table = DBget_table("config");

		if (SUCCEED != is_time_suffix(DBget_field(config_table, "refresh_unsupported")->default_value,
				&refresh_unsupported, TRX_LENGTH_UNLIMITED))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			refresh_unsupported = 0;
		}
	}

	if (0 == found || config->config->refresh_unsupported != refresh_unsupported)
		*flags |= TRX_REFRESH_UNSUPPORTED_CHANGED;

	config->config->refresh_unsupported = refresh_unsupported;

	if (NULL != row[1])
		TRX_STR2UINT64(config->config->discovery_groupid, row[1]);
	else
		config->config->discovery_groupid = TRX_DISCOVERY_GROUPID_UNDEFINED;

	config->config->snmptrap_logging = (unsigned char)atoi(row[2]);
	config->config->default_inventory_mode = atoi(row[26]);
	DCstrpool_replace(found, &config->config->db_extension, row[27]);
	config->config->autoreg_tls_accept = (unsigned char)atoi(row[28]);

	for (j = 0; TRIGGER_SEVERITY_COUNT > j; j++)
		DCstrpool_replace(found, &config->config->severity_name[j], row[3 + j]);

#if TRIGGER_SEVERITY_COUNT != 6
#	error "row indexes below are based on assumption of six trigger severity levels"
#endif

	/* read housekeeper configuration */

	if (TRX_HK_OPTION_ENABLED == (config->config->hk.events_mode = atoi(row[9])) &&
			(SUCCEED != set_hk_opt(&config->config->hk.events_trigger, 1, SEC_PER_DAY, row[10]) ||
			SUCCEED != set_hk_opt(&config->config->hk.events_internal, 1, SEC_PER_DAY, row[11]) ||
			SUCCEED != set_hk_opt(&config->config->hk.events_discovery, 1, SEC_PER_DAY, row[12]) ||
			SUCCEED != set_hk_opt(&config->config->hk.events_autoreg, 1, SEC_PER_DAY, row[13])))
	{
		treegix_log(LOG_LEVEL_WARNING, "trigger, internal, network discovery and auto-registration data"
				" housekeeping will be disabled due to invalid settings");
		config->config->hk.events_mode = TRX_HK_OPTION_DISABLED;
	}

	if (TRX_HK_OPTION_ENABLED == (config->config->hk.services_mode = atoi(row[14])) &&
			SUCCEED != set_hk_opt(&config->config->hk.services, 1, SEC_PER_DAY, row[15]))
	{
		treegix_log(LOG_LEVEL_WARNING, "IT services data housekeeping will be disabled due to invalid"
				" settings");
		config->config->hk.services_mode = TRX_HK_OPTION_DISABLED;
	}

	if (TRX_HK_OPTION_ENABLED == (config->config->hk.audit_mode = atoi(row[16])) &&
			SUCCEED != set_hk_opt(&config->config->hk.audit, 1, SEC_PER_DAY, row[17]))
	{
		treegix_log(LOG_LEVEL_WARNING, "audit data housekeeping will be disabled due to invalid"
				" settings");
		config->config->hk.audit_mode = TRX_HK_OPTION_DISABLED;
	}

	if (TRX_HK_OPTION_ENABLED == (config->config->hk.sessions_mode = atoi(row[18])) &&
			SUCCEED != set_hk_opt(&config->config->hk.sessions, 1, SEC_PER_DAY, row[19]))
	{
		treegix_log(LOG_LEVEL_WARNING, "user sessions data housekeeping will be disabled due to invalid"
				" settings");
		config->config->hk.sessions_mode = TRX_HK_OPTION_DISABLED;
	}

	config->config->hk.history_mode = atoi(row[20]);
	if (TRX_HK_OPTION_ENABLED == (config->config->hk.history_global = atoi(row[21])) &&
			SUCCEED != set_hk_opt(&config->config->hk.history, 0, TRX_HK_HISTORY_MIN, row[22]))
	{
		treegix_log(LOG_LEVEL_WARNING, "history data housekeeping will be disabled and all items will"
				" store their history due to invalid global override settings");
		config->config->hk.history_mode = TRX_HK_MODE_DISABLED;
		config->config->hk.history = 1;	/* just enough to make 0 == items[i].history condition fail */
	}

#ifdef HAVE_POSTGRESQL
	if (TRX_HK_MODE_DISABLED != config->config->hk.history_mode &&
			TRX_HK_OPTION_ENABLED == config->config->hk.history_global &&
			0 == trx_strcmp_null(config->config->db_extension, TRX_CONFIG_DB_EXTENSION_TIMESCALE))
	{
		config->config->hk.history_mode = TRX_HK_MODE_PARTITION;
	}
#endif

	config->config->hk.trends_mode = atoi(row[23]);
	if (TRX_HK_OPTION_ENABLED == (config->config->hk.trends_global = atoi(row[24])) &&
			SUCCEED != set_hk_opt(&config->config->hk.trends, 0, TRX_HK_TRENDS_MIN, row[25]))
	{
		treegix_log(LOG_LEVEL_WARNING, "trends data housekeeping will be disabled and all numeric items"
				" will store their history due to invalid global override settings");
		config->config->hk.trends_mode = TRX_HK_MODE_DISABLED;
		config->config->hk.trends = 1;	/* just enough to make 0 == items[i].trends condition fail */
	}

#ifdef HAVE_POSTGRESQL
	if (TRX_HK_MODE_DISABLED != config->config->hk.trends_mode &&
			TRX_HK_OPTION_ENABLED == config->config->hk.trends_global &&
			0 == trx_strcmp_null(config->config->db_extension, TRX_CONFIG_DB_EXTENSION_TIMESCALE))
	{
		config->config->hk.trends_mode = TRX_HK_MODE_PARTITION;
	}
#endif

	if (SUCCEED == ret && SUCCEED == trx_dbsync_next(sync, &rowid, &db_row, &tag))	/* table must have */
		treegix_log(LOG_LEVEL_ERR, "table 'config' has multiple records");	/* only one record */

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return SUCCEED;
}

static void	DCsync_autoreg_config(trx_dbsync_t *sync)
{
	/* sync this function with trx_dbsync_compare_autoreg_psk() */
	int		ret;
	char		**db_row;
	trx_uint64_t	rowid;
	unsigned char	tag;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &db_row, &tag)))
	{
		switch (tag)
		{
			case TRX_DBSYNC_ROW_ADD:
			case TRX_DBSYNC_ROW_UPDATE:
				trx_strlcpy(config->autoreg_psk_identity, db_row[0],
						sizeof(config->autoreg_psk_identity));
				trx_strlcpy(config->autoreg_psk, db_row[1], sizeof(config->autoreg_psk));
				break;
			case TRX_DBSYNC_ROW_REMOVE:
				config->autoreg_psk_identity[0] = '\0';
				trx_guaranteed_memset(config->autoreg_psk, 0, sizeof(config->autoreg_psk));
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_hosts(trx_dbsync_t *sync)
{
	char		**row;
	trx_uint64_t	rowid;
	unsigned char	tag;

	TRX_DC_HOST	*host;
	TRX_DC_IPMIHOST	*ipmihost;
	TRX_DC_PROXY	*proxy;
	TRX_DC_HOST_H	*host_h, host_h_local, *host_p, host_p_local;

	int		found;
	int		update_index_h, update_index_p, ret;
	trx_uint64_t	hostid, proxy_hostid;
	unsigned char	status;
	time_t		now;
	signed char	ipmi_authtype;
	unsigned char	ipmi_privilege;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	TRX_DC_PSK	*psk_i, psk_i_local;
	trx_ptr_pair_t	*psk_owner, psk_owner_local;
	trx_hashset_t	psk_owners;
#endif
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_hashset_create(&psk_owners, 0, TRX_DEFAULT_PTR_HASH_FUNC, TRX_DEFAULT_PTR_COMPARE_FUNC);
#endif
	now = time(NULL);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(hostid, row[0]);
		TRX_DBROW2UINT64(proxy_hostid, row[1]);
		TRX_STR2UCHAR(status, row[22]);

		host = (TRX_DC_HOST *)DCfind_id(&config->hosts, hostid, sizeof(TRX_DC_HOST), &found);

		/* see whether we should and can update 'hosts_h' and 'hosts_p' indexes at this point */

		update_index_h = 0;
		update_index_p = 0;

		if ((HOST_STATUS_MONITORED == status || HOST_STATUS_NOT_MONITORED == status) &&
				(0 == found || 0 != strcmp(host->host, row[2])))
		{
			if (1 == found)
			{
				host_h_local.host = host->host;
				host_h = (TRX_DC_HOST_H *)trx_hashset_search(&config->hosts_h, &host_h_local);

				if (NULL != host_h && host == host_h->host_ptr)	/* see TRX-4045 for NULL check */
				{
					trx_strpool_release(host_h->host);
					trx_hashset_remove_direct(&config->hosts_h, host_h);
				}
			}

			host_h_local.host = row[2];
			host_h = (TRX_DC_HOST_H *)trx_hashset_search(&config->hosts_h, &host_h_local);

			if (NULL != host_h)
				host_h->host_ptr = host;
			else
				update_index_h = 1;
		}
		else if ((HOST_STATUS_PROXY_ACTIVE == status || HOST_STATUS_PROXY_PASSIVE == status) &&
				(0 == found || 0 != strcmp(host->host, row[2])))
		{
			if (1 == found)
			{
				host_p_local.host = host->host;
				host_p = (TRX_DC_HOST_H *)trx_hashset_search(&config->hosts_p, &host_p_local);

				if (NULL != host_p && host == host_p->host_ptr)
				{
					trx_strpool_release(host_p->host);
					trx_hashset_remove_direct(&config->hosts_p, host_p);
				}
			}

			host_p_local.host = row[2];
			host_p = (TRX_DC_HOST_H *)trx_hashset_search(&config->hosts_p, &host_p_local);

			if (NULL != host_p)
				host_p->host_ptr = host;
			else
				update_index_p = 1;
		}

		/* store new information in host structure */

		DCstrpool_replace(found, &host->host, row[2]);
		DCstrpool_replace(found, &host->name, row[23]);
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		DCstrpool_replace(found, &host->tls_issuer, row[31]);
		DCstrpool_replace(found, &host->tls_subject, row[32]);

		/* maintain 'config->psks' in configuration cache */

		/*****************************************************************************/
		/*                                                                           */
		/* cases to cover (PSKid means PSK identity):                                */
		/*                                                                           */
		/*                                  Incoming data record                     */
		/*                                  /                   \                    */
		/*                                new                   new                  */
		/*                               PSKid                 PSKid                 */
		/*                             non-empty               empty                 */
		/*                             /      \                /    \                */
		/*                            /        \              /      \               */
		/*                       'host'        'host'      'host'    'host'          */
		/*                       record        record      record    record          */
		/*                        has           has         has       has            */
		/*                     non-empty       empty     non-empty  empty PSK        */
		/*                        PSK           PSK         PSK      |     \         */
		/*                       /   \           |           |       |      \        */
		/*                      /     \          |           |       |       \       */
		/*                     /       \         |           |       |        \      */
		/*            new PSKid       new PSKid  |           |   existing     new    */
		/*             same as         differs   |           |    record     record  */
		/*            old PSKid         from     |           |      |          |     */
		/*           /    |           old PSKid  |           |     done        |     */
		/*          /     |              |       |           |                 |     */
		/*   new PSK    new PSK        delete    |        delete               |     */
		/*    value      value        old PSKid  |       old PSKid             |     */
		/*   same as    differs       and value  |       and value             |     */
		/*     old       from         from psks  |       from psks             |     */
		/*      |        old          hashset    |        hashset              |     */
		/*     done       /           (if ref    |        (if ref              |     */
		/*               /            count=0)   |        count=0)             |     */
		/*              /              /     \  /|           \                /      */
		/*             /              /--------- |            \              /       */
		/*            /              /         \ |             \            /        */
		/*       delete          new PSKid   new PSKid         set pointer in        */
		/*       old PSK          already     not in           'hosts' record        */
		/*        value           in psks      psks             to NULL PSK          */
		/*        from            hashset     hashset                |               */
		/*       string            /   \          \                 done             */
		/*        pool            /     \          \                                 */
		/*         |             /       \          \                                */
		/*       change    PSK value   PSK value    insert                           */
		/*      PSK value  in hashset  in hashset  new PSKid                         */
		/*      for this    same as     differs    and value                         */
		/*       PSKid      new PSK     from new   into psks                         */
		/*         |        value      PSK value    hashset                          */
		/*        done        \           |            /                             */
		/*                     \       replace        /                              */
		/*                      \      PSK value     /                               */
		/*                       \     in hashset   /                                */
		/*                        \    with new    /                                 */
		/*                         \   PSK value  /                                  */
		/*                          \     |      /                                   */
		/*                           \    |     /                                    */
		/*                            set pointer                                    */
		/*                            in 'host'                                      */
		/*                            record to                                      */
		/*                            new PSKid                                      */
		/*                                |                                          */
		/*                               done                                        */
		/*                                                                           */
		/*****************************************************************************/

		psk_owner = NULL;

		if ('\0' == *row[33] || '\0' == *row[34])	/* new PSKid or value empty */
		{
			/* In case of "impossible" errors ("PSK value without identity" or "PSK identity without */
			/* value") assume empty PSK identity and value. These errors should have been prevented */
			/* by validation in frontend/API. Be prepared when making a connection requiring PSK - */
			/* the PSK might not be available. */

			if (1 == found)
			{
				if (NULL == host->tls_dc_psk)	/* 'host' record has empty PSK */
					goto done;

				/* 'host' record has non-empty PSK. Unlink and delete PSK. */

				psk_i_local.tls_psk_identity = host->tls_dc_psk->tls_psk_identity;

				if (NULL != (psk_i = (TRX_DC_PSK *)trx_hashset_search(&config->psks, &psk_i_local)) &&
						0 == --(psk_i->refcount))
				{
					trx_strpool_release(psk_i->tls_psk_identity);
					trx_strpool_release(psk_i->tls_psk);
					trx_hashset_remove_direct(&config->psks, psk_i);
				}
			}

			host->tls_dc_psk = NULL;
			goto done;
		}

		/* new PSKid and value non-empty */

		trx_strlower(row[34]);

		if (1 == found && NULL != host->tls_dc_psk)	/* 'host' record has non-empty PSK */
		{
			if (0 == strcmp(host->tls_dc_psk->tls_psk_identity, row[33]))	/* new PSKid same as */
											/* old PSKid */
			{
				if (0 != strcmp(host->tls_dc_psk->tls_psk, row[34]))	/* new PSK value */
											/* differs from old */
				{
					if (NULL == (psk_owner = (trx_ptr_pair_t *)trx_hashset_search(&psk_owners,
							&host->tls_dc_psk->tls_psk_identity)))
					{
						/* change underlying PSK value and 'config->psks' is updated, too */
						DCstrpool_replace(1, &host->tls_dc_psk->tls_psk, row[34]);
					}
					else
					{
						treegix_log(LOG_LEVEL_WARNING, "conflicting PSK values for PSK identity"
								" \"%s\" on hosts \"%s\" and \"%s\" (and maybe others)",
								(char *)psk_owner->first, (char *)psk_owner->second,
								host->host);
					}
				}

				goto done;
			}

			/* New PSKid differs from old PSKid. Unlink and delete old PSK. */

			psk_i_local.tls_psk_identity = host->tls_dc_psk->tls_psk_identity;

			if (NULL != (psk_i = (TRX_DC_PSK *)trx_hashset_search(&config->psks, &psk_i_local)) &&
					0 == --(psk_i->refcount))
			{
				trx_strpool_release(psk_i->tls_psk_identity);
				trx_strpool_release(psk_i->tls_psk);
				trx_hashset_remove_direct(&config->psks, psk_i);
			}

			host->tls_dc_psk = NULL;
		}

		/* new PSK identity already stored? */

		psk_i_local.tls_psk_identity = row[33];

		if (NULL != (psk_i = (TRX_DC_PSK *)trx_hashset_search(&config->psks, &psk_i_local)))
		{
			/* new PSKid already in psks hashset */

			if (0 != strcmp(psk_i->tls_psk, row[34]))	/* PSKid stored but PSK value is different */
			{
				if (NULL == (psk_owner = (trx_ptr_pair_t *)trx_hashset_search(&psk_owners, &psk_i->tls_psk_identity)))
				{
					DCstrpool_replace(1, &psk_i->tls_psk, row[34]);
				}
				else
				{
					treegix_log(LOG_LEVEL_WARNING, "conflicting PSK values for PSK identity"
							" \"%s\" on hosts \"%s\" and \"%s\" (and maybe others)",
							(char *)psk_owner->first, (char *)psk_owner->second,
							host->host);
				}
			}

			host->tls_dc_psk = psk_i;
			psk_i->refcount++;
			goto done;
		}

		/* insert new PSKid and value into psks hashset */

		DCstrpool_replace(0, &psk_i_local.tls_psk_identity, row[33]);
		DCstrpool_replace(0, &psk_i_local.tls_psk, row[34]);
		psk_i_local.refcount = 1;
		host->tls_dc_psk = trx_hashset_insert(&config->psks, &psk_i_local, sizeof(TRX_DC_PSK));
done:
		if (NULL != host->tls_dc_psk && NULL == psk_owner)
		{
			if (NULL == (psk_owner = (trx_ptr_pair_t *)trx_hashset_search(&psk_owners, &host->tls_dc_psk->tls_psk_identity)))
			{
				/* register this host as the PSK identity owner, against which to report conflicts */

				psk_owner_local.first = (char *)host->tls_dc_psk->tls_psk_identity;
				psk_owner_local.second = (char *)host->host;

				trx_hashset_insert(&psk_owners, &psk_owner_local, sizeof(psk_owner_local));
			}
		}
#endif
		TRX_STR2UCHAR(host->tls_connect, row[29]);
		TRX_STR2UCHAR(host->tls_accept, row[30]);

		if (0 == found)
		{
			TRX_DBROW2UINT64(host->maintenanceid, row[33 + TRX_HOST_TLS_OFFSET]);
			host->maintenance_status = (unsigned char)atoi(row[7]);
			host->maintenance_type = (unsigned char)atoi(row[8]);
			host->maintenance_from = atoi(row[9]);
			host->data_expected_from = now;
			host->update_items = 0;

			host->errors_from = atoi(row[10]);
			host->available = (unsigned char)atoi(row[11]);
			host->disable_until = atoi(row[12]);
			host->snmp_errors_from = atoi(row[13]);
			host->snmp_available = (unsigned char)atoi(row[14]);
			host->snmp_disable_until = atoi(row[15]);
			host->ipmi_errors_from = atoi(row[16]);
			host->ipmi_available = (unsigned char)atoi(row[17]);
			host->ipmi_disable_until = atoi(row[18]);
			host->jmx_errors_from = atoi(row[19]);
			host->jmx_available = (unsigned char)atoi(row[20]);
			host->jmx_disable_until = atoi(row[21]);
			host->availability_ts = now;

			DCstrpool_replace(0, &host->error, row[25]);
			DCstrpool_replace(0, &host->snmp_error, row[26]);
			DCstrpool_replace(0, &host->ipmi_error, row[27]);
			DCstrpool_replace(0, &host->jmx_error, row[28]);

			host->items_num = 0;
			host->snmp_items_num = 0;
			host->ipmi_items_num = 0;
			host->jmx_items_num = 0;

			host->reset_availability = 0;

			trx_vector_ptr_create_ext(&host->interfaces_v, __config_mem_malloc_func,
					__config_mem_realloc_func, __config_mem_free_func);
		}
		else
		{
			if (HOST_STATUS_MONITORED == status && HOST_STATUS_MONITORED != host->status)
				host->data_expected_from = now;

			/* reset host status if host status has been changed (e.g., if host has been disabled) */
			if (status != host->status)
				host->reset_availability = 1;

			/* reset host status if host proxy assignment has been changed */
			if (proxy_hostid != host->proxy_hostid)
				host->reset_availability = 1;
		}

		host->proxy_hostid = proxy_hostid;

		/* update 'hosts_h' and 'hosts_p' indexes using new data, if not done already */

		if (1 == update_index_h)
		{
			host_h_local.host = trx_strpool_acquire(host->host);
			host_h_local.host_ptr = host;
			trx_hashset_insert(&config->hosts_h, &host_h_local, sizeof(TRX_DC_HOST_H));
		}

		if (1 == update_index_p)
		{
			host_p_local.host = trx_strpool_acquire(host->host);
			host_p_local.host_ptr = host;
			trx_hashset_insert(&config->hosts_p, &host_p_local, sizeof(TRX_DC_HOST_H));
		}

		/* IPMI hosts */

		ipmi_authtype = (signed char)atoi(row[3]);
		ipmi_privilege = (unsigned char)atoi(row[4]);

		if (TRX_IPMI_DEFAULT_AUTHTYPE != ipmi_authtype || TRX_IPMI_DEFAULT_PRIVILEGE != ipmi_privilege ||
				'\0' != *row[5] || '\0' != *row[6])	/* useipmi */
		{
			ipmihost = (TRX_DC_IPMIHOST *)DCfind_id(&config->ipmihosts, hostid, sizeof(TRX_DC_IPMIHOST), &found);

			ipmihost->ipmi_authtype = ipmi_authtype;
			ipmihost->ipmi_privilege = ipmi_privilege;
			DCstrpool_replace(found, &ipmihost->ipmi_username, row[5]);
			DCstrpool_replace(found, &ipmihost->ipmi_password, row[6]);
		}
		else if (NULL != (ipmihost = (TRX_DC_IPMIHOST *)trx_hashset_search(&config->ipmihosts, &hostid)))
		{
			/* remove IPMI connection parameters for hosts without IPMI */

			trx_strpool_release(ipmihost->ipmi_username);
			trx_strpool_release(ipmihost->ipmi_password);

			trx_hashset_remove_direct(&config->ipmihosts, ipmihost);
		}

		/* proxies */

		if (HOST_STATUS_PROXY_ACTIVE == status || HOST_STATUS_PROXY_PASSIVE == status)
		{
			proxy = (TRX_DC_PROXY *)DCfind_id(&config->proxies, hostid, sizeof(TRX_DC_PROXY), &found);

			if (0 == found)
			{
				proxy->location = TRX_LOC_NOWHERE;
				proxy->version = 0;
				proxy->lastaccess = atoi(row[24]);
				proxy->last_cfg_error_time = 0;
			}

			proxy->auto_compress = atoi(row[32 + TRX_HOST_TLS_OFFSET]);
			DCstrpool_replace(found, &proxy->proxy_address, row[31 + TRX_HOST_TLS_OFFSET]);

			if (HOST_STATUS_PROXY_PASSIVE == status && (0 == found || status != host->status))
			{
				proxy->proxy_config_nextcheck = (int)calculate_proxy_nextcheck(
						hostid, CONFIG_PROXYCONFIG_FREQUENCY, now);
				proxy->proxy_data_nextcheck = (int)calculate_proxy_nextcheck(
						hostid, CONFIG_PROXYDATA_FREQUENCY, now);
				proxy->proxy_tasks_nextcheck = (int)calculate_proxy_nextcheck(
						hostid, TRX_TASK_UPDATE_FREQUENCY, now);

				DCupdate_proxy_queue(proxy);
			}
			else if (HOST_STATUS_PROXY_ACTIVE == status && TRX_LOC_QUEUE == proxy->location)
			{
				trx_binary_heap_remove_direct(&config->pqueue, proxy->hostid);
				proxy->location = TRX_LOC_NOWHERE;
			}
			proxy->last_version_error_time = time(NULL);
		}
		else if (NULL != (proxy = (TRX_DC_PROXY *)trx_hashset_search(&config->proxies, &hostid)))
		{
			if (TRX_LOC_QUEUE == proxy->location)
			{
				trx_binary_heap_remove_direct(&config->pqueue, proxy->hostid);
				proxy->location = TRX_LOC_NOWHERE;
			}

			trx_strpool_release(proxy->proxy_address);
			trx_hashset_remove_direct(&config->proxies, proxy);
		}

		host->status = status;
	}

	/* remove deleted hosts from buffer */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &rowid)))
			continue;

		hostid = host->hostid;

		/* IPMI hosts */

		if (NULL != (ipmihost = (TRX_DC_IPMIHOST *)trx_hashset_search(&config->ipmihosts, &hostid)))
		{
			trx_strpool_release(ipmihost->ipmi_username);
			trx_strpool_release(ipmihost->ipmi_password);

			trx_hashset_remove_direct(&config->ipmihosts, ipmihost);
		}

		/* proxies */

		if (NULL != (proxy = (TRX_DC_PROXY *)trx_hashset_search(&config->proxies, &hostid)))
		{
			if (TRX_LOC_QUEUE == proxy->location)
			{
				trx_binary_heap_remove_direct(&config->pqueue, proxy->hostid);
				proxy->location = TRX_LOC_NOWHERE;
			}

			trx_strpool_release(proxy->proxy_address);
			trx_hashset_remove_direct(&config->proxies, proxy);
		}

		/* hosts */

		if (HOST_STATUS_MONITORED == host->status || HOST_STATUS_NOT_MONITORED == host->status)
		{
			host_h_local.host = host->host;
			host_h = (TRX_DC_HOST_H *)trx_hashset_search(&config->hosts_h, &host_h_local);

			if (NULL != host_h && host == host_h->host_ptr)	/* see TRX-4045 for NULL check */
			{
				trx_strpool_release(host_h->host);
				trx_hashset_remove_direct(&config->hosts_h, host_h);
			}
		}
		else if (HOST_STATUS_PROXY_ACTIVE == host->status || HOST_STATUS_PROXY_PASSIVE == host->status)
		{
			host_p_local.host = host->host;
			host_p = (TRX_DC_HOST_H *)trx_hashset_search(&config->hosts_p, &host_p_local);

			if (NULL != host_p && host == host_p->host_ptr)
			{
				trx_strpool_release(host_p->host);
				trx_hashset_remove_direct(&config->hosts_p, host_p);
			}
		}

		trx_strpool_release(host->host);
		trx_strpool_release(host->name);

		trx_strpool_release(host->error);
		trx_strpool_release(host->snmp_error);
		trx_strpool_release(host->ipmi_error);
		trx_strpool_release(host->jmx_error);
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		trx_strpool_release(host->tls_issuer);
		trx_strpool_release(host->tls_subject);

		/* Maintain 'psks' index. Unlink and delete the PSK identity. */
		if (NULL != host->tls_dc_psk)
		{
			psk_i_local.tls_psk_identity = host->tls_dc_psk->tls_psk_identity;

			if (NULL != (psk_i = (TRX_DC_PSK *)trx_hashset_search(&config->psks, &psk_i_local)) &&
					0 == --(psk_i->refcount))
			{
				trx_strpool_release(psk_i->tls_psk_identity);
				trx_strpool_release(psk_i->tls_psk);
				trx_hashset_remove_direct(&config->psks, psk_i);
			}
		}
#endif
		trx_vector_ptr_destroy(&host->interfaces_v);
		trx_hashset_remove_direct(&config->hosts, host);
	}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_hashset_destroy(&psk_owners);
#endif

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_host_inventory(trx_dbsync_t *sync)
{
	TRX_DC_HOST_INVENTORY	*host_inventory, *host_inventory_auto;
	trx_uint64_t		rowid, hostid;
	int			found, ret, i;
	char			**row;
	unsigned char		tag;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(hostid, row[0]);

		host_inventory = (TRX_DC_HOST_INVENTORY *)DCfind_id(&config->host_inventories, hostid, sizeof(TRX_DC_HOST_INVENTORY), &found);

		TRX_STR2UCHAR(host_inventory->inventory_mode, row[1]);

		/* store new information in host_inventory structure */
		for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
			DCstrpool_replace(found, &(host_inventory->values[i]), row[i + 2]);

		host_inventory_auto = (TRX_DC_HOST_INVENTORY *)DCfind_id(&config->host_inventories_auto, hostid, sizeof(TRX_DC_HOST_INVENTORY),
				&found);

		host_inventory_auto->inventory_mode = host_inventory->inventory_mode;

		if (1 == found)
		{
			for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
			{
				if (NULL == host_inventory_auto->values[i])
					continue;

				trx_strpool_release(host_inventory_auto->values[i]);
				host_inventory_auto->values[i] = NULL;
			}
		}
		else
		{
			for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
				host_inventory_auto->values[i] = NULL;
		}
	}

	/* remove deleted host inventory from cache */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (host_inventory = (TRX_DC_HOST_INVENTORY *)trx_hashset_search(&config->host_inventories, &rowid)))
			continue;

		for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
			trx_strpool_release(host_inventory->values[i]);

		trx_hashset_remove_direct(&config->host_inventories, host_inventory);

		if (NULL == (host_inventory_auto = (TRX_DC_HOST_INVENTORY *)trx_hashset_search(&config->host_inventories_auto, &rowid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
		{
			if (NULL != host_inventory_auto->values[i])
				trx_strpool_release(host_inventory_auto->values[i]);
		}

		trx_hashset_remove_direct(&config->host_inventories_auto, host_inventory_auto);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_htmpls(trx_dbsync_t *sync)
{
	char			**row;
	trx_uint64_t		rowid;
	unsigned char		tag;

	TRX_DC_HTMPL		*htmpl = NULL;

	int			found, i, index, ret;
	trx_uint64_t		_hostid = 0, hostid, templateid;
	trx_vector_ptr_t	sort;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&sort);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(hostid, row[0]);
		TRX_STR2UINT64(templateid, row[1]);

		if (_hostid != hostid || 0 == _hostid)
		{
			_hostid = hostid;

			htmpl = (TRX_DC_HTMPL *)DCfind_id(&config->htmpls, hostid, sizeof(TRX_DC_HTMPL), &found);

			if (0 == found)
			{
				trx_vector_uint64_create_ext(&htmpl->templateids,
						__config_mem_malloc_func,
						__config_mem_realloc_func,
						__config_mem_free_func);
				trx_vector_uint64_reserve(&htmpl->templateids, 1);
			}

			trx_vector_ptr_append(&sort, htmpl);
		}

		trx_vector_uint64_append(&htmpl->templateids, templateid);
	}

	/* remove deleted host templates from cache */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		TRX_STR2UINT64(hostid, row[0]);

		if (NULL == (htmpl = (TRX_DC_HTMPL *)trx_hashset_search(&config->htmpls, &hostid)))
			continue;

		TRX_STR2UINT64(templateid, row[1]);

		if (-1 == (index = trx_vector_uint64_search(&htmpl->templateids, templateid,
				TRX_DEFAULT_UINT64_COMPARE_FUNC)))
		{
			continue;
		}

		if (1 == htmpl->templateids.values_num)
		{
			trx_vector_uint64_destroy(&htmpl->templateids);
			trx_hashset_remove_direct(&config->htmpls, htmpl);
		}
		else
		{
			trx_vector_uint64_remove_noorder(&htmpl->templateids, index);
			trx_vector_ptr_append(&sort, htmpl);
		}
	}

	/* sort the changed template lists */

	trx_vector_ptr_sort(&sort, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	trx_vector_ptr_uniq(&sort, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < sort.values_num; i++)
	{
		htmpl = (TRX_DC_HTMPL *)sort.values[i];
		trx_vector_uint64_sort(&htmpl->templateids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	}

	trx_vector_ptr_destroy(&sort);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_gmacros(trx_dbsync_t *sync)
{
	char		**row;
	trx_uint64_t	rowid;
	unsigned char	tag;

	TRX_DC_GMACRO	*gmacro;

	int		found, context_existed, update_index, ret;
	trx_uint64_t	globalmacroid;
	char		*macro = NULL, *context = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(globalmacroid, row[0]);

		if (SUCCEED != trx_user_macro_parse_dyn(row[1], &macro, &context, NULL))
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot parse user macro \"%s\"", row[1]);
			continue;
		}

		gmacro = (TRX_DC_GMACRO *)DCfind_id(&config->gmacros, globalmacroid, sizeof(TRX_DC_GMACRO), &found);

		/* see whether we should and can update gmacros_m index at this point */
		update_index = 0;

		if (0 == found || 0 != strcmp(gmacro->macro, macro) || 0 != trx_strcmp_null(gmacro->context, context))
		{
			if (1 == found)
				config_gmacro_remove_index(&config->gmacros_m, gmacro);

			update_index = 1;
		}

		/* store new information in macro structure */
		DCstrpool_replace(found, &gmacro->macro, macro);
		DCstrpool_replace(found, &gmacro->value, row[2]);

		context_existed = (1 == found && NULL != gmacro->context);

		if (NULL == context)
		{
			/* release the context if it was removed from the macro */
			if (1 == context_existed)
				trx_strpool_release(gmacro->context);

			gmacro->context = NULL;
		}
		else
		{
			/* replace the existing context (1) or add context to macro (0) */
			DCstrpool_replace(context_existed, &gmacro->context, context);
		}

		/* update gmacros_m index using new data */
		if (1 == update_index)
			config_gmacro_add_index(&config->gmacros_m, gmacro);
	}

	/* remove deleted globalmacros from buffer */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (gmacro = (TRX_DC_GMACRO *)trx_hashset_search(&config->gmacros, &rowid)))
			continue;

		config_gmacro_remove_index(&config->gmacros_m, gmacro);

		trx_strpool_release(gmacro->macro);
		trx_strpool_release(gmacro->value);

		if (NULL != gmacro->context)
			trx_strpool_release(gmacro->context);

		trx_hashset_remove_direct(&config->gmacros, gmacro);
	}

	trx_free(context);
	trx_free(macro);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_hmacros(trx_dbsync_t *sync)
{
	char		**row;
	trx_uint64_t	rowid;
	unsigned char	tag;

	TRX_DC_HMACRO	*hmacro;

	int		found, context_existed, update_index, ret;
	trx_uint64_t	hostmacroid, hostid;
	char		*macro = NULL, *context = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(hostmacroid, row[0]);
		TRX_STR2UINT64(hostid, row[1]);

		if (SUCCEED != trx_user_macro_parse_dyn(row[2], &macro, &context, NULL))
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot parse host \"%s\" macro \"%s\"", row[1], row[2]);
			continue;
		}

		hmacro = (TRX_DC_HMACRO *)DCfind_id(&config->hmacros, hostmacroid, sizeof(TRX_DC_HMACRO), &found);

		/* see whether we should and can update hmacros_hm index at this point */
		update_index = 0;

		if (0 == found || hmacro->hostid != hostid || 0 != strcmp(hmacro->macro, macro) ||
				0 != trx_strcmp_null(hmacro->context, context))
		{
			if (1 == found)
				config_hmacro_remove_index(&config->hmacros_hm, hmacro);

			update_index = 1;
		}

		/* store new information in macro structure */
		hmacro->hostid = hostid;
		DCstrpool_replace(found, &hmacro->macro, macro);
		DCstrpool_replace(found, &hmacro->value, row[3]);

		context_existed = (1 == found && NULL != hmacro->context);

		if (NULL == context)
		{
			/* release the context if it was removed from the macro */
			if (1 == context_existed)
				trx_strpool_release(hmacro->context);

			hmacro->context = NULL;
		}
		else
		{
			/* replace the existing context (1) or add context to macro (0) */
			DCstrpool_replace(context_existed, &hmacro->context, context);
		}

		/* update hmacros_hm index using new data */
		if (1 == update_index)
			config_hmacro_add_index(&config->hmacros_hm, hmacro);
	}

	/* remove deleted host macros from buffer */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (hmacro = (TRX_DC_HMACRO *)trx_hashset_search(&config->hmacros, &rowid)))
			continue;

		config_hmacro_remove_index(&config->hmacros_hm, hmacro);

		trx_strpool_release(hmacro->macro);
		trx_strpool_release(hmacro->value);

		if (NULL != hmacro->context)
			trx_strpool_release(hmacro->context);

		trx_hashset_remove_direct(&config->hmacros, hmacro);
	}

	trx_free(context);
	trx_free(macro);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: substitute_host_interface_macros                                 *
 *                                                                            *
 * Purpose: trying to resolve the macros in host interface                    *
 *                                                                            *
 ******************************************************************************/
static void	substitute_host_interface_macros(TRX_DC_INTERFACE *interface)
{
	int	macros;
	char	*addr;
	DC_HOST	host;

	macros = STR_CONTAINS_MACROS(interface->ip) ? 0x01 : 0;
	macros |= STR_CONTAINS_MACROS(interface->dns) ? 0x02 : 0;

	if (0 != macros)
	{
		DCget_host_by_hostid(&host, interface->hostid);

		if (0 != (macros & 0x01))
		{
			addr = trx_strdup(NULL, interface->ip);
			substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &host, NULL, NULL, NULL,
					&addr, MACRO_TYPE_INTERFACE_ADDR, NULL, 0);
			if (SUCCEED == is_ip(addr) || SUCCEED == trx_validate_hostname(addr))
				DCstrpool_replace(1, &interface->ip, addr);
			trx_free(addr);
		}

		if (0 != (macros & 0x02))
		{
			addr = trx_strdup(NULL, interface->dns);
			substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &host, NULL, NULL, NULL,
					&addr, MACRO_TYPE_INTERFACE_ADDR, NULL, 0);
			if (SUCCEED == is_ip(addr) || SUCCEED == trx_validate_hostname(addr))
				DCstrpool_replace(1, &interface->dns, addr);
			trx_free(addr);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_interface_snmpaddrs_remove                                    *
 *                                                                            *
 * Purpose: remove interface from SNMP address -> interfaceid index           *
 *                                                                            *
 * Parameters: interface - [IN] the interface                                 *
 *                                                                            *
 ******************************************************************************/
static void	dc_interface_snmpaddrs_remove(TRX_DC_INTERFACE *interface)
{
	TRX_DC_INTERFACE_ADDR	*ifaddr, ifaddr_local;
	int			index;

	ifaddr_local.addr = (0 != interface->useip ? interface->ip : interface->dns);

	if ('\0' == *ifaddr_local.addr)
		return;

	if (NULL == (ifaddr = (TRX_DC_INTERFACE_ADDR *)trx_hashset_search(&config->interface_snmpaddrs, &ifaddr_local)))
		return;

	if (FAIL == (index = trx_vector_uint64_search(&ifaddr->interfaceids, interface->interfaceid,
			TRX_DEFAULT_UINT64_COMPARE_FUNC)))
	{
		return;
	}

	trx_vector_uint64_remove_noorder(&ifaddr->interfaceids, index);

	if (0 == ifaddr->interfaceids.values_num)
	{
		trx_strpool_release(ifaddr->addr);
		trx_vector_uint64_destroy(&ifaddr->interfaceids);
		trx_hashset_remove_direct(&config->interface_snmpaddrs, ifaddr);
	}
}

static void	DCsync_interfaces(trx_dbsync_t *sync)
{
	char			**row;
	trx_uint64_t		rowid;
	unsigned char		tag;

	TRX_DC_INTERFACE	*interface;
	TRX_DC_INTERFACE_HT	*interface_ht, interface_ht_local;
	TRX_DC_INTERFACE_ADDR	*interface_snmpaddr, interface_snmpaddr_local;
	TRX_DC_HOST		*host;

	int			found, update_index, ret, i;
	trx_uint64_t		interfaceid, hostid;
	unsigned char		type, main_, useip;
	unsigned char		bulk, reset_snmp_stats;
	trx_vector_ptr_t	interfaces;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&interfaces);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(interfaceid, row[0]);
		TRX_STR2UINT64(hostid, row[1]);
		TRX_STR2UCHAR(type, row[2]);
		TRX_STR2UCHAR(main_, row[3]);
		TRX_STR2UCHAR(useip, row[4]);
		TRX_STR2UCHAR(bulk, row[8]);

		/* If there is no host for this interface, skip it. */
		/* This may be possible if the host was added after we synced config for hosts. */
		if (NULL == (host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &hostid)))
			continue;

		interface = (TRX_DC_INTERFACE *)DCfind_id(&config->interfaces, interfaceid, sizeof(TRX_DC_INTERFACE), &found);
		trx_vector_ptr_append(&interfaces, interface);

		/* remove old address->interfaceid index */
		if (0 != found && INTERFACE_TYPE_SNMP == interface->type)
			dc_interface_snmpaddrs_remove(interface);

		/* see whether we should and can update interfaces_ht index at this point */

		update_index = 0;

		if (0 == found || interface->hostid != hostid || interface->type != type || interface->main != main_)
		{
			if (1 == found && 1 == interface->main)
			{
				interface_ht_local.hostid = interface->hostid;
				interface_ht_local.type = interface->type;
				interface_ht = (TRX_DC_INTERFACE_HT *)trx_hashset_search(&config->interfaces_ht, &interface_ht_local);

				if (NULL != interface_ht && interface == interface_ht->interface_ptr)
				{
					/* see TRX-4045 for NULL check in the conditional */
					trx_hashset_remove(&config->interfaces_ht, &interface_ht_local);
				}
			}

			if (1 == main_)
			{
				interface_ht_local.hostid = hostid;
				interface_ht_local.type = type;
				interface_ht = (TRX_DC_INTERFACE_HT *)trx_hashset_search(&config->interfaces_ht, &interface_ht_local);

				if (NULL != interface_ht)
					interface_ht->interface_ptr = interface;
				else
					update_index = 1;
			}
		}

		/* store new information in interface structure */

		reset_snmp_stats = (0 == found || interface->hostid != hostid || interface->type != type ||
						interface->useip != useip || interface->bulk != bulk);

		interface->hostid = hostid;
		interface->type = type;
		interface->main = main_;
		interface->useip = useip;
		interface->bulk = bulk;
		reset_snmp_stats |= (SUCCEED == DCstrpool_replace(found, &interface->ip, row[5]));
		reset_snmp_stats |= (SUCCEED == DCstrpool_replace(found, &interface->dns, row[6]));
		reset_snmp_stats |= (SUCCEED == DCstrpool_replace(found, &interface->port, row[7]));

		/* update interfaces_ht index using new data, if not done already */

		if (1 == update_index)
		{
			interface_ht_local.hostid = interface->hostid;
			interface_ht_local.type = interface->type;
			interface_ht_local.interface_ptr = interface;
			trx_hashset_insert(&config->interfaces_ht, &interface_ht_local, sizeof(TRX_DC_INTERFACE_HT));
		}

		/* update interface_snmpaddrs for SNMP traps or reset bulk request statistics */

		if (INTERFACE_TYPE_SNMP == interface->type)
		{
			interface_snmpaddr_local.addr = (0 != interface->useip ? interface->ip : interface->dns);

			if ('\0' != *interface_snmpaddr_local.addr)
			{
				if (NULL == (interface_snmpaddr = (TRX_DC_INTERFACE_ADDR *)trx_hashset_search(&config->interface_snmpaddrs,
						&interface_snmpaddr_local)))
				{
					trx_strpool_acquire(interface_snmpaddr_local.addr);

					interface_snmpaddr = (TRX_DC_INTERFACE_ADDR *)trx_hashset_insert(&config->interface_snmpaddrs,
							&interface_snmpaddr_local, sizeof(TRX_DC_INTERFACE_ADDR));
					trx_vector_uint64_create_ext(&interface_snmpaddr->interfaceids,
							__config_mem_malloc_func,
							__config_mem_realloc_func,
							__config_mem_free_func);
				}

				trx_vector_uint64_append(&interface_snmpaddr->interfaceids, interfaceid);
			}

			if (1 == reset_snmp_stats)
			{
				interface->max_snmp_succeed = 0;
				interface->min_snmp_fail = MAX_SNMP_ITEMS + 1;
			}
		}

		/* first resolve macros for ip and dns fields in main agent interface  */
		/* because other interfaces might reference main interfaces ip and dns */
		/* with {HOST.IP} and {HOST.DNS} macros                                */
		if (1 == interface->main && INTERFACE_TYPE_AGENT == interface->type)
			substitute_host_interface_macros(interface);

		if (0 == found)
		{
			/* new interface - add it to a list of host interfaces in 'config->hosts' hashset */

			int	exists = 0;

			/* It is an error if the pointer is already in the list. Detect it. */

			for (i = 0; i < host->interfaces_v.values_num; i++)
			{
				if (interface == host->interfaces_v.values[i])
				{
					exists = 1;
					break;
				}
			}

			if (0 == exists)
				trx_vector_ptr_append(&host->interfaces_v, interface);
			else
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}

	/* resolve macros in other interfaces */

	for (i = 0; i < interfaces.values_num; i++)
	{
		interface = (TRX_DC_INTERFACE *)interfaces.values[i];

		if (1 != interface->main || INTERFACE_TYPE_AGENT != interface->type)
			substitute_host_interface_macros(interface);
	}

	/* remove deleted interfaces from buffer */

	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (interface = (TRX_DC_INTERFACE *)trx_hashset_search(&config->interfaces, &rowid)))
			continue;

		/* remove interface from the list of host interfaces in 'config->hosts' hashset */

		if (NULL != (host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &interface->hostid)))
		{
			for (i = 0; i < host->interfaces_v.values_num; i++)
			{
				if (interface == host->interfaces_v.values[i])
				{
					trx_vector_ptr_remove(&host->interfaces_v, i);
					break;
				}
			}
		}

		if (INTERFACE_TYPE_SNMP == interface->type)
			dc_interface_snmpaddrs_remove(interface);

		if (1 == interface->main)
		{
			interface_ht_local.hostid = interface->hostid;
			interface_ht_local.type = interface->type;
			interface_ht = (TRX_DC_INTERFACE_HT *)trx_hashset_search(&config->interfaces_ht, &interface_ht_local);

			if (NULL != interface_ht && interface == interface_ht->interface_ptr)
			{
				/* see TRX-4045 for NULL check in the conditional */
				trx_hashset_remove(&config->interfaces_ht, &interface_ht_local);
			}
		}

		trx_strpool_release(interface->ip);
		trx_strpool_release(interface->dns);
		trx_strpool_release(interface->port);

		trx_hashset_remove_direct(&config->interfaces, interface);
	}

	trx_vector_ptr_destroy(&interfaces);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_interface_snmpitems_remove                                    *
 *                                                                            *
 * Purpose: remove item from interfaceid -> itemid index                      *
 *                                                                            *
 * Parameters: interface - [IN] the item                                      *
 *                                                                            *
 ******************************************************************************/
static void	dc_interface_snmpitems_remove(TRX_DC_ITEM *item)
{
	TRX_DC_INTERFACE_ITEM	*ifitem;
	int			index;
	trx_uint64_t		interfaceid;

	if (0 == (interfaceid = item->interfaceid))
		return;

	if (NULL == (ifitem = (TRX_DC_INTERFACE_ITEM *)trx_hashset_search(&config->interface_snmpitems, &interfaceid)))
		return;

	if (FAIL == (index = trx_vector_uint64_search(&ifitem->itemids, item->itemid, TRX_DEFAULT_UINT64_COMPARE_FUNC)))
		return;

	trx_vector_uint64_remove_noorder(&ifitem->itemids, index);

	if (0 == ifitem->itemids.values_num)
	{
		trx_vector_uint64_destroy(&ifitem->itemids);
		trx_hashset_remove_direct(&config->interface_snmpitems, ifitem);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_masteritem_remove_depitem                                     *
 *                                                                            *
 * Purpose: remove itemid from master item dependent itemid vector            *
 *                                                                            *
 * Parameters: master_itemid - [IN] the master item identifier                *
 *             dep_itemid    - [IN] the dependent item identifier             *
 *                                                                            *
 ******************************************************************************/
static void	dc_masteritem_remove_depitem(trx_uint64_t master_itemid, trx_uint64_t dep_itemid)
{
	TRX_DC_MASTERITEM	*masteritem;
	int			index;
	trx_uint64_pair_t	pair;

	if (NULL == (masteritem = (TRX_DC_MASTERITEM *)trx_hashset_search(&config->masteritems, &master_itemid)))
		return;

	pair.first = dep_itemid;
	if (FAIL == (index = trx_vector_uint64_pair_search(&masteritem->dep_itemids, pair,
			TRX_DEFAULT_UINT64_COMPARE_FUNC)))
	{
		return;
	}

	trx_vector_uint64_pair_remove_noorder(&masteritem->dep_itemids, index);

	if (0 == masteritem->dep_itemids.values_num)
	{
		trx_vector_uint64_pair_destroy(&masteritem->dep_itemids);
		trx_hashset_remove_direct(&config->masteritems, masteritem);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_host_update_agent_stats                                       *
 *                                                                            *
 * Purpose: update number of items per agent statistics                       *
 *                                                                            *
 * Parameters: host - [IN] the host                                           *
 *             type - [IN] the item type (ITEM_TYPE_*)                        *
 *             num  - [IN] the number of items (+) added, (-) removed         *
 *                                                                            *
 ******************************************************************************/
static void	dc_host_update_agent_stats(TRX_DC_HOST *host, unsigned char type, int num)
{
	switch (type)
	{
		case ITEM_TYPE_TREEGIX:
			host->items_num += num;
			break;
		case ITEM_TYPE_SNMPv1:
		case ITEM_TYPE_SNMPv2c:
		case ITEM_TYPE_SNMPv3:
			host->snmp_items_num += num;
			break;
		case ITEM_TYPE_IPMI:
			host->ipmi_items_num += num;
			break;
		case ITEM_TYPE_JMX:
			host->jmx_items_num += num;
	}
}

static void	DCsync_items(trx_dbsync_t *sync, int flags)
{
	char			**row;
	trx_uint64_t		rowid;
	unsigned char		tag;

	TRX_DC_HOST		*host;

	TRX_DC_ITEM		*item;
	TRX_DC_NUMITEM		*numitem;
	TRX_DC_SNMPITEM		*snmpitem;
	TRX_DC_IPMIITEM		*ipmiitem;
	TRX_DC_TRAPITEM		*trapitem;
	TRX_DC_DEPENDENTITEM	*depitem;
	TRX_DC_LOGITEM		*logitem;
	TRX_DC_DBITEM		*dbitem;
	TRX_DC_SSHITEM		*sshitem;
	TRX_DC_TELNETITEM	*telnetitem;
	TRX_DC_SIMPLEITEM	*simpleitem;
	TRX_DC_JMXITEM		*jmxitem;
	TRX_DC_CALCITEM		*calcitem;
	TRX_DC_INTERFACE_ITEM	*interface_snmpitem;
	TRX_DC_MASTERITEM	*master;
	TRX_DC_PREPROCITEM	*preprocitem;
	TRX_DC_HTTPITEM		*httpitem;
	TRX_DC_ITEM_HK		*item_hk, item_hk_local;

	time_t			now;
	unsigned char		status, type, value_type, old_poller_type;
	int			found, update_index, ret, i,  old_nextcheck;
	trx_uint64_t		itemid, hostid;
	trx_vector_ptr_t	dep_items;

	trx_vector_ptr_create(&dep_items);

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	now = time(NULL);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		flags &= TRX_REFRESH_UNSUPPORTED_CHANGED;

		TRX_STR2UINT64(itemid, row[0]);
		TRX_STR2UINT64(hostid, row[1]);
		TRX_STR2UCHAR(status, row[2]);
		TRX_STR2UCHAR(type, row[3]);

		if (NULL == (host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &hostid)))
			continue;

		item = (TRX_DC_ITEM *)DCfind_id(&config->items, itemid, sizeof(TRX_DC_ITEM), &found);

		/* template item */
		TRX_DBROW2UINT64(item->templateid, row[57]);

		/* LLD item prototype */
		TRX_DBROW2UINT64(item->parent_itemid, row[58]);

		if (0 != found && ITEM_TYPE_SNMPTRAP == item->type)
			dc_interface_snmpitems_remove(item);

		/* see whether we should and can update items_hk index at this point */

		update_index = 0;

		if (0 == found || item->hostid != hostid || 0 != strcmp(item->key, row[5]))
		{
			if (1 == found)
			{
				item_hk_local.hostid = item->hostid;
				item_hk_local.key = item->key;
				item_hk = (TRX_DC_ITEM_HK *)trx_hashset_search(&config->items_hk, &item_hk_local);

				if (item == item_hk->item_ptr)
				{
					trx_strpool_release(item_hk->key);
					trx_hashset_remove_direct(&config->items_hk, item_hk);
				}
			}

			item_hk_local.hostid = hostid;
			item_hk_local.key = row[5];
			item_hk = (TRX_DC_ITEM_HK *)trx_hashset_search(&config->items_hk, &item_hk_local);

			if (NULL != item_hk)
				item_hk->item_ptr = item;
			else
				update_index = 1;
		}

		/* store new information in item structure */

		item->hostid = hostid;
		DCstrpool_replace(found, &item->port, row[8]);
		item->flags = (unsigned char)atoi(row[24]);
		TRX_DBROW2UINT64(item->interfaceid, row[25]);

		if (SUCCEED != is_time_suffix(row[31], &item->history_sec, TRX_LENGTH_UNLIMITED))
			item->history_sec = TRX_HK_PERIOD_MAX;

		if (0 != item->history_sec && TRX_HK_OPTION_ENABLED == config->config->hk.history_global)
			item->history_sec = config->config->hk.history;

		item->history = (0 != item->history_sec);

		TRX_STR2UCHAR(item->inventory_link, row[33]);
		TRX_DBROW2UINT64(item->valuemapid, row[34]);

		if (0 != (TRX_FLAG_DISCOVERY_RULE & item->flags))
			value_type = ITEM_VALUE_TYPE_TEXT;
		else
			TRX_STR2UCHAR(value_type, row[4]);

		if (SUCCEED == DCstrpool_replace(found, &item->key, row[5]))
			flags |= TRX_ITEM_KEY_CHANGED;

		if (0 == found)
		{
			item->triggers = NULL;
			item->update_triggers = 0;
			item->nextcheck = 0;
			item->lastclock = 0;
			item->state = (unsigned char)atoi(row[18]);
			TRX_STR2UINT64(item->lastlogsize, row[29]);
			item->mtime = atoi(row[30]);
			DCstrpool_replace(found, &item->error, row[36]);
			item->data_expected_from = now;
			item->location = TRX_LOC_NOWHERE;
			item->poller_type = TRX_NO_POLLER;
			item->queue_priority = TRX_QUEUE_PRIORITY_NORMAL;
			item->schedulable = 1;
		}
		else
		{
			if (item->type != type)
				flags |= TRX_ITEM_TYPE_CHANGED;

			if (ITEM_STATUS_ACTIVE == status && ITEM_STATUS_ACTIVE != item->status)
				item->data_expected_from = now;

			if (ITEM_STATUS_ACTIVE == item->status)
				dc_host_update_agent_stats(host, item->type, -1);
		}

		if (ITEM_STATUS_ACTIVE == status)
			dc_host_update_agent_stats(host, type, 1);

		item->type = type;
		item->status = status;
		item->value_type = value_type;

		/* update items_hk index using new data, if not done already */

		if (1 == update_index)
		{
			item_hk_local.hostid = item->hostid;
			item_hk_local.key = trx_strpool_acquire(item->key);
			item_hk_local.item_ptr = item;
			trx_hashset_insert(&config->items_hk, &item_hk_local, sizeof(TRX_DC_ITEM_HK));
		}

		/* process item intervals and update item nextcheck */

		if (SUCCEED == DCstrpool_replace(found, &item->delay, row[14]))
			flags |= TRX_ITEM_DELAY_CHANGED;

		/* numeric items */

		if (ITEM_VALUE_TYPE_FLOAT == item->value_type || ITEM_VALUE_TYPE_UINT64 == item->value_type)
		{
			int	trends_sec;

			numitem = (TRX_DC_NUMITEM *)DCfind_id(&config->numitems, itemid, sizeof(TRX_DC_NUMITEM), &found);

			if (SUCCEED != is_time_suffix(row[32], &trends_sec, TRX_LENGTH_UNLIMITED))
				trends_sec = TRX_HK_PERIOD_MAX;

			if (0 != trends_sec && TRX_HK_OPTION_ENABLED == config->config->hk.trends_global)
				trends_sec = config->config->hk.trends;

			numitem->trends = (0 != trends_sec);

			DCstrpool_replace(found, &numitem->units, row[35]);
		}
		else if (NULL != (numitem = (TRX_DC_NUMITEM *)trx_hashset_search(&config->numitems, &itemid)))
		{
			/* remove parameters for non-numeric item */

			trx_strpool_release(numitem->units);

			trx_hashset_remove_direct(&config->numitems, numitem);
		}

		/* SNMP items */

		if (SUCCEED == is_snmp_type(item->type))
		{
			snmpitem = (TRX_DC_SNMPITEM *)DCfind_id(&config->snmpitems, itemid, sizeof(TRX_DC_SNMPITEM), &found);

			DCstrpool_replace(found, &snmpitem->snmp_community, row[6]);
			DCstrpool_replace(found, &snmpitem->snmpv3_securityname, row[9]);
			snmpitem->snmpv3_securitylevel = (unsigned char)atoi(row[10]);
			DCstrpool_replace(found, &snmpitem->snmpv3_authpassphrase, row[11]);
			DCstrpool_replace(found, &snmpitem->snmpv3_privpassphrase, row[12]);
			snmpitem->snmpv3_authprotocol = (unsigned char)atoi(row[26]);
			snmpitem->snmpv3_privprotocol = (unsigned char)atoi(row[27]);
			DCstrpool_replace(found, &snmpitem->snmpv3_contextname, row[28]);

			if (SUCCEED == DCstrpool_replace(found, &snmpitem->snmp_oid, row[7]))
			{
				if (NULL != strchr(snmpitem->snmp_oid, '{'))
					snmpitem->snmp_oid_type = TRX_SNMP_OID_TYPE_MACRO;
				else if (NULL != strchr(snmpitem->snmp_oid, '['))
					snmpitem->snmp_oid_type = TRX_SNMP_OID_TYPE_DYNAMIC;
				else
					snmpitem->snmp_oid_type = TRX_SNMP_OID_TYPE_NORMAL;
			}
		}
		else if (NULL != (snmpitem = (TRX_DC_SNMPITEM *)trx_hashset_search(&config->snmpitems, &itemid)))
		{
			/* remove SNMP parameters for non-SNMP item */

			trx_strpool_release(snmpitem->snmp_community);
			trx_strpool_release(snmpitem->snmp_oid);
			trx_strpool_release(snmpitem->snmpv3_securityname);
			trx_strpool_release(snmpitem->snmpv3_authpassphrase);
			trx_strpool_release(snmpitem->snmpv3_privpassphrase);
			trx_strpool_release(snmpitem->snmpv3_contextname);

			trx_hashset_remove_direct(&config->snmpitems, snmpitem);
		}

		/* IPMI items */

		if (ITEM_TYPE_IPMI == item->type)
		{
			ipmiitem = (TRX_DC_IPMIITEM *)DCfind_id(&config->ipmiitems, itemid, sizeof(TRX_DC_IPMIITEM), &found);

			DCstrpool_replace(found, &ipmiitem->ipmi_sensor, row[13]);
		}
		else if (NULL != (ipmiitem = (TRX_DC_IPMIITEM *)trx_hashset_search(&config->ipmiitems, &itemid)))
		{
			/* remove IPMI parameters for non-IPMI item */
			trx_strpool_release(ipmiitem->ipmi_sensor);
			trx_hashset_remove_direct(&config->ipmiitems, ipmiitem);
		}

		/* trapper items */

		if (ITEM_TYPE_TRAPPER == item->type && '\0' != *row[15])
		{
			trapitem = (TRX_DC_TRAPITEM *)DCfind_id(&config->trapitems, itemid, sizeof(TRX_DC_TRAPITEM), &found);
			DCstrpool_replace(found, &trapitem->trapper_hosts, row[15]);
		}
		else if (NULL != (trapitem = (TRX_DC_TRAPITEM *)trx_hashset_search(&config->trapitems, &itemid)))
		{
			/* remove trapper_hosts parameter */
			trx_strpool_release(trapitem->trapper_hosts);
			trx_hashset_remove_direct(&config->trapitems, trapitem);
		}

		/* dependent items */

		if (ITEM_TYPE_DEPENDENT == item->type && SUCCEED != DBis_null(row[38]) && 0 == host->proxy_hostid)
		{
			depitem = (TRX_DC_DEPENDENTITEM *)DCfind_id(&config->dependentitems, itemid,
					sizeof(TRX_DC_DEPENDENTITEM), &found);

			if (1 == found)
				depitem->last_master_itemid = depitem->master_itemid;
			else
				depitem->last_master_itemid = 0;

			depitem->flags = item->flags;
			TRX_STR2UINT64(depitem->master_itemid, row[38]);

			if (depitem->last_master_itemid != depitem->master_itemid)
				trx_vector_ptr_append(&dep_items, depitem);
		}
		else if (NULL != (depitem = (TRX_DC_DEPENDENTITEM *)trx_hashset_search(&config->dependentitems, &itemid)))
		{
			dc_masteritem_remove_depitem(depitem->master_itemid, itemid);
			trx_hashset_remove_direct(&config->dependentitems, depitem);
		}

		/* log items */

		if (ITEM_VALUE_TYPE_LOG == item->value_type && '\0' != *row[16])
		{
			logitem = (TRX_DC_LOGITEM *)DCfind_id(&config->logitems, itemid, sizeof(TRX_DC_LOGITEM), &found);

			DCstrpool_replace(found, &logitem->logtimefmt, row[16]);
		}
		else if (NULL != (logitem = (TRX_DC_LOGITEM *)trx_hashset_search(&config->logitems, &itemid)))
		{
			/* remove logtimefmt parameter */
			trx_strpool_release(logitem->logtimefmt);
			trx_hashset_remove_direct(&config->logitems, logitem);
		}

		/* db items */

		if (ITEM_TYPE_DB_MONITOR == item->type && '\0' != *row[17])
		{
			dbitem = (TRX_DC_DBITEM *)DCfind_id(&config->dbitems, itemid, sizeof(TRX_DC_DBITEM), &found);

			DCstrpool_replace(found, &dbitem->params, row[17]);
			DCstrpool_replace(found, &dbitem->username, row[20]);
			DCstrpool_replace(found, &dbitem->password, row[21]);
		}
		else if (NULL != (dbitem = (TRX_DC_DBITEM *)trx_hashset_search(&config->dbitems, &itemid)))
		{
			/* remove db item parameters */
			trx_strpool_release(dbitem->params);
			trx_strpool_release(dbitem->username);
			trx_strpool_release(dbitem->password);

			trx_hashset_remove_direct(&config->dbitems, dbitem);
		}

		/* SSH items */

		if (ITEM_TYPE_SSH == item->type)
		{
			sshitem = (TRX_DC_SSHITEM *)DCfind_id(&config->sshitems, itemid, sizeof(TRX_DC_SSHITEM), &found);

			sshitem->authtype = (unsigned short)atoi(row[19]);
			DCstrpool_replace(found, &sshitem->username, row[20]);
			DCstrpool_replace(found, &sshitem->password, row[21]);
			DCstrpool_replace(found, &sshitem->publickey, row[22]);
			DCstrpool_replace(found, &sshitem->privatekey, row[23]);
			DCstrpool_replace(found, &sshitem->params, row[17]);
		}
		else if (NULL != (sshitem = (TRX_DC_SSHITEM *)trx_hashset_search(&config->sshitems, &itemid)))
		{
			/* remove SSH item parameters */

			trx_strpool_release(sshitem->username);
			trx_strpool_release(sshitem->password);
			trx_strpool_release(sshitem->publickey);
			trx_strpool_release(sshitem->privatekey);
			trx_strpool_release(sshitem->params);

			trx_hashset_remove_direct(&config->sshitems, sshitem);
		}

		/* TELNET items */

		if (ITEM_TYPE_TELNET == item->type)
		{
			telnetitem = (TRX_DC_TELNETITEM *)DCfind_id(&config->telnetitems, itemid, sizeof(TRX_DC_TELNETITEM), &found);

			DCstrpool_replace(found, &telnetitem->username, row[20]);
			DCstrpool_replace(found, &telnetitem->password, row[21]);
			DCstrpool_replace(found, &telnetitem->params, row[17]);
		}
		else if (NULL != (telnetitem = (TRX_DC_TELNETITEM *)trx_hashset_search(&config->telnetitems, &itemid)))
		{
			/* remove TELNET item parameters */

			trx_strpool_release(telnetitem->username);
			trx_strpool_release(telnetitem->password);
			trx_strpool_release(telnetitem->params);

			trx_hashset_remove_direct(&config->telnetitems, telnetitem);
		}

		/* simple items */

		if (ITEM_TYPE_SIMPLE == item->type)
		{
			simpleitem = (TRX_DC_SIMPLEITEM *)DCfind_id(&config->simpleitems, itemid, sizeof(TRX_DC_SIMPLEITEM), &found);

			DCstrpool_replace(found, &simpleitem->username, row[20]);
			DCstrpool_replace(found, &simpleitem->password, row[21]);
		}
		else if (NULL != (simpleitem = (TRX_DC_SIMPLEITEM *)trx_hashset_search(&config->simpleitems, &itemid)))
		{
			/* remove simple item parameters */

			trx_strpool_release(simpleitem->username);
			trx_strpool_release(simpleitem->password);

			trx_hashset_remove_direct(&config->simpleitems, simpleitem);
		}

		/* JMX items */

		if (ITEM_TYPE_JMX == item->type)
		{
			jmxitem = (TRX_DC_JMXITEM *)DCfind_id(&config->jmxitems, itemid, sizeof(TRX_DC_JMXITEM), &found);

			DCstrpool_replace(found, &jmxitem->username, row[20]);
			DCstrpool_replace(found, &jmxitem->password, row[21]);
			DCstrpool_replace(found, &jmxitem->jmx_endpoint, row[37]);
		}
		else if (NULL != (jmxitem = (TRX_DC_JMXITEM *)trx_hashset_search(&config->jmxitems, &itemid)))
		{
			/* remove JMX item parameters */

			trx_strpool_release(jmxitem->username);
			trx_strpool_release(jmxitem->password);
			trx_strpool_release(jmxitem->jmx_endpoint);

			trx_hashset_remove_direct(&config->jmxitems, jmxitem);
		}

		/* SNMP trap items for current server/proxy */

		if (ITEM_TYPE_SNMPTRAP == item->type && 0 == host->proxy_hostid)
		{
			interface_snmpitem = (TRX_DC_INTERFACE_ITEM *)DCfind_id(&config->interface_snmpitems,
					item->interfaceid, sizeof(TRX_DC_INTERFACE_ITEM), &found);

			if (0 == found)
			{
				trx_vector_uint64_create_ext(&interface_snmpitem->itemids,
						__config_mem_malloc_func,
						__config_mem_realloc_func,
						__config_mem_free_func);
			}

			trx_vector_uint64_append(&interface_snmpitem->itemids, itemid);
		}

		/* calculated items */

		if (ITEM_TYPE_CALCULATED == item->type)
		{
			calcitem = (TRX_DC_CALCITEM *)DCfind_id(&config->calcitems, itemid, sizeof(TRX_DC_CALCITEM), &found);

			DCstrpool_replace(found, &calcitem->params, row[17]);
		}
		else if (NULL != (calcitem = (TRX_DC_CALCITEM *)trx_hashset_search(&config->calcitems, &itemid)))
		{
			/* remove calculated item parameters */

			trx_strpool_release(calcitem->params);
			trx_hashset_remove_direct(&config->calcitems, calcitem);
		}

		/* HTTP agent items */

		if (ITEM_TYPE_HTTPAGENT == item->type)
		{
			httpitem = (TRX_DC_HTTPITEM *)DCfind_id(&config->httpitems, itemid, sizeof(TRX_DC_HTTPITEM),
					&found);

			DCstrpool_replace(found, &httpitem->timeout, row[39]);
			DCstrpool_replace(found, &httpitem->url, row[40]);
			DCstrpool_replace(found, &httpitem->query_fields, row[41]);
			DCstrpool_replace(found, &httpitem->posts, row[42]);
			DCstrpool_replace(found, &httpitem->status_codes, row[43]);
			httpitem->follow_redirects = (unsigned char)atoi(row[44]);
			httpitem->post_type = (unsigned char)atoi(row[45]);
			DCstrpool_replace(found, &httpitem->http_proxy, row[46]);
			DCstrpool_replace(found, &httpitem->headers, row[47]);
			httpitem->retrieve_mode = (unsigned char)atoi(row[48]);
			httpitem->request_method = (unsigned char)atoi(row[49]);
			httpitem->output_format = (unsigned char)atoi(row[50]);
			DCstrpool_replace(found, &httpitem->ssl_cert_file, row[51]);
			DCstrpool_replace(found, &httpitem->ssl_key_file, row[52]);
			DCstrpool_replace(found, &httpitem->ssl_key_password, row[53]);
			httpitem->verify_peer = (unsigned char)atoi(row[54]);
			httpitem->verify_host = (unsigned char)atoi(row[55]);
			httpitem->allow_traps = (unsigned char)atoi(row[56]);

			httpitem->authtype = (unsigned char)atoi(row[19]);
			DCstrpool_replace(found, &httpitem->username, row[20]);
			DCstrpool_replace(found, &httpitem->password, row[21]);
			DCstrpool_replace(found, &httpitem->trapper_hosts, row[15]);
		}
		else if (NULL != (httpitem = (TRX_DC_HTTPITEM *)trx_hashset_search(&config->httpitems, &itemid)))
		{
			trx_strpool_release(httpitem->timeout);
			trx_strpool_release(httpitem->url);
			trx_strpool_release(httpitem->query_fields);
			trx_strpool_release(httpitem->posts);
			trx_strpool_release(httpitem->status_codes);
			trx_strpool_release(httpitem->http_proxy);
			trx_strpool_release(httpitem->headers);
			trx_strpool_release(httpitem->ssl_cert_file);
			trx_strpool_release(httpitem->ssl_key_file);
			trx_strpool_release(httpitem->ssl_key_password);
			trx_strpool_release(httpitem->username);
			trx_strpool_release(httpitem->password);
			trx_strpool_release(httpitem->trapper_hosts);

			trx_hashset_remove_direct(&config->httpitems, httpitem);
		}

		/* it is crucial to update type specific (config->snmpitems, config->ipmiitems, etc.) hashsets before */
		/* attempting to requeue an item because type specific properties are used to arrange items in queues */

		old_poller_type = item->poller_type;
		old_nextcheck = item->nextcheck;

		if (ITEM_STATUS_ACTIVE == item->status && HOST_STATUS_MONITORED == host->status)
		{
			DCitem_poller_type_update(item, host, flags);

			if (SUCCEED == trx_is_counted_in_item_queue(item->type, item->key))
			{
				char	*error = NULL;

				if (FAIL == DCitem_nextcheck_update(item, host, item->state, flags, now, &error))
				{
					trx_timespec_t	ts = {now, 0};

					/* Usual way for an item to become not supported is to receive an error     */
					/* instead of value. Item state and error will be updated by history syncer */
					/* during history sync following a regular procedure with item update in    */
					/* database and config cache, logging etc. There is no need to set          */
					/* ITEM_STATE_NOTSUPPORTED here.                                            */

					if (0 == host->proxy_hostid)
					{
						dc_add_history(item->itemid, item->value_type, 0, NULL, &ts,
								ITEM_STATE_NOTSUPPORTED, error);
					}
					trx_free(error);
				}
			}
		}
		else
		{
			item->nextcheck = 0;
			item->queue_priority = TRX_QUEUE_PRIORITY_NORMAL;
			item->poller_type = TRX_NO_POLLER;
		}

		DCupdate_item_queue(item, old_poller_type, old_nextcheck);
	}

	/* update dependent item vectors within master items */

	for (i = 0; i < dep_items.values_num; i++)
	{
		trx_uint64_pair_t	pair;

		depitem = (TRX_DC_DEPENDENTITEM *)dep_items.values[i];
		dc_masteritem_remove_depitem(depitem->last_master_itemid, depitem->itemid);
		pair.first = depitem->itemid;
		pair.second = depitem->flags;

		/* append item to dependent item vector of master item */
		if (NULL == (master = (TRX_DC_MASTERITEM *)trx_hashset_search(&config->masteritems, &depitem->master_itemid)))
		{
			TRX_DC_MASTERITEM	master_local;

			master_local.itemid = depitem->master_itemid;
			master = (TRX_DC_MASTERITEM *)trx_hashset_insert(&config->masteritems, &master_local, sizeof(master_local));

			trx_vector_uint64_pair_create_ext(&master->dep_itemids, __config_mem_malloc_func,
					__config_mem_realloc_func, __config_mem_free_func);
		}

		trx_vector_uint64_pair_append(&master->dep_itemids, pair);
	}

	trx_vector_ptr_destroy(&dep_items);

	/* remove deleted items from buffer */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &rowid)))
			continue;

		if (ITEM_STATUS_ACTIVE == item->status &&
				NULL != (host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &item->hostid)))
		{
			dc_host_update_agent_stats(host, item->type, -1);
		}

		itemid = item->itemid;

		if (ITEM_TYPE_SNMPTRAP == item->type)
			dc_interface_snmpitems_remove(item);

		/* numeric items */

		if (ITEM_VALUE_TYPE_FLOAT == item->value_type || ITEM_VALUE_TYPE_UINT64 == item->value_type)
		{
			numitem = (TRX_DC_NUMITEM *)trx_hashset_search(&config->numitems, &itemid);

			trx_strpool_release(numitem->units);

			trx_hashset_remove_direct(&config->numitems, numitem);
		}

		/* SNMP items */

		if (SUCCEED == is_snmp_type(item->type))
		{
			snmpitem = (TRX_DC_SNMPITEM *)trx_hashset_search(&config->snmpitems, &itemid);

			trx_strpool_release(snmpitem->snmp_community);
			trx_strpool_release(snmpitem->snmp_oid);
			trx_strpool_release(snmpitem->snmpv3_securityname);
			trx_strpool_release(snmpitem->snmpv3_authpassphrase);
			trx_strpool_release(snmpitem->snmpv3_privpassphrase);
			trx_strpool_release(snmpitem->snmpv3_contextname);

			trx_hashset_remove_direct(&config->snmpitems, snmpitem);
		}

		/* IPMI items */

		if (ITEM_TYPE_IPMI == item->type)
		{
			ipmiitem = (TRX_DC_IPMIITEM *)trx_hashset_search(&config->ipmiitems, &itemid);
			trx_strpool_release(ipmiitem->ipmi_sensor);
			trx_hashset_remove_direct(&config->ipmiitems, ipmiitem);
		}

		/* trapper items */

		if (ITEM_TYPE_TRAPPER == item->type &&
				NULL != (trapitem = (TRX_DC_TRAPITEM *)trx_hashset_search(&config->trapitems, &itemid)))
		{
			trx_strpool_release(trapitem->trapper_hosts);
			trx_hashset_remove_direct(&config->trapitems, trapitem);
		}

		/* dependent items */

		if (NULL != (depitem = (TRX_DC_DEPENDENTITEM *)trx_hashset_search(&config->dependentitems, &itemid)))
		{
			dc_masteritem_remove_depitem(depitem->master_itemid, itemid);
			trx_hashset_remove_direct(&config->dependentitems, depitem);
		}

		/* log items */

		if (ITEM_VALUE_TYPE_LOG == item->value_type &&
				NULL != (logitem = (TRX_DC_LOGITEM *)trx_hashset_search(&config->logitems, &itemid)))
		{
			trx_strpool_release(logitem->logtimefmt);
			trx_hashset_remove_direct(&config->logitems, logitem);
		}

		/* db items */

		if (ITEM_TYPE_DB_MONITOR == item->type &&
				NULL != (dbitem = (TRX_DC_DBITEM *)trx_hashset_search(&config->dbitems, &itemid)))
		{
			trx_strpool_release(dbitem->params);
			trx_strpool_release(dbitem->username);
			trx_strpool_release(dbitem->password);

			trx_hashset_remove_direct(&config->dbitems, dbitem);
		}

		/* SSH items */

		if (ITEM_TYPE_SSH == item->type)
		{
			sshitem = (TRX_DC_SSHITEM *)trx_hashset_search(&config->sshitems, &itemid);

			trx_strpool_release(sshitem->username);
			trx_strpool_release(sshitem->password);
			trx_strpool_release(sshitem->publickey);
			trx_strpool_release(sshitem->privatekey);
			trx_strpool_release(sshitem->params);

			trx_hashset_remove_direct(&config->sshitems, sshitem);
		}

		/* TELNET items */

		if (ITEM_TYPE_TELNET == item->type)
		{
			telnetitem = (TRX_DC_TELNETITEM *)trx_hashset_search(&config->telnetitems, &itemid);

			trx_strpool_release(telnetitem->username);
			trx_strpool_release(telnetitem->password);
			trx_strpool_release(telnetitem->params);

			trx_hashset_remove_direct(&config->telnetitems, telnetitem);
		}

		/* simple items */

		if (ITEM_TYPE_SIMPLE == item->type)
		{
			simpleitem = (TRX_DC_SIMPLEITEM *)trx_hashset_search(&config->simpleitems, &itemid);

			trx_strpool_release(simpleitem->username);
			trx_strpool_release(simpleitem->password);

			trx_hashset_remove_direct(&config->simpleitems, simpleitem);
		}

		/* JMX items */

		if (ITEM_TYPE_JMX == item->type)
		{
			jmxitem = (TRX_DC_JMXITEM *)trx_hashset_search(&config->jmxitems, &itemid);

			trx_strpool_release(jmxitem->username);
			trx_strpool_release(jmxitem->password);
			trx_strpool_release(jmxitem->jmx_endpoint);

			trx_hashset_remove_direct(&config->jmxitems, jmxitem);
		}

		/* calculated items */

		if (ITEM_TYPE_CALCULATED == item->type)
		{
			calcitem = (TRX_DC_CALCITEM *)trx_hashset_search(&config->calcitems, &itemid);
			trx_strpool_release(calcitem->params);
			trx_hashset_remove_direct(&config->calcitems, calcitem);
		}

		/* HTTP agent items */

		if (ITEM_TYPE_HTTPAGENT == item->type)
		{
			httpitem = (TRX_DC_HTTPITEM *)trx_hashset_search(&config->httpitems, &itemid);

			trx_strpool_release(httpitem->timeout);
			trx_strpool_release(httpitem->url);
			trx_strpool_release(httpitem->query_fields);
			trx_strpool_release(httpitem->posts);
			trx_strpool_release(httpitem->status_codes);
			trx_strpool_release(httpitem->http_proxy);
			trx_strpool_release(httpitem->headers);
			trx_strpool_release(httpitem->ssl_cert_file);
			trx_strpool_release(httpitem->ssl_key_file);
			trx_strpool_release(httpitem->ssl_key_password);
			trx_strpool_release(httpitem->username);
			trx_strpool_release(httpitem->password);
			trx_strpool_release(httpitem->trapper_hosts);

			trx_hashset_remove_direct(&config->httpitems, httpitem);
		}

		/* items */

		item_hk_local.hostid = item->hostid;
		item_hk_local.key = item->key;
		item_hk = (TRX_DC_ITEM_HK *)trx_hashset_search(&config->items_hk, &item_hk_local);

		if (item == item_hk->item_ptr)
		{
			trx_strpool_release(item_hk->key);
			trx_hashset_remove_direct(&config->items_hk, item_hk);
		}

		if (TRX_LOC_QUEUE == item->location)
			trx_binary_heap_remove_direct(&config->queues[item->poller_type], item->itemid);

		trx_strpool_release(item->key);
		trx_strpool_release(item->port);
		trx_strpool_release(item->error);
		trx_strpool_release(item->delay);

		if (NULL != item->triggers)
			config->items.mem_free_func(item->triggers);

		if (NULL != (preprocitem = (TRX_DC_PREPROCITEM *)trx_hashset_search(&config->preprocitems, &item->itemid)))
		{
			trx_vector_ptr_destroy(&preprocitem->preproc_ops);
			trx_hashset_remove_direct(&config->preprocitems, preprocitem);
		}

		trx_hashset_remove_direct(&config->items, item);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_template_items(trx_dbsync_t *sync)
{
	char			**row;
	trx_uint64_t		rowid, itemid;
	unsigned char		tag;
	int			ret, found;
	TRX_DC_TEMPLATE_ITEM 	*item;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(itemid, row[0]);
		item = (TRX_DC_TEMPLATE_ITEM *)DCfind_id(&config->template_items, itemid, sizeof(TRX_DC_TEMPLATE_ITEM),
				&found);

		TRX_STR2UINT64(item->hostid, row[1]);
		TRX_DBROW2UINT64(item->templateid, row[2]);
	}

	/* remove deleted template items from buffer */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (item = (TRX_DC_TEMPLATE_ITEM *)trx_hashset_search(&config->template_items, &rowid)))
			continue;

		trx_hashset_remove_direct(&config->template_items, item);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_prototype_items(trx_dbsync_t *sync)
{
	char			**row;
	trx_uint64_t		rowid, itemid;
	unsigned char		tag;
	int			ret, found;
	TRX_DC_PROTOTYPE_ITEM 	*item;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(itemid, row[0]);
		item = (TRX_DC_PROTOTYPE_ITEM *)DCfind_id(&config->prototype_items, itemid,
				sizeof(TRX_DC_PROTOTYPE_ITEM), &found);

		TRX_STR2UINT64(item->hostid, row[1]);
		TRX_DBROW2UINT64(item->templateid, row[2]);
	}

	/* remove deleted prototype items from buffer */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (item = (TRX_DC_PROTOTYPE_ITEM *)trx_hashset_search(&config->prototype_items, &rowid)))
			continue;

		trx_hashset_remove_direct(&config->prototype_items, item);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_triggers(trx_dbsync_t *sync)
{
	char		**row;
	trx_uint64_t	rowid;
	unsigned char	tag;

	TRX_DC_TRIGGER	*trigger;

	int		found, ret;
	trx_uint64_t	triggerid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(triggerid, row[0]);

		trigger = (TRX_DC_TRIGGER *)DCfind_id(&config->triggers, triggerid, sizeof(TRX_DC_TRIGGER), &found);

		/* store new information in trigger structure */

		DCstrpool_replace(found, &trigger->description, row[1]);
		DCstrpool_replace(found, &trigger->expression, row[2]);
		DCstrpool_replace(found, &trigger->recovery_expression, row[11]);
		DCstrpool_replace(found, &trigger->correlation_tag, row[13]);
		DCstrpool_replace(found, &trigger->opdata, row[14]);
		TRX_STR2UCHAR(trigger->priority, row[4]);
		TRX_STR2UCHAR(trigger->type, row[5]);
		TRX_STR2UCHAR(trigger->status, row[9]);
		TRX_STR2UCHAR(trigger->recovery_mode, row[10]);
		TRX_STR2UCHAR(trigger->correlation_mode, row[12]);

		if (0 == found)
		{
			DCstrpool_replace(found, &trigger->error, row[3]);
			TRX_STR2UCHAR(trigger->value, row[6]);
			TRX_STR2UCHAR(trigger->state, row[7]);
			trigger->lastchange = atoi(row[8]);
			trigger->locked = 0;

			trx_vector_ptr_create_ext(&trigger->tags, __config_mem_malloc_func, __config_mem_realloc_func,
					__config_mem_free_func);
			trigger->topoindex = 1;
		}
	}

	/* remove deleted triggers from buffer */
	if (SUCCEED == ret)
	{
		trx_vector_uint64_t	functionids;
		int			i;
		TRX_DC_ITEM		*item;
		TRX_DC_FUNCTION		*function;

		trx_vector_uint64_create(&functionids);

		for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
		{
			if (NULL == (trigger = (TRX_DC_TRIGGER *)trx_hashset_search(&config->triggers, &rowid)))
				continue;

			/* force trigger list update for items used in removed trigger */

			get_functionids(&functionids, trigger->expression);

			if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == trigger->recovery_mode)
				get_functionids(&functionids, trigger->recovery_expression);

			for (i = 0; i < functionids.values_num; i++)
			{
				if (NULL == (function = (TRX_DC_FUNCTION *)trx_hashset_search(&config->functions, &functionids.values[i])))
					continue;

				if (NULL == (item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &function->itemid)))
					continue;

				item->update_triggers = 1;
				if (NULL != item->triggers)
				{
					config->items.mem_free_func(item->triggers);
					item->triggers = NULL;
				}
			}
			trx_vector_uint64_clear(&functionids);

			trx_strpool_release(trigger->description);
			trx_strpool_release(trigger->expression);
			trx_strpool_release(trigger->recovery_expression);
			trx_strpool_release(trigger->error);
			trx_strpool_release(trigger->correlation_tag);
			trx_strpool_release(trigger->opdata);

			trx_vector_ptr_destroy(&trigger->tags);

			trx_hashset_remove_direct(&config->triggers, trigger);
		}
		trx_vector_uint64_destroy(&functionids);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCconfig_sort_triggers_topologically(void);

/******************************************************************************
 *                                                                            *
 * Function: dc_trigger_deplist_release                                       *
 *                                                                            *
 * Purpose: releases trigger dependency list, removing it if necessary        *
 *                                                                            *
 ******************************************************************************/
static int	dc_trigger_deplist_release(TRX_DC_TRIGGER_DEPLIST *trigdep)
{
	if (0 == --trigdep->refcount)
	{
		trx_vector_ptr_destroy(&trigdep->dependencies);
		trx_hashset_remove_direct(&config->trigdeps, trigdep);
		return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_trigger_deplist_init                                          *
 *                                                                            *
 * Purpose: initializes trigger dependency list                               *
 *                                                                            *
 ******************************************************************************/
static void	dc_trigger_deplist_init(TRX_DC_TRIGGER_DEPLIST *trigdep, TRX_DC_TRIGGER *trigger)
{
	trigdep->refcount = 1;
	trigdep->trigger = trigger;
	trx_vector_ptr_create_ext(&trigdep->dependencies, __config_mem_malloc_func, __config_mem_realloc_func,
			__config_mem_free_func);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_trigger_deplist_reset                                         *
 *                                                                            *
 * Purpose: resets trigger dependency list to release memory allocated by     *
 *          dependencies vector                                               *
 *                                                                            *
 ******************************************************************************/
static void	dc_trigger_deplist_reset(TRX_DC_TRIGGER_DEPLIST *trigdep)
{
	trx_vector_ptr_destroy(&trigdep->dependencies);
	trx_vector_ptr_create_ext(&trigdep->dependencies, __config_mem_malloc_func, __config_mem_realloc_func,
			__config_mem_free_func);
}

static void	DCsync_trigdeps(trx_dbsync_t *sync)
{
	char			**row;
	trx_uint64_t		rowid;
	unsigned char		tag;

	TRX_DC_TRIGGER_DEPLIST	*trigdep_down, *trigdep_up;

	int			found, index, ret;
	trx_uint64_t		triggerid_down, triggerid_up;
	TRX_DC_TRIGGER		*trigger_up, *trigger_down;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		/* find trigdep_down pointer */

		TRX_STR2UINT64(triggerid_down, row[0]);
		if (NULL == (trigger_down = (TRX_DC_TRIGGER *)trx_hashset_search(&config->triggers, &triggerid_down)))
			continue;

		TRX_STR2UINT64(triggerid_up, row[1]);
		if (NULL == (trigger_up = (TRX_DC_TRIGGER *)trx_hashset_search(&config->triggers, &triggerid_up)))
			continue;

		trigdep_down = (TRX_DC_TRIGGER_DEPLIST *)DCfind_id(&config->trigdeps, triggerid_down, sizeof(TRX_DC_TRIGGER_DEPLIST), &found);
		if (0 == found)
			dc_trigger_deplist_init(trigdep_down, trigger_down);
		else
			trigdep_down->refcount++;

		trigdep_up = (TRX_DC_TRIGGER_DEPLIST *)DCfind_id(&config->trigdeps, triggerid_up, sizeof(TRX_DC_TRIGGER_DEPLIST), &found);
		if (0 == found)
			dc_trigger_deplist_init(trigdep_up, trigger_up);
		else
			trigdep_up->refcount++;

		trx_vector_ptr_append(&trigdep_down->dependencies, trigdep_up);
	}

	/* remove deleted trigger dependencies from buffer */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		TRX_STR2UINT64(triggerid_down, row[0]);
		if (NULL == (trigdep_down = (TRX_DC_TRIGGER_DEPLIST *)trx_hashset_search(&config->trigdeps,
				&triggerid_down)))
		{
			continue;
		}

		TRX_STR2UINT64(triggerid_up, row[1]);
		if (NULL != (trigdep_up = (TRX_DC_TRIGGER_DEPLIST *)trx_hashset_search(&config->trigdeps,
				&triggerid_up)))
		{
			dc_trigger_deplist_release(trigdep_up);
		}

		if (SUCCEED != dc_trigger_deplist_release(trigdep_down))
		{
			if (FAIL == (index = trx_vector_ptr_search(&trigdep_down->dependencies, &triggerid_up,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				continue;
			}

			if (1 == trigdep_down->dependencies.values_num)
				dc_trigger_deplist_reset(trigdep_down);
			else
				trx_vector_ptr_remove_noorder(&trigdep_down->dependencies, index);
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_functions(trx_dbsync_t *sync)
{
	char		**row;
	trx_uint64_t	rowid;
	unsigned char	tag;

	TRX_DC_ITEM	*item;
	TRX_DC_FUNCTION	*function;

	int		found, ret;
	trx_uint64_t	itemid, functionid, triggerid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(itemid, row[0]);
		TRX_STR2UINT64(functionid, row[1]);
		TRX_STR2UINT64(triggerid, row[4]);

		if (NULL == (item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &itemid)))
		{
			/* Item could have been created after we have selected them in the             */
			/* previous queries. However, we shall avoid the check for functions being the */
			/* same as in the trigger expression, because that is somewhat expensive, not  */
			/* 100% (think functions keeping their functionid, but changing their function */
			/* or parameters), and even if there is an inconsistency, we can live with it. */

			continue;
		}

		/* process function information */

		function = (TRX_DC_FUNCTION *)DCfind_id(&config->functions, functionid, sizeof(TRX_DC_FUNCTION), &found);

		if (1 == found && function->itemid != itemid)
		{
			TRX_DC_ITEM	*item_last;

			if (NULL != (item_last = trx_hashset_search(&config->items, &function->itemid)))
			{
				item_last->update_triggers = 1;
				if (NULL != item_last->triggers)
				{
					config->items.mem_free_func(item_last->triggers);
					item_last->triggers = NULL;
				}
			}
		}

		function->triggerid = triggerid;
		function->itemid = itemid;
		DCstrpool_replace(found, &function->function, row[2]);
		DCstrpool_replace(found, &function->parameter, row[3]);

		function->timer = (SUCCEED == is_time_function(function->function) ? 1 : 0);

		item->update_triggers = 1;
		if (NULL != item->triggers)
			item->triggers[0] = NULL;
	}

	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (function = (TRX_DC_FUNCTION *)trx_hashset_search(&config->functions, &rowid)))
			continue;

		if (NULL != (item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &function->itemid)))
		{
			item->update_triggers = 1;
			if (NULL != item->triggers)
			{
				config->items.mem_free_func(item->triggers);
				item->triggers = NULL;
			}
		}

		trx_strpool_release(function->function);
		trx_strpool_release(function->parameter);

		trx_hashset_remove_direct(&config->functions, function);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_regexp_remove_expression                                      *
 *                                                                            *
 * Purpose: removes expression from regexp                                    *
 *                                                                            *
 ******************************************************************************/
static TRX_DC_REGEXP	*dc_regexp_remove_expression(const char *regexp_name, trx_uint64_t expressionid)
{
	TRX_DC_REGEXP	*regexp, regexp_local;
	int		index;

	regexp_local.name = regexp_name;

	if (NULL == (regexp = (TRX_DC_REGEXP *)trx_hashset_search(&config->regexps, &regexp_local)))
		return NULL;

	if (FAIL == (index = trx_vector_uint64_search(&regexp->expressionids, expressionid,
			TRX_DEFAULT_UINT64_COMPARE_FUNC)))
	{
		return NULL;
	}

	trx_vector_uint64_remove_noorder(&regexp->expressionids, index);

	return regexp;
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_expressions                                               *
 *                                                                            *
 * Purpose: Updates expressions configuration cache                           *
 *                                                                            *
 * Parameters: result - [IN] the result of expressions database select        *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_expressions(trx_dbsync_t *sync)
{
	char			**row;
	trx_uint64_t		rowid;
	unsigned char		tag;
	trx_hashset_iter_t	iter;
	TRX_DC_EXPRESSION	*expression;
	TRX_DC_REGEXP		*regexp, regexp_local;
	trx_uint64_t		expressionid;
	int			found, ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(expressionid, row[1]);
		expression = (TRX_DC_EXPRESSION *)DCfind_id(&config->expressions, expressionid, sizeof(TRX_DC_EXPRESSION), &found);

		if (0 != found)
			dc_regexp_remove_expression(expression->regexp, expressionid);

		DCstrpool_replace(found, &expression->regexp, row[0]);
		DCstrpool_replace(found, &expression->expression, row[2]);
		TRX_STR2UCHAR(expression->type, row[3]);
		TRX_STR2UCHAR(expression->case_sensitive, row[5]);
		expression->delimiter = *row[4];

		regexp_local.name = row[0];

		if (NULL == (regexp = (TRX_DC_REGEXP *)trx_hashset_search(&config->regexps, &regexp_local)))
		{
			DCstrpool_replace(0, &regexp_local.name, row[0]);
			trx_vector_uint64_create_ext(&regexp_local.expressionids,
					__config_mem_malloc_func,
					__config_mem_realloc_func,
					__config_mem_free_func);

			regexp = (TRX_DC_REGEXP *)trx_hashset_insert(&config->regexps, &regexp_local, sizeof(TRX_DC_REGEXP));
		}

		trx_vector_uint64_append(&regexp->expressionids, expressionid);
	}

	/* remove regexps with no expressions related to it */
	trx_hashset_iter_reset(&config->regexps, &iter);

	while (NULL != (regexp = (TRX_DC_REGEXP *)trx_hashset_iter_next(&iter)))
	{
		if (0 < regexp->expressionids.values_num)
			continue;

		trx_strpool_release(regexp->name);
		trx_vector_uint64_destroy(&regexp->expressionids);
		trx_hashset_iter_remove(&iter);
	}

	/* remove unused expressions */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (expression = (TRX_DC_EXPRESSION *)trx_hashset_search(&config->expressions, &rowid)))
			continue;

		if (NULL != (regexp = dc_regexp_remove_expression(expression->regexp, expression->expressionid)))
		{
			if (0 == regexp->expressionids.values_num)
			{
				trx_strpool_release(regexp->name);
				trx_vector_uint64_destroy(&regexp->expressionids);
				trx_hashset_remove_direct(&config->regexps, regexp);
			}
		}

		trx_strpool_release(expression->expression);
		trx_strpool_release(expression->regexp);
		trx_hashset_remove_direct(&config->expressions, expression);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_actions                                                   *
 *                                                                            *
 * Purpose: Updates actions configuration cache                               *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - actionid                                                     *
 *           1 - eventsource                                                  *
 *           2 - evaltype                                                     *
 *           3 - formula                                                      *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_actions(trx_dbsync_t *sync)
{
	char		**row;
	trx_uint64_t	rowid;
	unsigned char	tag;
	trx_uint64_t	actionid;
	trx_dc_action_t	*action;
	int		found, ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(actionid, row[0]);
		action = (trx_dc_action_t *)DCfind_id(&config->actions, actionid, sizeof(trx_dc_action_t), &found);

		if (0 == found)
		{
			trx_vector_ptr_create_ext(&action->conditions, __config_mem_malloc_func,
					__config_mem_realloc_func, __config_mem_free_func);

			trx_vector_ptr_reserve(&action->conditions, 1);

			action->opflags = TRX_ACTION_OPCLASS_NONE;
		}

		TRX_STR2UCHAR(action->eventsource, row[1]);
		TRX_STR2UCHAR(action->evaltype, row[2]);

		DCstrpool_replace(found, &action->formula, row[3]);
	}

	/* remove deleted actions */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (action = (trx_dc_action_t *)trx_hashset_search(&config->actions, &rowid)))
			continue;

		trx_strpool_release(action->formula);
		trx_vector_ptr_destroy(&action->conditions);

		trx_hashset_remove_direct(&config->actions, action);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_action_ops                                                *
 *                                                                            *
 * Purpose: Updates action operation class flags in configuration cache       *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - actionid                                                     *
 *           1 - action operation class flags                                 *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_action_ops(trx_dbsync_t *sync)
{
	char		**row;
	trx_uint64_t	rowid;
	unsigned char	tag;
	trx_uint64_t	actionid;
	trx_dc_action_t	*action;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		TRX_STR2UINT64(actionid, row[0]);

		if (NULL == (action = (trx_dc_action_t *)trx_hashset_search(&config->actions, &actionid)))
			continue;

		action->opflags = atoi(row[1]);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_compare_action_conditions_by_type                             *
 *                                                                            *
 * Purpose: compare two action conditions by their type                       *
 *                                                                            *
 * Comments: This function is used to sort action conditions by type.         *
 *                                                                            *
 ******************************************************************************/
static int	dc_compare_action_conditions_by_type(const void *d1, const void *d2)
{
	trx_dc_action_condition_t	*c1 = *(trx_dc_action_condition_t **)d1;
	trx_dc_action_condition_t	*c2 = *(trx_dc_action_condition_t **)d2;

	TRX_RETURN_IF_NOT_EQUAL(c1->conditiontype, c2->conditiontype);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_action_conditions                                         *
 *                                                                            *
 * Purpose: Updates action conditions configuration cache                     *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - conditionid                                                  *
 *           1 - actionid                                                     *
 *           2 - conditiontype                                                *
 *           3 - operator                                                     *
 *           4 - value                                                        *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_action_conditions(trx_dbsync_t *sync)
{
	char				**row;
	trx_uint64_t			rowid;
	unsigned char			tag;
	trx_uint64_t			actionid, conditionid;
	trx_dc_action_t			*action;
	trx_dc_action_condition_t	*condition;
	int				found, i, index, ret;
	trx_vector_ptr_t		actions;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&actions);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(actionid, row[1]);

		if (NULL == (action = (trx_dc_action_t *)trx_hashset_search(&config->actions, &actionid)))
			continue;

		TRX_STR2UINT64(conditionid, row[0]);

		condition = (trx_dc_action_condition_t *)DCfind_id(&config->action_conditions, conditionid, sizeof(trx_dc_action_condition_t),
				&found);

		TRX_STR2UCHAR(condition->conditiontype, row[2]);
		TRX_STR2UCHAR(condition->op, row[3]);

		DCstrpool_replace(found, &condition->value, row[4]);
		DCstrpool_replace(found, &condition->value2, row[5]);

		if (0 == found)
		{
			condition->actionid = actionid;
			trx_vector_ptr_append(&action->conditions, condition);
		}

		if (CONDITION_EVAL_TYPE_AND_OR == action->evaltype)
			trx_vector_ptr_append(&actions, action);
	}

	/* remove deleted conditions */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (condition = (trx_dc_action_condition_t *)trx_hashset_search(&config->action_conditions, &rowid)))
			continue;

		if (NULL != (action = (trx_dc_action_t *)trx_hashset_search(&config->actions, &condition->actionid)))
		{
			if (FAIL != (index = trx_vector_ptr_search(&action->conditions, condition,
					TRX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				trx_vector_ptr_remove_noorder(&action->conditions, index);

				if (CONDITION_EVAL_TYPE_AND_OR == action->evaltype)
					trx_vector_ptr_append(&actions, action);
			}
		}

		trx_strpool_release(condition->value);
		trx_strpool_release(condition->value2);

		trx_hashset_remove_direct(&config->action_conditions, condition);
	}

	/* sort conditions by type */

	trx_vector_ptr_sort(&actions, TRX_DEFAULT_PTR_COMPARE_FUNC);
	trx_vector_ptr_uniq(&actions, TRX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < actions.values_num; i++)
	{
		action = (trx_dc_action_t *)actions.values[i];

		if (CONDITION_EVAL_TYPE_AND_OR == action->evaltype)
			trx_vector_ptr_sort(&action->conditions, dc_compare_action_conditions_by_type);
	}

	trx_vector_ptr_destroy(&actions);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_correlations                                              *
 *                                                                            *
 * Purpose: Updates correlations configuration cache                          *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - correlationid                                                *
 *           1 - name                                                         *
 *           2 - evaltype                                                     *
 *           3 - formula                                                      *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_correlations(trx_dbsync_t *sync)
{
	char			**row;
	trx_uint64_t		rowid;
	unsigned char		tag;
	trx_uint64_t		correlationid;
	trx_dc_correlation_t	*correlation;
	int			found, ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(correlationid, row[0]);

		correlation = (trx_dc_correlation_t *)DCfind_id(&config->correlations, correlationid, sizeof(trx_dc_correlation_t), &found);

		if (0 == found)
		{
			trx_vector_ptr_create_ext(&correlation->conditions, __config_mem_malloc_func,
					__config_mem_realloc_func, __config_mem_free_func);

			trx_vector_ptr_create_ext(&correlation->operations, __config_mem_malloc_func,
					__config_mem_realloc_func, __config_mem_free_func);
		}

		DCstrpool_replace(found, &correlation->name, row[1]);
		DCstrpool_replace(found, &correlation->formula, row[3]);

		TRX_STR2UCHAR(correlation->evaltype, row[2]);
	}

	/* remove deleted correlations */

	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (correlation = (trx_dc_correlation_t *)trx_hashset_search(&config->correlations, &rowid)))
			continue;

		trx_strpool_release(correlation->name);
		trx_strpool_release(correlation->formula);

		trx_vector_ptr_destroy(&correlation->conditions);
		trx_vector_ptr_destroy(&correlation->operations);

		trx_hashset_remove_direct(&config->correlations, correlation);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_corr_condition_get_size                                       *
 *                                                                            *
 * Purpose: get the actual size of correlation condition data depending on    *
 *          its type                                                          *
 *                                                                            *
 * Parameters: type - [IN] the condition type                                 *
 *                                                                            *
 * Return value: the size                                                     *
 *                                                                            *
 ******************************************************************************/
static size_t	dc_corr_condition_get_size(unsigned char type)
{
	switch (type)
	{
		case TRX_CORR_CONDITION_OLD_EVENT_TAG:
			/* break; is not missing here */
		case TRX_CORR_CONDITION_NEW_EVENT_TAG:
			return offsetof(trx_dc_corr_condition_t, data) + sizeof(trx_dc_corr_condition_tag_t);
		case TRX_CORR_CONDITION_NEW_EVENT_HOSTGROUP:
			return offsetof(trx_dc_corr_condition_t, data) + sizeof(trx_dc_corr_condition_group_t);
		case TRX_CORR_CONDITION_EVENT_TAG_PAIR:
			return offsetof(trx_dc_corr_condition_t, data) + sizeof(trx_dc_corr_condition_tag_pair_t);
		case TRX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
			/* break; is not missing here */
		case TRX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
			return offsetof(trx_dc_corr_condition_t, data) + sizeof(trx_dc_corr_condition_tag_value_t);
	}

	THIS_SHOULD_NEVER_HAPPEN;
	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_corr_condition_init_data                                      *
 *                                                                            *
 * Purpose: initializes correlation condition data from database row          *
 *                                                                            *
 * Parameters: condition - [IN] the condition to initialize                   *
 *             found     - [IN] 0 - new condition, 1 - cached condition       *
 *             row       - [IN] the database row containing condition data    *
 *                                                                            *
 ******************************************************************************/
static void	dc_corr_condition_init_data(trx_dc_corr_condition_t *condition, int found,  DB_ROW row)
{
	if (TRX_CORR_CONDITION_OLD_EVENT_TAG == condition->type || TRX_CORR_CONDITION_NEW_EVENT_TAG == condition->type)
	{
		DCstrpool_replace(found, &condition->data.tag.tag, row[0]);
		return;
	}

	row++;

	if (TRX_CORR_CONDITION_OLD_EVENT_TAG_VALUE == condition->type ||
			TRX_CORR_CONDITION_NEW_EVENT_TAG_VALUE == condition->type)
	{
		DCstrpool_replace(found, &condition->data.tag_value.tag, row[0]);
		DCstrpool_replace(found, &condition->data.tag_value.value, row[1]);
		TRX_STR2UCHAR(condition->data.tag_value.op, row[2]);
		return;
	}

	row += 3;

	if (TRX_CORR_CONDITION_NEW_EVENT_HOSTGROUP == condition->type)
	{
		TRX_STR2UINT64(condition->data.group.groupid, row[0]);
		TRX_STR2UCHAR(condition->data.group.op, row[1]);
		return;
	}

	row += 2;

	if (TRX_CORR_CONDITION_EVENT_TAG_PAIR == condition->type)
	{
		DCstrpool_replace(found, &condition->data.tag_pair.oldtag, row[0]);
		DCstrpool_replace(found, &condition->data.tag_pair.newtag, row[1]);
		return;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: corr_condition_free_data                                         *
 *                                                                            *
 * Purpose: frees correlation condition data                                  *
 *                                                                            *
 * Parameters: condition - [IN] the condition                                 *
 *                                                                            *
 ******************************************************************************/
static void	corr_condition_free_data(trx_dc_corr_condition_t *condition)
{
	switch (condition->type)
	{
		case TRX_CORR_CONDITION_OLD_EVENT_TAG:
			/* break; is not missing here */
		case TRX_CORR_CONDITION_NEW_EVENT_TAG:
			trx_strpool_release(condition->data.tag.tag);
			break;
		case TRX_CORR_CONDITION_EVENT_TAG_PAIR:
			trx_strpool_release(condition->data.tag_pair.oldtag);
			trx_strpool_release(condition->data.tag_pair.newtag);
			break;
		case TRX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
			/* break; is not missing here */
		case TRX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
			trx_strpool_release(condition->data.tag_value.tag);
			trx_strpool_release(condition->data.tag_value.value);
			break;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_compare_corr_conditions_by_type                               *
 *                                                                            *
 * Purpose: compare two correlation conditions by their type                  *
 *                                                                            *
 * Comments: This function is used to sort correlation conditions by type.    *
 *                                                                            *
 ******************************************************************************/
static int	dc_compare_corr_conditions_by_type(const void *d1, const void *d2)
{
	trx_dc_corr_condition_t	*c1 = *(trx_dc_corr_condition_t **)d1;
	trx_dc_corr_condition_t	*c2 = *(trx_dc_corr_condition_t **)d2;

	TRX_RETURN_IF_NOT_EQUAL(c1->type, c2->type);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_corr_conditions                                           *
 *                                                                            *
 * Purpose: Updates correlation conditions configuration cache                *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - corr_conditionid                                             *
 *           1 - correlationid                                                *
 *           2 - type                                                         *
 *           3 - corr_condition_tag.tag                                       *
 *           4 - corr_condition_tagvalue.tag                                  *
 *           5 - corr_condition_tagvalue.value                                *
 *           6 - corr_condition_tagvalue.operator                             *
 *           7 - corr_condition_group.groupid                                 *
 *           8 - corr_condition_group.operator                                *
 *           9 - corr_condition_tagpair.oldtag                                *
 *          10 - corr_condition_tagpair.newtag                                *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_corr_conditions(trx_dbsync_t *sync)
{
	char			**row;
	trx_uint64_t		rowid;
	unsigned char		tag;
	trx_uint64_t		conditionid, correlationid;
	trx_dc_corr_condition_t	*condition;
	trx_dc_correlation_t	*correlation;
	int			found, ret, i, index;
	unsigned char		type;
	size_t			condition_size;
	trx_vector_ptr_t	correlations;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&correlations);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(correlationid, row[1]);

		if (NULL == (correlation = (trx_dc_correlation_t *)trx_hashset_search(&config->correlations, &correlationid)))
			continue;

		TRX_STR2UINT64(conditionid, row[0]);
		TRX_STR2UCHAR(type, row[2]);

		condition_size = dc_corr_condition_get_size(type);
		condition = (trx_dc_corr_condition_t *)DCfind_id(&config->corr_conditions, conditionid, condition_size, &found);

		condition->correlationid = correlationid;
		condition->type = type;
		dc_corr_condition_init_data(condition, found, row + 3);

		if (0 == found)
			trx_vector_ptr_append(&correlation->conditions, condition);

		/* sort the conditions later */
		if (CONDITION_EVAL_TYPE_AND_OR == correlation->evaltype)
			trx_vector_ptr_append(&correlations, correlation);
	}

	/* remove deleted correlation conditions */

	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (condition = (trx_dc_corr_condition_t *)trx_hashset_search(&config->corr_conditions, &rowid)))
			continue;

		/* remove condition from correlation->conditions vector */
		if (NULL != (correlation = (trx_dc_correlation_t *)trx_hashset_search(&config->correlations, &condition->correlationid)))
		{
			if (FAIL != (index = trx_vector_ptr_search(&correlation->conditions, condition,
					TRX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				/* sort the conditions later */
				if (CONDITION_EVAL_TYPE_AND_OR == correlation->evaltype)
					trx_vector_ptr_append(&correlations, correlation);

				trx_vector_ptr_remove_noorder(&correlation->conditions, index);
			}
		}

		corr_condition_free_data(condition);
		trx_hashset_remove_direct(&config->corr_conditions, condition);
	}

	/* sort conditions by type */

	trx_vector_ptr_sort(&correlations, TRX_DEFAULT_PTR_COMPARE_FUNC);
	trx_vector_ptr_uniq(&correlations, TRX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < correlations.values_num; i++)
	{
		correlation = (trx_dc_correlation_t *)correlations.values[i];
		trx_vector_ptr_sort(&correlation->conditions, dc_compare_corr_conditions_by_type);
	}

	trx_vector_ptr_destroy(&correlations);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_corr_operations                                           *
 *                                                                            *
 * Purpose: Updates correlation operations configuration cache                *
 *                                                                            *
 * Parameters: result - [IN] the result of correlation operations database    *
 *                           select                                           *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - corr_operationid                                             *
 *           1 - correlationid                                                *
 *           2 - type                                                         *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_corr_operations(trx_dbsync_t *sync)
{
	char			**row;
	trx_uint64_t		rowid;
	unsigned char		tag;
	trx_uint64_t		operationid, correlationid;
	trx_dc_corr_operation_t	*operation;
	trx_dc_correlation_t	*correlation;
	int			found, ret, index;
	unsigned char		type;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(correlationid, row[1]);

		if (NULL == (correlation = (trx_dc_correlation_t *)trx_hashset_search(&config->correlations, &correlationid)))
			continue;

		TRX_STR2UINT64(operationid, row[0]);
		TRX_STR2UCHAR(type, row[2]);

		operation = (trx_dc_corr_operation_t *)DCfind_id(&config->corr_operations, operationid, sizeof(trx_dc_corr_operation_t), &found);

		operation->type = type;

		if (0 == found)
		{
			operation->correlationid = correlationid;
			trx_vector_ptr_append(&correlation->operations, operation);
		}
	}

	/* remove deleted correlation operations */

	/* remove deleted actions */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (operation = (trx_dc_corr_operation_t *)trx_hashset_search(&config->corr_operations, &rowid)))
			continue;

		/* remove operation from correlation->conditions vector */
		if (NULL != (correlation = (trx_dc_correlation_t *)trx_hashset_search(&config->correlations, &operation->correlationid)))
		{
			if (FAIL != (index = trx_vector_ptr_search(&correlation->operations, operation,
					TRX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				trx_vector_ptr_remove_noorder(&correlation->operations, index);
			}
		}
		trx_hashset_remove_direct(&config->corr_operations, operation);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static int	dc_compare_hgroups(const void *d1, const void *d2)
{
	const trx_dc_hostgroup_t	*g1 = *((const trx_dc_hostgroup_t **)d1);
	const trx_dc_hostgroup_t	*g2 = *((const trx_dc_hostgroup_t **)d2);

	return strcmp(g1->name, g2->name);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_hostgroups                                                *
 *                                                                            *
 * Purpose: Updates host groups configuration cache                           *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - groupid                                                      *
 *           1 - name                                                         *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_hostgroups(trx_dbsync_t *sync)
{
	char			**row;
	trx_uint64_t		rowid;
	unsigned char		tag;
	trx_uint64_t		groupid;
	trx_dc_hostgroup_t	*group;
	int			found, ret, index;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(groupid, row[0]);

		group = (trx_dc_hostgroup_t *)DCfind_id(&config->hostgroups, groupid, sizeof(trx_dc_hostgroup_t), &found);

		if (0 == found)
		{
			group->flags = TRX_DC_HOSTGROUP_FLAGS_NONE;
			trx_vector_ptr_append(&config->hostgroups_name, group);

			trx_hashset_create_ext(&group->hostids, 0, TRX_DEFAULT_UINT64_HASH_FUNC,
					TRX_DEFAULT_UINT64_COMPARE_FUNC, NULL, __config_mem_malloc_func,
					__config_mem_realloc_func, __config_mem_free_func);
		}

		DCstrpool_replace(found, &group->name, row[1]);
	}

	/* remove deleted host groups */

	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (group = (trx_dc_hostgroup_t *)trx_hashset_search(&config->hostgroups, &rowid)))
			continue;

		if (FAIL != (index = trx_vector_ptr_search(&config->hostgroups_name, group, TRX_DEFAULT_PTR_COMPARE_FUNC)))
			trx_vector_ptr_remove_noorder(&config->hostgroups_name, index);

		if (TRX_DC_HOSTGROUP_FLAGS_NONE != group->flags)
			trx_vector_uint64_destroy(&group->nested_groupids);

		trx_strpool_release(group->name);
		trx_hashset_destroy(&group->hostids);
		trx_hashset_remove_direct(&config->hostgroups, group);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_trigger_tags                                              *
 *                                                                            *
 * Purpose: Updates trigger tags in configuration cache                       *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - triggertagid                                                 *
 *           1 - triggerid                                                    *
 *           2 - tag                                                          *
 *           3 - value                                                        *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_trigger_tags(trx_dbsync_t *sync)
{
	char			**row;
	trx_uint64_t		rowid;
	unsigned char		tag;
	int			found, ret, index;
	trx_uint64_t		triggerid, triggertagid;
	TRX_DC_TRIGGER		*trigger;
	trx_dc_trigger_tag_t	*trigger_tag;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(triggerid, row[1]);

		if (NULL == (trigger = (TRX_DC_TRIGGER *)trx_hashset_search(&config->triggers, &triggerid)))
			continue;

		TRX_STR2UINT64(triggertagid, row[0]);

		trigger_tag = (trx_dc_trigger_tag_t *)DCfind_id(&config->trigger_tags, triggertagid, sizeof(trx_dc_trigger_tag_t), &found);
		DCstrpool_replace(found, &trigger_tag->tag, row[2]);
		DCstrpool_replace(found, &trigger_tag->value, row[3]);

		if (0 == found)
		{
			trigger_tag->triggerid = triggerid;
			trx_vector_ptr_append(&trigger->tags, trigger_tag);
		}
	}

	/* remove unused trigger tags */

	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (trigger_tag = (trx_dc_trigger_tag_t *)trx_hashset_search(&config->trigger_tags, &rowid)))
			continue;

		if (NULL != (trigger = (TRX_DC_TRIGGER *)trx_hashset_search(&config->triggers, &trigger_tag->triggerid)))
		{
			if (FAIL != (index = trx_vector_ptr_search(&trigger->tags, trigger_tag,
					TRX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				trx_vector_ptr_remove_noorder(&trigger->tags, index);

				/* recreate empty tags vector to release used memory */
				if (0 == trigger->tags.values_num)
				{
					trx_vector_ptr_destroy(&trigger->tags);
					trx_vector_ptr_create_ext(&trigger->tags, __config_mem_malloc_func,
							__config_mem_realloc_func, __config_mem_free_func);
				}
			}
		}

		trx_strpool_release(trigger_tag->tag);
		trx_strpool_release(trigger_tag->value);

		trx_hashset_remove_direct(&config->trigger_tags, trigger_tag);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_host_tags                                                 *
 *                                                                            *
 * Purpose: Updates host tags in configuration cache                          *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - hosttagid                                                    *
 *           1 - hostid                                                       *
 *           2 - tag                                                          *
 *           3 - value                                                        *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_host_tags(trx_dbsync_t *sync)
{
	char		**row;
	trx_uint64_t	rowid;
	unsigned char	tag;

	trx_dc_host_tag_t		*host_tag;
	trx_dc_host_tag_index_t		*host_tag_index_entry;

	int		found, index, ret;
	trx_uint64_t	hosttagid, hostid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(hosttagid, row[0]);
		TRX_STR2UINT64(hostid, row[1]);

		host_tag = (trx_dc_host_tag_t *)DCfind_id(&config->host_tags, hosttagid,
				sizeof(trx_dc_host_tag_t), &found);

		/* store new information in host_tag structure */
		host_tag->hostid = hostid;
		DCstrpool_replace(found, &host_tag->tag, row[2]);
		DCstrpool_replace(found, &host_tag->value, row[3]);

		/* update host_tags_index*/
		if (tag == TRX_DBSYNC_ROW_ADD)
		{
			host_tag_index_entry = (trx_dc_host_tag_index_t *)DCfind_id(&config->host_tags_index, hostid,
					sizeof(trx_dc_host_tag_index_t), &found);

			if (0 == found)
			{
				trx_vector_ptr_create_ext(&host_tag_index_entry->tags, __config_mem_malloc_func,
						__config_mem_realloc_func, __config_mem_free_func);
			}

			trx_vector_ptr_append(&host_tag_index_entry->tags, host_tag);
		}
	}

	/* remove deleted host tags from buffer */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (host_tag = (trx_dc_host_tag_t *)trx_hashset_search(&config->host_tags, &rowid)))
			continue;

		/* update host_tags_index*/
		host_tag_index_entry = (trx_dc_host_tag_index_t *)trx_hashset_search(&config->host_tags_index,
				&host_tag->hostid);

		if (NULL != host_tag_index_entry)
		{
			if (FAIL != (index = trx_vector_ptr_search(&host_tag_index_entry->tags, host_tag,
					TRX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				trx_vector_ptr_remove(&host_tag_index_entry->tags, index);
			}

			/* remove index entry if it's empty */
			if (0 == host_tag_index_entry->tags.values_num)
			{
				trx_vector_ptr_destroy(&host_tag_index_entry->tags);
				trx_hashset_remove_direct(&config->host_tags_index, host_tag_index_entry);
			}
		}

		/* clear host_tag structure */
		trx_strpool_release(host_tag->tag);
		trx_strpool_release(host_tag->value);

		trx_hashset_remove_direct(&config->host_tags, host_tag);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_compare_item_preproc_by_step                                  *
 *                                                                            *
 * Purpose: compare two item preprocessing operations by step                 *
 *                                                                            *
 * Comments: This function is used to sort correlation conditions by type.    *
 *                                                                            *
 ******************************************************************************/
static int	dc_compare_preprocops_by_step(const void *d1, const void *d2)
{
	trx_dc_preproc_op_t	*p1 = *(trx_dc_preproc_op_t **)d1;
	trx_dc_preproc_op_t	*p2 = *(trx_dc_preproc_op_t **)d2;

	TRX_RETURN_IF_NOT_EQUAL(p1->step, p2->step);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_item_preproc                                              *
 *                                                                            *
 * Purpose: Updates item preprocessing steps in configuration cache           *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - item_preprocid                                               *
 *           1 - itemid                                                       *
 *           2 - type                                                         *
 *           3 - params                                                       *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_item_preproc(trx_dbsync_t *sync, int timestamp)
{
	char			**row;
	trx_uint64_t		rowid;
	unsigned char		tag;
	trx_uint64_t		item_preprocid, itemid;
	int			found, ret, i, index;
	TRX_DC_PREPROCITEM	*preprocitem = NULL;
	trx_dc_preproc_op_t	*op;
	trx_vector_ptr_t	items;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&items);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(itemid, row[1]);

		if (NULL == preprocitem || itemid != preprocitem->itemid)
		{
			if (NULL == (preprocitem = (TRX_DC_PREPROCITEM *)trx_hashset_search(&config->preprocitems, &itemid)))
			{
				TRX_DC_PREPROCITEM	preprocitem_local;

				preprocitem_local.itemid = itemid;

				preprocitem = (TRX_DC_PREPROCITEM *)trx_hashset_insert(&config->preprocitems, &preprocitem_local,
						sizeof(preprocitem_local));

				trx_vector_ptr_create_ext(&preprocitem->preproc_ops, __config_mem_malloc_func,
						__config_mem_realloc_func, __config_mem_free_func);
			}

			preprocitem->update_time = timestamp;
		}

		TRX_STR2UINT64(item_preprocid, row[0]);

		op = (trx_dc_preproc_op_t *)DCfind_id(&config->preprocops, item_preprocid, sizeof(trx_dc_preproc_op_t), &found);

		TRX_STR2UCHAR(op->type, row[2]);
		DCstrpool_replace(found, &op->params, row[3]);
		op->step = atoi(row[4]);
		op->error_handler = atoi(row[6]);
		DCstrpool_replace(found, &op->error_handler_params, row[7]);

		if (0 == found)
		{
			op->itemid = itemid;
			trx_vector_ptr_append(&preprocitem->preproc_ops, op);
		}

		trx_vector_ptr_append(&items, preprocitem);
	}

	/* remove deleted item preprocessing operations */

	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (op = (trx_dc_preproc_op_t *)trx_hashset_search(&config->preprocops, &rowid)))
			continue;

		if (NULL != (preprocitem = (TRX_DC_PREPROCITEM *)trx_hashset_search(&config->preprocitems, &op->itemid)))
		{
			if (FAIL != (index = trx_vector_ptr_search(&preprocitem->preproc_ops, op,
					TRX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				trx_vector_ptr_remove_noorder(&preprocitem->preproc_ops, index);
				trx_vector_ptr_append(&items, preprocitem);
			}
		}

		trx_strpool_release(op->params);
		trx_strpool_release(op->error_handler_params);
		trx_hashset_remove_direct(&config->preprocops, op);
	}

	/* sort item preprocessing operations by step */

	trx_vector_ptr_sort(&items, TRX_DEFAULT_PTR_COMPARE_FUNC);
	trx_vector_ptr_uniq(&items, TRX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < items.values_num; i++)
	{
		preprocitem = (TRX_DC_PREPROCITEM *)items.values[i];

		if (0 == preprocitem->preproc_ops.values_num)
		{
			trx_vector_ptr_destroy(&preprocitem->preproc_ops);
			trx_hashset_remove_direct(&config->preprocitems, preprocitem);
		}
		else
			trx_vector_ptr_sort(&preprocitem->preproc_ops, dc_compare_preprocops_by_step);
	}

	trx_vector_ptr_destroy(&items);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_hostgroup_hosts                                           *
 *                                                                            *
 * Purpose: Updates group hosts in configuration cache                        *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - groupid                                                      *
 *           1 - hostid                                                       *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_hostgroup_hosts(trx_dbsync_t *sync)
{
	char			**row;
	trx_uint64_t		rowid;
	unsigned char		tag;

	trx_dc_hostgroup_t	*group = NULL;

	int			ret;
	trx_uint64_t		last_groupid = 0, groupid, hostid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = trx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		config->maintenance_update = TRX_MAINTENANCE_UPDATE_TRUE;

		/* removed rows will be always added at the end */
		if (TRX_DBSYNC_ROW_REMOVE == tag)
			break;

		TRX_STR2UINT64(groupid, row[0]);

		if (last_groupid != groupid)
		{
			group = (trx_dc_hostgroup_t *)trx_hashset_search(&config->hostgroups, &groupid);
			last_groupid = groupid;
		}

		if (NULL == group)
			continue;

		TRX_STR2UINT64(hostid, row[1]);
		trx_hashset_insert(&group->hostids, &hostid, sizeof(hostid));
	}

	/* remove deleted group hostids from cache */
	for (; SUCCEED == ret; ret = trx_dbsync_next(sync, &rowid, &row, &tag))
	{
		TRX_STR2UINT64(groupid, row[0]);

		if (NULL == (group = (trx_dc_hostgroup_t *)trx_hashset_search(&config->hostgroups, &groupid)))
			continue;

		TRX_STR2UINT64(hostid, row[1]);
		trx_hashset_remove(&group->hostids, &hostid);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_trigger_update_topology                                       *
 *                                                                            *
 * Purpose: updates trigger topology after trigger dependency changes         *
 *                                                                            *
 ******************************************************************************/
static void	dc_trigger_update_topology(void)
{
	trx_hashset_iter_t	iter;
	TRX_DC_TRIGGER		*trigger;

	trx_hashset_iter_reset(&config->triggers, &iter);
	while (NULL != (trigger = (TRX_DC_TRIGGER *)trx_hashset_iter_next(&iter)))
		trigger->topoindex = 1;

	DCconfig_sort_triggers_topologically();
}

static int	trx_default_ptr_pair_ptr_compare_func(const void *d1, const void *d2)
{
	const trx_ptr_pair_t	*p1 = (const trx_ptr_pair_t *)d1;
	const trx_ptr_pair_t	*p2 = (const trx_ptr_pair_t *)d2;

	TRX_RETURN_IF_NOT_EQUAL(p1->first, p2->first);
	TRX_RETURN_IF_NOT_EQUAL(p1->second, p2->second);

	return 0;
}

#define TRX_TIMER_DELAY		30

/******************************************************************************
 *                                                                            *
 * Function: dc_timer_calculate_nextcheck                                     *
 *                                                                            *
 * Purpose: calculates next check for timer queue item                        *
 *                                                                            *
 ******************************************************************************/
static int	dc_timer_calculate_nextcheck(time_t now, trx_uint64_t seed)
{
	int	nextcheck;

	nextcheck = TRX_TIMER_DELAY * (int)(now / (time_t)TRX_TIMER_DELAY) +
			(int)(seed % (trx_uint64_t)TRX_TIMER_DELAY);

	while (nextcheck <= now)
		nextcheck += TRX_TIMER_DELAY;

	return nextcheck;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_trigger_update_cache                                          *
 *                                                                            *
 * Purpose: updates trigger related cache data;                               *
 *              1) time triggers assigned to timer processes                  *
 *              2) trigger functionality (if it uses contain disabled         *
 *                 items/hosts)                                               *
 *              3) list of triggers each item is used by                      *
 *                                                                            *
 ******************************************************************************/
static void	dc_trigger_update_cache(void)
{
	trx_hashset_iter_t	iter;
	TRX_DC_TRIGGER		*trigger;
	TRX_DC_FUNCTION		*function;
	TRX_DC_ITEM		*item;
	int			i, j, k, now;
	trx_ptr_pair_t		itemtrig;
	trx_vector_ptr_pair_t	itemtrigs;
	TRX_DC_HOST		*host;

	trx_hashset_iter_reset(&config->triggers, &iter);
	while (NULL != (trigger = (TRX_DC_TRIGGER *)trx_hashset_iter_next(&iter)))
	{
		trigger->functional = TRIGGER_FUNCTIONAL_TRUE;
		trigger->timer = TRX_TRIGGER_TIMER_UNKNOWN;
	}

	trx_vector_ptr_pair_create(&itemtrigs);
	trx_hashset_iter_reset(&config->functions, &iter);
	while (NULL != (function = (TRX_DC_FUNCTION *)trx_hashset_iter_next(&iter)))
	{

		if (NULL == (item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &function->itemid)) ||
				NULL == (trigger = (TRX_DC_TRIGGER *)trx_hashset_search(&config->triggers, &function->triggerid)))
		{
			continue;
		}

		/* cache item - trigger link */
		if (0 != item->update_triggers)
		{
			itemtrig.first = item;
			itemtrig.second = trigger;
			trx_vector_ptr_pair_append(&itemtrigs, itemtrig);
		}

		/* disable functionality for triggers with expression containing */
		/* disabled or not monitored items                               */

		if (TRIGGER_FUNCTIONAL_FALSE == trigger->functional)
			continue;

		if (ITEM_STATUS_DISABLED == item->status ||
				(NULL == (host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &item->hostid)) ||
						HOST_STATUS_NOT_MONITORED == host->status))
		{
			trigger->functional = TRIGGER_FUNCTIONAL_FALSE;
		}

		if (1 == function->timer)
			trigger->timer = TRX_TRIGGER_TIMER_QUEUE;
	}

	trx_vector_ptr_pair_sort(&itemtrigs, trx_default_ptr_pair_ptr_compare_func);
	trx_vector_ptr_pair_uniq(&itemtrigs, trx_default_ptr_pair_ptr_compare_func);

	/* update links from items to triggers */
	for (i = 0; i < itemtrigs.values_num; i++)
	{
		for (j = i + 1; j < itemtrigs.values_num; j++)
		{
			if (itemtrigs.values[i].first != itemtrigs.values[j].first)
				break;
		}

		item = (TRX_DC_ITEM *)itemtrigs.values[i].first;
		item->update_triggers = 0;
		item->triggers = (TRX_DC_TRIGGER **)config->items.mem_realloc_func(item->triggers, (j - i + 1) * sizeof(TRX_DC_TRIGGER *));

		for (k = i; k < j; k++)
			item->triggers[k - i] = (TRX_DC_TRIGGER *)itemtrigs.values[k].second;

		item->triggers[j - i] = NULL;

		i = j - 1;
	}

	trx_vector_ptr_pair_destroy(&itemtrigs);

	/* add triggers to timer queue */
	now = time(NULL);
	trx_binary_heap_clear(&config->timer_queue);
	trx_hashset_iter_reset(&config->triggers, &iter);
	while (NULL != (trigger = (TRX_DC_TRIGGER *)trx_hashset_iter_next(&iter)))
	{
		trx_binary_heap_elem_t	elem;

		if (TRIGGER_STATUS_DISABLED == trigger->status)
			continue;

		if (TRIGGER_FUNCTIONAL_FALSE == trigger->functional)
			continue;

		if (TRX_TRIGGER_TIMER_QUEUE != trigger->timer)
			continue;

		trigger->nextcheck = dc_timer_calculate_nextcheck(now, trigger->triggerid);
		elem.key = trigger->triggerid;
		elem.data = (void *)trigger;
		trx_binary_heap_insert(&config->timer_queue, &elem);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_hostgroups_update_cache                                       *
 *                                                                            *
 * Purpose: updates hostgroup name index and resets nested group lists        *
 *                                                                            *
 ******************************************************************************/
static void	dc_hostgroups_update_cache(void)
{
	trx_hashset_iter_t	iter;
	trx_dc_hostgroup_t	*group;

	trx_vector_ptr_sort(&config->hostgroups_name, dc_compare_hgroups);

	trx_hashset_iter_reset(&config->hostgroups, &iter);
	while (NULL != (group = (trx_dc_hostgroup_t *)trx_hashset_iter_next(&iter)))
	{
		if (TRX_DC_HOSTGROUP_FLAGS_NONE != group->flags)
		{
			group->flags = TRX_DC_HOSTGROUP_FLAGS_NONE;
			trx_vector_uint64_destroy(&group->nested_groupids);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_configuration                                             *
 *                                                                            *
 * Purpose: Synchronize configuration data from database                      *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
void	DCsync_configuration(unsigned char mode)
{
	int		i, flags;
	double		sec, csec, hsec, hisec, htsec, gmsec, hmsec, ifsec, isec, tsec, dsec, fsec, expr_sec, csec2,
			hsec2, hisec2, htsec2, gmsec2, hmsec2, ifsec2, isec2, tsec2, dsec2, fsec2, expr_sec2,
			action_sec, action_sec2, action_op_sec, action_op_sec2, action_condition_sec,
			action_condition_sec2, trigger_tag_sec, trigger_tag_sec2, host_tag_sec, host_tag_sec2,
			correlation_sec, correlation_sec2, corr_condition_sec, corr_condition_sec2, corr_operation_sec,
			corr_operation_sec2, hgroups_sec, hgroups_sec2, itempp_sec, itempp_sec2, total, total2,
			update_sec, maintenance_sec, maintenance_sec2;

	trx_dbsync_t	config_sync, hosts_sync, hi_sync, htmpl_sync, gmacro_sync, hmacro_sync, if_sync, items_sync,
			template_items_sync, prototype_items_sync, triggers_sync, tdep_sync, func_sync, expr_sync,
			action_sync, action_op_sync, action_condition_sync, trigger_tag_sync, host_tag_sync,
			correlation_sync, corr_condition_sync, corr_operation_sync, hgroups_sync, itempp_sync,
			maintenance_sync, maintenance_period_sync, maintenance_tag_sync, maintenance_group_sync,
			maintenance_host_sync, hgroup_host_sync;

	double		autoreg_csec, autoreg_csec2;
	trx_dbsync_t	autoreg_config_sync;
	trx_uint64_t	update_flags = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_dbsync_init_env(config);

	/* global configuration must be synchronized directly with database */
	trx_dbsync_init(&config_sync, TRX_DBSYNC_INIT);
	trx_dbsync_init(&autoreg_config_sync, mode);
	trx_dbsync_init(&hosts_sync, mode);
	trx_dbsync_init(&hi_sync, mode);
	trx_dbsync_init(&htmpl_sync, mode);
	trx_dbsync_init(&gmacro_sync, mode);
	trx_dbsync_init(&hmacro_sync, mode);
	trx_dbsync_init(&if_sync, mode);
	trx_dbsync_init(&items_sync, mode);
	trx_dbsync_init(&template_items_sync, mode);
	trx_dbsync_init(&prototype_items_sync, mode);
	trx_dbsync_init(&triggers_sync, mode);
	trx_dbsync_init(&tdep_sync, mode);
	trx_dbsync_init(&func_sync, mode);
	trx_dbsync_init(&expr_sync, mode);
	trx_dbsync_init(&action_sync, mode);

	/* Action operation sync produces virtual rows with two columns - actionid, opflags. */
	/* Because of this it cannot return the original database select and must always be  */
	/* initialized in update mode.                                                       */
	trx_dbsync_init(&action_op_sync, TRX_DBSYNC_UPDATE);

	trx_dbsync_init(&action_condition_sync, mode);
	trx_dbsync_init(&trigger_tag_sync, mode);
	trx_dbsync_init(&host_tag_sync, mode);
	trx_dbsync_init(&correlation_sync, mode);
	trx_dbsync_init(&corr_condition_sync, mode);
	trx_dbsync_init(&corr_operation_sync, mode);
	trx_dbsync_init(&hgroups_sync, mode);
	trx_dbsync_init(&hgroup_host_sync, mode);
	trx_dbsync_init(&itempp_sync, mode);

	trx_dbsync_init(&maintenance_sync, mode);
	trx_dbsync_init(&maintenance_period_sync, mode);
	trx_dbsync_init(&maintenance_tag_sync, mode);
	trx_dbsync_init(&maintenance_group_sync, mode);
	trx_dbsync_init(&maintenance_host_sync, mode);

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_config(&config_sync))
		goto out;
	csec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_autoreg_psk(&autoreg_config_sync))
		goto out;
	autoreg_csec = trx_time() - sec;

	/* sync global configuration settings */
	START_SYNC;
	sec = trx_time();
	DCsync_config(&config_sync, &flags);
	csec2 = trx_time() - sec;

	sec = trx_time();
	DCsync_autoreg_config(&autoreg_config_sync);	/* must be done in the same cache locking with config sync */
	autoreg_csec2 = trx_time() - sec;
	FINISH_SYNC;

	/* sync macro related data, to support macro resolving during configuration sync */

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_host_templates(&htmpl_sync))
		goto out;
	htsec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_global_macros(&gmacro_sync))
		goto out;
	gmsec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_host_macros(&hmacro_sync))
		goto out;
	hmsec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_host_tags(&host_tag_sync))
		goto out;
	host_tag_sec = trx_time() - sec;

	START_SYNC;
	sec = trx_time();
	DCsync_htmpls(&htmpl_sync);
	htsec2 = trx_time() - sec;

	sec = trx_time();
	DCsync_gmacros(&gmacro_sync);
	gmsec2 = trx_time() - sec;

	sec = trx_time();
	DCsync_hmacros(&hmacro_sync);
	hmsec2 = trx_time() - sec;

	sec = trx_time();
	DCsync_host_tags(&host_tag_sync);
	host_tag_sec2 = trx_time() - sec;
	FINISH_SYNC;

	/* sync host data to support host lookups when resolving macros during configuration sync */

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_hosts(&hosts_sync))
		goto out;
	hsec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_host_inventory(&hi_sync))
		goto out;
	hisec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_host_groups(&hgroups_sync))
		goto out;
	if (FAIL == trx_dbsync_compare_host_group_hosts(&hgroup_host_sync))
		goto out;
	hgroups_sec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_maintenances(&maintenance_sync))
		goto out;
	if (FAIL == trx_dbsync_compare_maintenance_tags(&maintenance_tag_sync))
		goto out;
	if (FAIL == trx_dbsync_compare_maintenance_periods(&maintenance_period_sync))
		goto out;
	if (FAIL == trx_dbsync_compare_maintenance_groups(&maintenance_group_sync))
		goto out;
	if (FAIL == trx_dbsync_compare_maintenance_hosts(&maintenance_host_sync))
		goto out;
	maintenance_sec = trx_time() - sec;

	START_SYNC;
	sec = trx_time();
	DCsync_hosts(&hosts_sync);
	hsec2 = trx_time() - sec;

	sec = trx_time();
	DCsync_host_inventory(&hi_sync);
	hisec2 = trx_time() - sec;

	sec = trx_time();
	DCsync_hostgroups(&hgroups_sync);
	DCsync_hostgroup_hosts(&hgroup_host_sync);
	hgroups_sec2 = trx_time() - sec;

	sec = trx_time();
	DCsync_maintenances(&maintenance_sync);
	DCsync_maintenance_tags(&maintenance_tag_sync);
	DCsync_maintenance_groups(&maintenance_group_sync);
	DCsync_maintenance_hosts(&maintenance_host_sync);
	DCsync_maintenance_periods(&maintenance_period_sync);
	maintenance_sec2 = trx_time() - sec;

	if (0 != hgroups_sync.add_num + hgroups_sync.update_num + hgroups_sync.remove_num)
		update_flags |= TRX_DBSYNC_UPDATE_HOST_GROUPS;

	if (0 != maintenance_group_sync.add_num + maintenance_group_sync.update_num + maintenance_group_sync.remove_num)
		update_flags |= TRX_DBSYNC_UPDATE_MAINTENANCE_GROUPS;

	if (0 != (update_flags & TRX_DBSYNC_UPDATE_HOST_GROUPS))
		dc_hostgroups_update_cache();

	/* pre-cache nested groups used in maintenances to allow read lock */
	/* during host maintenance update calculations                     */
	if (0 != (update_flags & (TRX_DBSYNC_UPDATE_HOST_GROUPS | TRX_DBSYNC_UPDATE_MAINTENANCE_GROUPS)))
		dc_maintenance_precache_nested_groups();

	FINISH_SYNC;

	/* sync item data to support item lookups when resolving macros during configuration sync */

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_interfaces(&if_sync))
		goto out;
	ifsec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_items(&items_sync))
		goto out;

	if (FAIL == trx_dbsync_compare_template_items(&template_items_sync))
		goto out;

	if (FAIL == trx_dbsync_compare_prototype_items(&prototype_items_sync))
		goto out;
	isec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_item_preprocs(&itempp_sync))
		goto out;
	itempp_sec = trx_time() - sec;

	START_SYNC;

	/* resolves macros for interface_snmpaddrs, must be after DCsync_hmacros() */
	sec = trx_time();
	DCsync_interfaces(&if_sync);
	ifsec2 = trx_time() - sec;

	/* relies on hosts, proxies and interfaces, must be after DCsync_{hosts,interfaces}() */
	sec = trx_time();
	DCsync_items(&items_sync, flags);
	DCsync_template_items(&template_items_sync);
	DCsync_prototype_items(&prototype_items_sync);
	isec2 = trx_time() - sec;

	/* relies on items, must be after DCsync_items() */
	sec = trx_time();
	DCsync_item_preproc(&itempp_sync, sec);
	itempp_sec2 = trx_time() - sec;

	config->item_sync_ts = time(NULL);
	FINISH_SYNC;

	dc_flush_history();	/* misconfigured items generate pseudo-historic values to become notsupported */

	/* sync function data to support function lookups when resolving macros during configuration sync */

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_functions(&func_sync))
		goto out;
	fsec = trx_time() - sec;

	START_SYNC;
	sec = trx_time();
	DCsync_functions(&func_sync);
	fsec2 = trx_time() - sec;
	FINISH_SYNC;

	/* sync rest of the data */

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_triggers(&triggers_sync))
		goto out;
	tsec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_trigger_dependency(&tdep_sync))
		goto out;
	dsec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_expressions(&expr_sync))
		goto out;
	expr_sec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_actions(&action_sync))
		goto out;
	action_sec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_action_ops(&action_op_sync))
		goto out;
	action_op_sec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_action_conditions(&action_condition_sync))
		goto out;
	action_condition_sec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_trigger_tags(&trigger_tag_sync))
		goto out;
	trigger_tag_sec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_correlations(&correlation_sync))
		goto out;
	correlation_sec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_corr_conditions(&corr_condition_sync))
		goto out;
	corr_condition_sec = trx_time() - sec;

	sec = trx_time();
	if (FAIL == trx_dbsync_compare_corr_operations(&corr_operation_sync))
		goto out;
	corr_operation_sec = trx_time() - sec;

	START_SYNC;

	sec = trx_time();
	DCsync_triggers(&triggers_sync);
	tsec2 = trx_time() - sec;

	sec = trx_time();
	DCsync_trigdeps(&tdep_sync);
	dsec2 = trx_time() - sec;

	sec = trx_time();
	DCsync_expressions(&expr_sync);
	expr_sec2 = trx_time() - sec;

	sec = trx_time();
	DCsync_actions(&action_sync);
	action_sec2 = trx_time() - sec;

	sec = trx_time();
	DCsync_action_ops(&action_op_sync);
	action_op_sec2 = trx_time() - sec;

	sec = trx_time();
	DCsync_action_conditions(&action_condition_sync);
	action_condition_sec2 = trx_time() - sec;

	sec = trx_time();
	/* relies on triggers, must be after DCsync_triggers() */
	DCsync_trigger_tags(&trigger_tag_sync);
	trigger_tag_sec2 = trx_time() - sec;

	sec = trx_time();
	DCsync_correlations(&correlation_sync);
	correlation_sec2 = trx_time() - sec;

	sec = trx_time();
	/* relies on correlation rules, must be after DCsync_correlations() */
	DCsync_corr_conditions(&corr_condition_sync);
	corr_condition_sec2 = trx_time() - sec;

	sec = trx_time();
	/* relies on correlation rules, must be after DCsync_correlations() */
	DCsync_corr_operations(&corr_operation_sync);
	corr_operation_sec2 = trx_time() - sec;

	sec = trx_time();

	if (0 != hosts_sync.add_num + hosts_sync.update_num + hosts_sync.remove_num)
		update_flags |= TRX_DBSYNC_UPDATE_HOSTS;

	if (0 != items_sync.add_num + items_sync.update_num + items_sync.remove_num)
		update_flags |= TRX_DBSYNC_UPDATE_ITEMS;

	if (0 != func_sync.add_num + func_sync.update_num + func_sync.remove_num)
		update_flags |= TRX_DBSYNC_UPDATE_FUNCTIONS;

	if (0 != triggers_sync.add_num + triggers_sync.update_num + triggers_sync.remove_num)
		update_flags |= TRX_DBSYNC_UPDATE_TRIGGERS;

	if (0 != tdep_sync.add_num + tdep_sync.update_num + tdep_sync.remove_num)
		update_flags |= TRX_DBSYNC_UPDATE_TRIGGER_DEPENDENCY;

	/* update trigger topology if trigger dependency was changed */
	if (0 != (update_flags & TRX_DBSYNC_UPDATE_TRIGGER_DEPENDENCY))
		dc_trigger_update_topology();

	/* update various trigger related links in cache */
	if (0 != (update_flags & (TRX_DBSYNC_UPDATE_HOSTS | TRX_DBSYNC_UPDATE_ITEMS | TRX_DBSYNC_UPDATE_FUNCTIONS |
			TRX_DBSYNC_UPDATE_TRIGGERS)))
	{
		dc_trigger_update_cache();
	}

	update_sec = trx_time() - sec;

	if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		total = csec + hsec + hisec + htsec + gmsec + hmsec + ifsec + isec + tsec + dsec + fsec + expr_sec +
				action_sec + action_op_sec + action_condition_sec + trigger_tag_sec + correlation_sec +
				corr_condition_sec + corr_operation_sec + hgroups_sec + itempp_sec + maintenance_sec;
		total2 = csec2 + hsec2 + hisec2 + htsec2 + gmsec2 + hmsec2 + ifsec2 + isec2 + tsec2 + dsec2 + fsec2 +
				expr_sec2 + action_op_sec2 + action_sec2 + action_condition_sec2 + trigger_tag_sec2 +
				correlation_sec2 + corr_condition_sec2 + corr_operation_sec2 + hgroups_sec2 +
				itempp_sec2 + maintenance_sec2 + update_sec;

		treegix_log(LOG_LEVEL_DEBUG, "%s() config     : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, csec, csec2, config_sync.add_num, config_sync.update_num,
				config_sync.remove_num);

		total += autoreg_csec;
		total2 += autoreg_csec2;
		treegix_log(LOG_LEVEL_DEBUG, "%s() autoreg    : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, autoreg_csec, autoreg_csec2, autoreg_config_sync.add_num,
				autoreg_config_sync.update_num, autoreg_config_sync.remove_num);

		treegix_log(LOG_LEVEL_DEBUG, "%s() hosts      : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, hsec, hsec2, hosts_sync.add_num, hosts_sync.update_num,
				hosts_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() host_invent: sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, hisec, hisec2, hi_sync.add_num, hi_sync.update_num,
				hi_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() templates  : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, htsec, htsec2, htmpl_sync.add_num, htmpl_sync.update_num,
				htmpl_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() globmacros : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, gmsec, gmsec2, gmacro_sync.add_num, gmacro_sync.update_num,
				gmacro_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() hostmacros : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, hmsec, hmsec2, hmacro_sync.add_num, hmacro_sync.update_num,
				hmacro_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() interfaces : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, ifsec, ifsec2, if_sync.add_num, if_sync.update_num,
				if_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() items      : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, isec, isec2, items_sync.add_num, items_sync.update_num,
				items_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() template_items      : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, isec, isec2, template_items_sync.add_num,
				template_items_sync.update_num, template_items_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() prototype_items      : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, isec, isec2, prototype_items_sync.add_num,
				prototype_items_sync.update_num, prototype_items_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() triggers   : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, tsec, tsec2, triggers_sync.add_num, triggers_sync.update_num,
				triggers_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() trigdeps   : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, dsec, dsec2, tdep_sync.add_num, tdep_sync.update_num,
				tdep_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() trig. tags : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, trigger_tag_sec, trigger_tag_sec2, trigger_tag_sync.add_num,
				trigger_tag_sync.update_num, trigger_tag_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() host tags : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, host_tag_sec, host_tag_sec2, host_tag_sync.add_num,
				host_tag_sync.update_num, host_tag_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() functions  : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, fsec, fsec2, func_sync.add_num, func_sync.update_num,
				func_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() expressions: sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, expr_sec, expr_sec2, expr_sync.add_num, expr_sync.update_num,
				expr_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() actions    : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, action_sec, action_sec2, action_sync.add_num, action_sync.update_num,
				action_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() operations : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, action_op_sec, action_op_sec2, action_op_sync.add_num,
				action_op_sync.update_num, action_op_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() conditions : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, action_condition_sec, action_condition_sec2,
				action_condition_sync.add_num, action_condition_sync.update_num,
				action_condition_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() corr       : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, correlation_sec, correlation_sec2, correlation_sync.add_num,
				correlation_sync.update_num, correlation_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() corr_cond  : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, corr_condition_sec, corr_condition_sec2, corr_condition_sync.add_num,
				corr_condition_sync.update_num, corr_condition_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() corr_op    : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, corr_operation_sec, corr_operation_sec2, corr_operation_sync.add_num,
				corr_operation_sync.update_num, corr_operation_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() hgroups    : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, hgroups_sec, hgroups_sec2, hgroups_sync.add_num,
				hgroups_sync.update_num, hgroups_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() item pproc : sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, itempp_sec, itempp_sec2, itempp_sync.add_num, itempp_sync.update_num,
				itempp_sync.remove_num);
		treegix_log(LOG_LEVEL_DEBUG, "%s() maintenance: sql:" TRX_FS_DBL " sync:" TRX_FS_DBL " sec ("
				TRX_FS_UI64 "/" TRX_FS_UI64 "/" TRX_FS_UI64 ").",
				__func__, maintenance_sec, maintenance_sec2, maintenance_sync.add_num,
				maintenance_sync.update_num, maintenance_sync.remove_num);

		treegix_log(LOG_LEVEL_DEBUG, "%s() reindex    : " TRX_FS_DBL " sec.", __func__, update_sec);

		treegix_log(LOG_LEVEL_DEBUG, "%s() total sql  : " TRX_FS_DBL " sec.", __func__, total);
		treegix_log(LOG_LEVEL_DEBUG, "%s() total sync : " TRX_FS_DBL " sec.", __func__, total2);

		treegix_log(LOG_LEVEL_DEBUG, "%s() proxies    : %d (%d slots)", __func__,
				config->proxies.num_data, config->proxies.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() hosts      : %d (%d slots)", __func__,
				config->hosts.num_data, config->hosts.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() hosts_h    : %d (%d slots)", __func__,
				config->hosts_h.num_data, config->hosts_h.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() hosts_p    : %d (%d slots)", __func__,
				config->hosts_p.num_data, config->hosts_p.num_slots);
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		treegix_log(LOG_LEVEL_DEBUG, "%s() psks       : %d (%d slots)", __func__,
				config->psks.num_data, config->psks.num_slots);
#endif
		treegix_log(LOG_LEVEL_DEBUG, "%s() ipmihosts  : %d (%d slots)", __func__,
				config->ipmihosts.num_data, config->ipmihosts.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() host_invent: %d (%d slots)", __func__,
				config->host_inventories.num_data, config->host_inventories.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() htmpls     : %d (%d slots)", __func__,
				config->htmpls.num_data, config->htmpls.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() gmacros    : %d (%d slots)", __func__,
				config->gmacros.num_data, config->gmacros.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() gmacros_m  : %d (%d slots)", __func__,
				config->gmacros_m.num_data, config->gmacros_m.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() hmacros    : %d (%d slots)", __func__,
				config->hmacros.num_data, config->hmacros.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() hmacros_hm : %d (%d slots)", __func__,
				config->hmacros_hm.num_data, config->hmacros_hm.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() interfaces : %d (%d slots)", __func__,
				config->interfaces.num_data, config->interfaces.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() interfac_ht: %d (%d slots)", __func__,
				config->interfaces_ht.num_data, config->interfaces_ht.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() if_snmpitms: %d (%d slots)", __func__,
				config->interface_snmpitems.num_data, config->interface_snmpitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() if_snmpaddr: %d (%d slots)", __func__,
				config->interface_snmpaddrs.num_data, config->interface_snmpaddrs.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() items      : %d (%d slots)", __func__,
				config->items.num_data, config->items.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() items_hk   : %d (%d slots)", __func__,
				config->items_hk.num_data, config->items_hk.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() numitems   : %d (%d slots)", __func__,
				config->numitems.num_data, config->numitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() preprocitems: %d (%d slots)", __func__,
				config->preprocitems.num_data, config->preprocitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() preprocops : %d (%d slots)", __func__,
				config->preprocops.num_data, config->preprocops.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() snmpitems  : %d (%d slots)", __func__,
				config->snmpitems.num_data, config->snmpitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() ipmiitems  : %d (%d slots)", __func__,
				config->ipmiitems.num_data, config->ipmiitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() trapitems  : %d (%d slots)", __func__,
				config->trapitems.num_data, config->trapitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() dependentitems  : %d (%d slots)", __func__,
				config->dependentitems.num_data, config->dependentitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() logitems   : %d (%d slots)", __func__,
				config->logitems.num_data, config->logitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() dbitems    : %d (%d slots)", __func__,
				config->dbitems.num_data, config->dbitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() sshitems   : %d (%d slots)", __func__,
				config->sshitems.num_data, config->sshitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() telnetitems: %d (%d slots)", __func__,
				config->telnetitems.num_data, config->telnetitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() simpleitems: %d (%d slots)", __func__,
				config->simpleitems.num_data, config->simpleitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() jmxitems   : %d (%d slots)", __func__,
				config->jmxitems.num_data, config->jmxitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() calcitems  : %d (%d slots)", __func__,
				config->calcitems.num_data, config->calcitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() httpitems  : %d (%d slots)", __func__,
				config->httpitems.num_data, config->httpitems.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() functions  : %d (%d slots)", __func__,
				config->functions.num_data, config->functions.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() triggers   : %d (%d slots)", __func__,
				config->triggers.num_data, config->triggers.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() trigdeps   : %d (%d slots)", __func__,
				config->trigdeps.num_data, config->trigdeps.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() trig. tags : %d (%d slots)", __func__,
				config->trigger_tags.num_data, config->trigger_tags.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() expressions: %d (%d slots)", __func__,
				config->expressions.num_data, config->expressions.num_slots);

		treegix_log(LOG_LEVEL_DEBUG, "%s() actions    : %d (%d slots)", __func__,
				config->actions.num_data, config->actions.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() conditions : %d (%d slots)", __func__,
				config->action_conditions.num_data, config->action_conditions.num_slots);

		treegix_log(LOG_LEVEL_DEBUG, "%s() corr.      : %d (%d slots)", __func__,
				config->correlations.num_data, config->correlations.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() corr. conds: %d (%d slots)", __func__,
				config->corr_conditions.num_data, config->corr_conditions.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() corr. ops  : %d (%d slots)", __func__,
				config->corr_operations.num_data, config->corr_operations.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() hgroups    : %d (%d slots)", __func__,
				config->hostgroups.num_data, config->hostgroups.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() item procs : %d (%d slots)", __func__,
				config->preprocops.num_data, config->preprocops.num_slots);

		treegix_log(LOG_LEVEL_DEBUG, "%s() maintenance: %d (%d slots)", __func__,
				config->maintenances.num_data, config->maintenances.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() maint tags : %d (%d slots)", __func__,
				config->maintenance_tags.num_data, config->maintenance_tags.num_slots);
		treegix_log(LOG_LEVEL_DEBUG, "%s() maint time : %d (%d slots)", __func__,
				config->maintenance_periods.num_data, config->maintenance_periods.num_slots);

		for (i = 0; TRX_POLLER_TYPE_COUNT > i; i++)
		{
			treegix_log(LOG_LEVEL_DEBUG, "%s() queue[%d]   : %d (%d allocated)", __func__,
					i, config->queues[i].elems_num, config->queues[i].elems_alloc);
		}

		treegix_log(LOG_LEVEL_DEBUG, "%s() pqueue     : %d (%d allocated)", __func__,
				config->pqueue.elems_num, config->pqueue.elems_alloc);

		treegix_log(LOG_LEVEL_DEBUG, "%s() timer queue: %d (%d allocated)", __func__,
				config->timer_queue.elems_num, config->timer_queue.elems_alloc);

		treegix_log(LOG_LEVEL_DEBUG, "%s() configfree : " TRX_FS_DBL "%%", __func__,
				100 * ((double)config_mem->free_size / config_mem->orig_size));

		treegix_log(LOG_LEVEL_DEBUG, "%s() strings    : %d (%d slots)", __func__,
				config->strpool.num_data, config->strpool.num_slots);

		trx_mem_dump_stats(LOG_LEVEL_DEBUG, config_mem);
	}

	config->status->last_update = 0;
	config->sync_ts = time(NULL);

	FINISH_SYNC;
out:
	trx_dbsync_clear(&config_sync);
	trx_dbsync_clear(&autoreg_config_sync);
	trx_dbsync_clear(&hosts_sync);
	trx_dbsync_clear(&hi_sync);
	trx_dbsync_clear(&htmpl_sync);
	trx_dbsync_clear(&gmacro_sync);
	trx_dbsync_clear(&hmacro_sync);
	trx_dbsync_clear(&host_tag_sync);
	trx_dbsync_clear(&if_sync);
	trx_dbsync_clear(&items_sync);
	trx_dbsync_clear(&template_items_sync);
	trx_dbsync_clear(&prototype_items_sync);
	trx_dbsync_clear(&triggers_sync);
	trx_dbsync_clear(&tdep_sync);
	trx_dbsync_clear(&func_sync);
	trx_dbsync_clear(&expr_sync);
	trx_dbsync_clear(&action_sync);
	trx_dbsync_clear(&action_op_sync);
	trx_dbsync_clear(&action_condition_sync);
	trx_dbsync_clear(&trigger_tag_sync);
	trx_dbsync_clear(&correlation_sync);
	trx_dbsync_clear(&corr_condition_sync);
	trx_dbsync_clear(&corr_operation_sync);
	trx_dbsync_clear(&hgroups_sync);
	trx_dbsync_clear(&itempp_sync);
	trx_dbsync_clear(&maintenance_sync);
	trx_dbsync_clear(&maintenance_period_sync);
	trx_dbsync_clear(&maintenance_tag_sync);
	trx_dbsync_clear(&maintenance_group_sync);
	trx_dbsync_clear(&maintenance_host_sync);
	trx_dbsync_clear(&hgroup_host_sync);

	trx_dbsync_free_env();

	if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
		DCdump_configuration();

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Helper functions for configuration cache data structure element comparison *
 * and hash value calculation.                                                *
 *                                                                            *
 * The __config_mem_XXX_func(), __config_XXX_hash and __config_XXX_compare    *
 * functions are used only inside init_configuration_cache() function to      *
 * initialize internal data structures.                                       *
 *                                                                            *
 ******************************************************************************/

static trx_hash_t	__config_item_hk_hash(const void *data)
{
	const TRX_DC_ITEM_HK	*item_hk = (const TRX_DC_ITEM_HK *)data;

	trx_hash_t		hash;

	hash = TRX_DEFAULT_UINT64_HASH_FUNC(&item_hk->hostid);
	hash = TRX_DEFAULT_STRING_HASH_ALGO(item_hk->key, strlen(item_hk->key), hash);

	return hash;
}

static int	__config_item_hk_compare(const void *d1, const void *d2)
{
	const TRX_DC_ITEM_HK	*item_hk_1 = (const TRX_DC_ITEM_HK *)d1;
	const TRX_DC_ITEM_HK	*item_hk_2 = (const TRX_DC_ITEM_HK *)d2;

	TRX_RETURN_IF_NOT_EQUAL(item_hk_1->hostid, item_hk_2->hostid);

	return item_hk_1->key == item_hk_2->key ? 0 : strcmp(item_hk_1->key, item_hk_2->key);
}

static trx_hash_t	__config_host_h_hash(const void *data)
{
	const TRX_DC_HOST_H	*host_h = (const TRX_DC_HOST_H *)data;

	return TRX_DEFAULT_STRING_HASH_ALGO(host_h->host, strlen(host_h->host), TRX_DEFAULT_HASH_SEED);
}

static int	__config_host_h_compare(const void *d1, const void *d2)
{
	const TRX_DC_HOST_H	*host_h_1 = (const TRX_DC_HOST_H *)d1;
	const TRX_DC_HOST_H	*host_h_2 = (const TRX_DC_HOST_H *)d2;

	return host_h_1->host == host_h_2->host ? 0 : strcmp(host_h_1->host, host_h_2->host);
}

static trx_hash_t	__config_gmacro_m_hash(const void *data)
{
	const TRX_DC_GMACRO_M	*gmacro_m = (const TRX_DC_GMACRO_M *)data;

	trx_hash_t		hash;

	hash = TRX_DEFAULT_STRING_HASH_FUNC(gmacro_m->macro);

	return hash;
}

static int	__config_gmacro_m_compare(const void *d1, const void *d2)
{
	const TRX_DC_GMACRO_M	*gmacro_m_1 = (const TRX_DC_GMACRO_M *)d1;
	const TRX_DC_GMACRO_M	*gmacro_m_2 = (const TRX_DC_GMACRO_M *)d2;

	return gmacro_m_1->macro == gmacro_m_2->macro ? 0 : strcmp(gmacro_m_1->macro, gmacro_m_2->macro);
}

static trx_hash_t	__config_hmacro_hm_hash(const void *data)
{
	const TRX_DC_HMACRO_HM	*hmacro_hm = (const TRX_DC_HMACRO_HM *)data;

	trx_hash_t		hash;

	hash = TRX_DEFAULT_UINT64_HASH_FUNC(&hmacro_hm->hostid);
	hash = TRX_DEFAULT_STRING_HASH_ALGO(hmacro_hm->macro, strlen(hmacro_hm->macro), hash);

	return hash;
}

static int	__config_hmacro_hm_compare(const void *d1, const void *d2)
{
	const TRX_DC_HMACRO_HM	*hmacro_hm_1 = (const TRX_DC_HMACRO_HM *)d1;
	const TRX_DC_HMACRO_HM	*hmacro_hm_2 = (const TRX_DC_HMACRO_HM *)d2;

	TRX_RETURN_IF_NOT_EQUAL(hmacro_hm_1->hostid, hmacro_hm_2->hostid);

	return hmacro_hm_1->macro == hmacro_hm_2->macro ? 0 : strcmp(hmacro_hm_1->macro, hmacro_hm_2->macro);
}

static trx_hash_t	__config_interface_ht_hash(const void *data)
{
	const TRX_DC_INTERFACE_HT	*interface_ht = (const TRX_DC_INTERFACE_HT *)data;

	trx_hash_t			hash;

	hash = TRX_DEFAULT_UINT64_HASH_FUNC(&interface_ht->hostid);
	hash = TRX_DEFAULT_STRING_HASH_ALGO((char *)&interface_ht->type, 1, hash);

	return hash;
}

static int	__config_interface_ht_compare(const void *d1, const void *d2)
{
	const TRX_DC_INTERFACE_HT	*interface_ht_1 = (const TRX_DC_INTERFACE_HT *)d1;
	const TRX_DC_INTERFACE_HT	*interface_ht_2 = (const TRX_DC_INTERFACE_HT *)d2;

	TRX_RETURN_IF_NOT_EQUAL(interface_ht_1->hostid, interface_ht_2->hostid);
	TRX_RETURN_IF_NOT_EQUAL(interface_ht_1->type, interface_ht_2->type);

	return 0;
}

static trx_hash_t	__config_interface_addr_hash(const void *data)
{
	const TRX_DC_INTERFACE_ADDR	*interface_addr = (const TRX_DC_INTERFACE_ADDR *)data;

	return TRX_DEFAULT_STRING_HASH_ALGO(interface_addr->addr, strlen(interface_addr->addr), TRX_DEFAULT_HASH_SEED);
}

static int	__config_interface_addr_compare(const void *d1, const void *d2)
{
	const TRX_DC_INTERFACE_ADDR	*interface_addr_1 = (const TRX_DC_INTERFACE_ADDR *)d1;
	const TRX_DC_INTERFACE_ADDR	*interface_addr_2 = (const TRX_DC_INTERFACE_ADDR *)d2;

	return (interface_addr_1->addr == interface_addr_2->addr ? 0 : strcmp(interface_addr_1->addr, interface_addr_2->addr));
}

static int	__config_snmp_item_compare(const TRX_DC_ITEM *i1, const TRX_DC_ITEM *i2)
{
	const TRX_DC_SNMPITEM	*s1;
	const TRX_DC_SNMPITEM	*s2;

	unsigned char		f1;
	unsigned char		f2;

	TRX_RETURN_IF_NOT_EQUAL(i1->interfaceid, i2->interfaceid);
	TRX_RETURN_IF_NOT_EQUAL(i1->port, i2->port);
	TRX_RETURN_IF_NOT_EQUAL(i1->type, i2->type);

	f1 = TRX_FLAG_DISCOVERY_RULE & i1->flags;
	f2 = TRX_FLAG_DISCOVERY_RULE & i2->flags;

	TRX_RETURN_IF_NOT_EQUAL(f1, f2);

	s1 = (TRX_DC_SNMPITEM *)trx_hashset_search(&config->snmpitems, &i1->itemid);
	s2 = (TRX_DC_SNMPITEM *)trx_hashset_search(&config->snmpitems, &i2->itemid);

	TRX_RETURN_IF_NOT_EQUAL(s1->snmp_community, s2->snmp_community);
	TRX_RETURN_IF_NOT_EQUAL(s1->snmpv3_securityname, s2->snmpv3_securityname);
	TRX_RETURN_IF_NOT_EQUAL(s1->snmpv3_authpassphrase, s2->snmpv3_authpassphrase);
	TRX_RETURN_IF_NOT_EQUAL(s1->snmpv3_privpassphrase, s2->snmpv3_privpassphrase);
	TRX_RETURN_IF_NOT_EQUAL(s1->snmpv3_contextname, s2->snmpv3_contextname);
	TRX_RETURN_IF_NOT_EQUAL(s1->snmpv3_securitylevel, s2->snmpv3_securitylevel);
	TRX_RETURN_IF_NOT_EQUAL(s1->snmpv3_authprotocol, s2->snmpv3_authprotocol);
	TRX_RETURN_IF_NOT_EQUAL(s1->snmpv3_privprotocol, s2->snmpv3_privprotocol);
	TRX_RETURN_IF_NOT_EQUAL(s1->snmp_oid_type, s2->snmp_oid_type);

	return 0;
}

static int	__config_heap_elem_compare(const void *d1, const void *d2)
{
	const trx_binary_heap_elem_t	*e1 = (const trx_binary_heap_elem_t *)d1;
	const trx_binary_heap_elem_t	*e2 = (const trx_binary_heap_elem_t *)d2;

	const TRX_DC_ITEM		*i1 = (const TRX_DC_ITEM *)e1->data;
	const TRX_DC_ITEM		*i2 = (const TRX_DC_ITEM *)e2->data;

	TRX_RETURN_IF_NOT_EQUAL(i1->nextcheck, i2->nextcheck);
	TRX_RETURN_IF_NOT_EQUAL(i1->queue_priority, i2->queue_priority);

	if (SUCCEED != is_snmp_type(i1->type))
	{
		if (SUCCEED != is_snmp_type(i2->type))
			return 0;

		return -1;
	}
	else
	{
		if (SUCCEED != is_snmp_type(i2->type))
			return +1;

		return __config_snmp_item_compare(i1, i2);
	}
}

static int	__config_pinger_elem_compare(const void *d1, const void *d2)
{
	const trx_binary_heap_elem_t	*e1 = (const trx_binary_heap_elem_t *)d1;
	const trx_binary_heap_elem_t	*e2 = (const trx_binary_heap_elem_t *)d2;

	const TRX_DC_ITEM		*i1 = (const TRX_DC_ITEM *)e1->data;
	const TRX_DC_ITEM		*i2 = (const TRX_DC_ITEM *)e2->data;

	TRX_RETURN_IF_NOT_EQUAL(i1->nextcheck, i2->nextcheck);
	TRX_RETURN_IF_NOT_EQUAL(i1->queue_priority, i2->queue_priority);
	TRX_RETURN_IF_NOT_EQUAL(i1->interfaceid, i2->interfaceid);

	return 0;
}

static int	__config_java_item_compare(const TRX_DC_ITEM *i1, const TRX_DC_ITEM *i2)
{
	const TRX_DC_JMXITEM	*j1;
	const TRX_DC_JMXITEM	*j2;

	TRX_RETURN_IF_NOT_EQUAL(i1->interfaceid, i2->interfaceid);

	j1 = (TRX_DC_JMXITEM *)trx_hashset_search(&config->jmxitems, &i1->itemid);
	j2 = (TRX_DC_JMXITEM *)trx_hashset_search(&config->jmxitems, &i2->itemid);

	TRX_RETURN_IF_NOT_EQUAL(j1->username, j2->username);
	TRX_RETURN_IF_NOT_EQUAL(j1->password, j2->password);
	TRX_RETURN_IF_NOT_EQUAL(j1->jmx_endpoint, j2->jmx_endpoint);

	return 0;
}

static int	__config_java_elem_compare(const void *d1, const void *d2)
{
	const trx_binary_heap_elem_t	*e1 = (const trx_binary_heap_elem_t *)d1;
	const trx_binary_heap_elem_t	*e2 = (const trx_binary_heap_elem_t *)d2;

	const TRX_DC_ITEM		*i1 = (const TRX_DC_ITEM *)e1->data;
	const TRX_DC_ITEM		*i2 = (const TRX_DC_ITEM *)e2->data;

	TRX_RETURN_IF_NOT_EQUAL(i1->nextcheck, i2->nextcheck);
	TRX_RETURN_IF_NOT_EQUAL(i1->queue_priority, i2->queue_priority);

	return __config_java_item_compare(i1, i2);
}

static int	__config_proxy_compare(const void *d1, const void *d2)
{
	const trx_binary_heap_elem_t	*e1 = (const trx_binary_heap_elem_t *)d1;
	const trx_binary_heap_elem_t	*e2 = (const trx_binary_heap_elem_t *)d2;

	const TRX_DC_PROXY		*p1 = (const TRX_DC_PROXY *)e1->data;
	const TRX_DC_PROXY		*p2 = (const TRX_DC_PROXY *)e2->data;

	TRX_RETURN_IF_NOT_EQUAL(p1->nextcheck, p2->nextcheck);

	return 0;
}

/* hash and compare functions for expressions hashset */

static trx_hash_t	__config_regexp_hash(const void *data)
{
	const TRX_DC_REGEXP	*regexp = (const TRX_DC_REGEXP *)data;

	return TRX_DEFAULT_STRING_HASH_FUNC(regexp->name);
}

static int	__config_regexp_compare(const void *d1, const void *d2)
{
	const TRX_DC_REGEXP	*r1 = (const TRX_DC_REGEXP *)d1;
	const TRX_DC_REGEXP	*r2 = (const TRX_DC_REGEXP *)d2;

	return r1->name == r2->name ? 0 : strcmp(r1->name, r2->name);
}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
static trx_hash_t	__config_psk_hash(const void *data)
{
	const TRX_DC_PSK	*psk_i = (const TRX_DC_PSK *)data;

	return TRX_DEFAULT_STRING_HASH_ALGO(psk_i->tls_psk_identity, strlen(psk_i->tls_psk_identity),
			TRX_DEFAULT_HASH_SEED);
}

static int	__config_psk_compare(const void *d1, const void *d2)
{
	const TRX_DC_PSK	*psk_1 = (const TRX_DC_PSK *)d1;
	const TRX_DC_PSK	*psk_2 = (const TRX_DC_PSK *)d2;

	return psk_1->tls_psk_identity == psk_2->tls_psk_identity ? 0 : strcmp(psk_1->tls_psk_identity,
			psk_2->tls_psk_identity);
}
#endif

static int	__config_timer_compare(const void *d1, const void *d2)
{
	const trx_binary_heap_elem_t	*e1 = (const trx_binary_heap_elem_t *)d1;
	const trx_binary_heap_elem_t	*e2 = (const trx_binary_heap_elem_t *)d2;

	const TRX_DC_TRIGGER		*t1 = (const TRX_DC_TRIGGER *)e1->data;
	const TRX_DC_TRIGGER		*t2 = (const TRX_DC_TRIGGER *)e2->data;

	TRX_RETURN_IF_NOT_EQUAL(t1->nextcheck, t2->nextcheck);

	return 0;
}

static trx_hash_t	__config_data_session_hash(const void *data)
{
	const trx_data_session_t	*session = (const trx_data_session_t *)data;
	trx_hash_t			hash;

	hash = TRX_DEFAULT_UINT64_HASH_FUNC(&session->hostid);
	return TRX_DEFAULT_STRING_HASH_ALGO(session->token, strlen(session->token), hash);
}

static int	__config_data_session_compare(const void *d1, const void *d2)
{
	const trx_data_session_t	*s1 = (const trx_data_session_t *)d1;
	const trx_data_session_t	*s2 = (const trx_data_session_t *)d2;

	TRX_RETURN_IF_NOT_EQUAL(s1->hostid, s2->hostid);
	return strcmp(s1->token, s2->token);
}

/******************************************************************************
 *                                                                            *
 * Function: init_configuration_cache                                         *
 *                                                                            *
 * Purpose: Allocate shared memory for configuration cache                    *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
int	init_configuration_cache(char **error)
{
	int	i, ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() size:" TRX_FS_UI64, __func__, CONFIG_CONF_CACHE_SIZE);

	if (SUCCEED != (ret = trx_rwlock_create(&config_lock, TRX_RWLOCK_CONFIG, error)))
		goto out;

	if (SUCCEED != (ret = trx_mem_create(&config_mem, CONFIG_CONF_CACHE_SIZE, "configuration cache",
			"CacheSize", 0, error)))
	{
		goto out;
	}

	config = (TRX_DC_CONFIG *)__config_mem_malloc_func(NULL, sizeof(TRX_DC_CONFIG) +
			CONFIG_TIMER_FORKS * sizeof(trx_vector_ptr_t));

#define CREATE_HASHSET(hashset, hashset_size)									\
														\
	CREATE_HASHSET_EXT(hashset, hashset_size, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC)

#define CREATE_HASHSET_EXT(hashset, hashset_size, hash_func, compare_func)					\
														\
	trx_hashset_create_ext(&hashset, hashset_size, hash_func, compare_func, NULL,				\
			__config_mem_malloc_func, __config_mem_realloc_func, __config_mem_free_func)

	CREATE_HASHSET(config->items, 100);
	CREATE_HASHSET(config->numitems, 0);
	CREATE_HASHSET(config->snmpitems, 0);
	CREATE_HASHSET(config->ipmiitems, 0);
	CREATE_HASHSET(config->trapitems, 0);
	CREATE_HASHSET(config->dependentitems, 0);
	CREATE_HASHSET(config->logitems, 0);
	CREATE_HASHSET(config->dbitems, 0);
	CREATE_HASHSET(config->sshitems, 0);
	CREATE_HASHSET(config->telnetitems, 0);
	CREATE_HASHSET(config->simpleitems, 0);
	CREATE_HASHSET(config->jmxitems, 0);
	CREATE_HASHSET(config->calcitems, 0);
	CREATE_HASHSET(config->masteritems, 0);
	CREATE_HASHSET(config->preprocitems, 0);
	CREATE_HASHSET(config->httpitems, 0);
	CREATE_HASHSET(config->template_items, 0);
	CREATE_HASHSET(config->prototype_items, 0);
	CREATE_HASHSET(config->functions, 100);
	CREATE_HASHSET(config->triggers, 100);
	CREATE_HASHSET(config->trigdeps, 0);
	CREATE_HASHSET(config->hosts, 10);
	CREATE_HASHSET(config->proxies, 0);
	CREATE_HASHSET(config->host_inventories, 0);
	CREATE_HASHSET(config->host_inventories_auto, 0);
	CREATE_HASHSET(config->ipmihosts, 0);
	CREATE_HASHSET(config->htmpls, 0);
	CREATE_HASHSET(config->gmacros, 0);
	CREATE_HASHSET(config->hmacros, 0);
	CREATE_HASHSET(config->interfaces, 10);
	CREATE_HASHSET(config->interface_snmpitems, 0);
	CREATE_HASHSET(config->expressions, 0);
	CREATE_HASHSET(config->actions, 0);
	CREATE_HASHSET(config->action_conditions, 0);
	CREATE_HASHSET(config->trigger_tags, 0);
	CREATE_HASHSET(config->host_tags, 0);
	CREATE_HASHSET(config->host_tags_index, 0);
	CREATE_HASHSET(config->correlations, 0);
	CREATE_HASHSET(config->corr_conditions, 0);
	CREATE_HASHSET(config->corr_operations, 0);
	CREATE_HASHSET(config->hostgroups, 0);
	trx_vector_ptr_create_ext(&config->hostgroups_name, __config_mem_malloc_func, __config_mem_realloc_func,
			__config_mem_free_func);

	CREATE_HASHSET(config->preprocops, 0);

	CREATE_HASHSET(config->maintenances, 0);
	CREATE_HASHSET(config->maintenance_periods, 0);
	CREATE_HASHSET(config->maintenance_tags, 0);

	CREATE_HASHSET_EXT(config->items_hk, 100, __config_item_hk_hash, __config_item_hk_compare);
	CREATE_HASHSET_EXT(config->hosts_h, 10, __config_host_h_hash, __config_host_h_compare);
	CREATE_HASHSET_EXT(config->hosts_p, 0, __config_host_h_hash, __config_host_h_compare);
	CREATE_HASHSET_EXT(config->gmacros_m, 0, __config_gmacro_m_hash, __config_gmacro_m_compare);
	CREATE_HASHSET_EXT(config->hmacros_hm, 0, __config_hmacro_hm_hash, __config_hmacro_hm_compare);
	CREATE_HASHSET_EXT(config->interfaces_ht, 10, __config_interface_ht_hash, __config_interface_ht_compare);
	CREATE_HASHSET_EXT(config->interface_snmpaddrs, 0, __config_interface_addr_hash, __config_interface_addr_compare);
	CREATE_HASHSET_EXT(config->regexps, 0, __config_regexp_hash, __config_regexp_compare);

	CREATE_HASHSET_EXT(config->strpool, 100, __config_strpool_hash, __config_strpool_compare);

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	CREATE_HASHSET_EXT(config->psks, 0, __config_psk_hash, __config_psk_compare);
#endif

	for (i = 0; i < TRX_POLLER_TYPE_COUNT; i++)
	{
		switch (i)
		{
			case TRX_POLLER_TYPE_JAVA:
				trx_binary_heap_create_ext(&config->queues[i],
						__config_java_elem_compare,
						TRX_BINARY_HEAP_OPTION_DIRECT,
						__config_mem_malloc_func,
						__config_mem_realloc_func,
						__config_mem_free_func);
				break;
			case TRX_POLLER_TYPE_PINGER:
				trx_binary_heap_create_ext(&config->queues[i],
						__config_pinger_elem_compare,
						TRX_BINARY_HEAP_OPTION_DIRECT,
						__config_mem_malloc_func,
						__config_mem_realloc_func,
						__config_mem_free_func);
				break;
			default:
				trx_binary_heap_create_ext(&config->queues[i],
						__config_heap_elem_compare,
						TRX_BINARY_HEAP_OPTION_DIRECT,
						__config_mem_malloc_func,
						__config_mem_realloc_func,
						__config_mem_free_func);
				break;
		}
	}

	trx_binary_heap_create_ext(&config->pqueue,
					__config_proxy_compare,
					TRX_BINARY_HEAP_OPTION_DIRECT,
					__config_mem_malloc_func,
					__config_mem_realloc_func,
					__config_mem_free_func);

	trx_binary_heap_create_ext(&config->timer_queue,
					__config_timer_compare,
					TRX_BINARY_HEAP_OPTION_DIRECT,
					__config_mem_malloc_func,
					__config_mem_realloc_func,
					__config_mem_free_func);

	CREATE_HASHSET_EXT(config->data_sessions, 0, __config_data_session_hash, __config_data_session_compare);

	config->config = NULL;

	config->status = (TRX_DC_STATUS *)__config_mem_malloc_func(NULL, sizeof(TRX_DC_STATUS));
	config->status->last_update = 0;

	config->availability_diff_ts = 0;
	config->sync_ts = 0;
	config->item_sync_ts = 0;

	/* maintenance data are used only when timers are defined (server) */
	if (0 != CONFIG_TIMER_FORKS)
	{
		config->maintenance_update = TRX_MAINTENANCE_UPDATE_FALSE;
		config->maintenance_update_flags = (trx_uint64_t *)__config_mem_malloc_func(NULL, sizeof(trx_uint64_t) *
				TRX_MAINTENANCE_UPDATE_FLAGS_NUM());
		memset(config->maintenance_update_flags, 0, sizeof(trx_uint64_t) * TRX_MAINTENANCE_UPDATE_FLAGS_NUM());
	}

	config->proxy_lastaccess_ts = time(NULL);

	/* create data session token for proxies */
	if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY))
	{
		char	*token;

		token = trx_create_token(0);
		config->session_token = dc_strdup(token);
		trx_free(token);
	}
	else
		config->session_token = NULL;

#undef CREATE_HASHSET
#undef CREATE_HASHSET_EXT
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: free_configuration_cache                                         *
 *                                                                            *
 * Purpose: Free memory allocated for configuration cache                     *
 *                                                                            *
 * Author: Alexei Vladishev, Aleksandrs Saveljevs                             *
 *                                                                            *
 ******************************************************************************/
void	free_configuration_cache(void)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	WRLOCK_CACHE;

	config = NULL;

	UNLOCK_CACHE;

	trx_rwlock_destroy(&config_lock);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: in_maintenance_without_data_collection                           *
 *                                                                            *
 * Parameters: maintenance_status - [IN] maintenance status                   *
 *                                       HOST_MAINTENANCE_STATUS_* flag       *
 *             maintenance_type   - [IN] maintenance type                     *
 *                                       MAINTENANCE_TYPE_* flag              *
 *             type               - [IN] item type                            *
 *                                       ITEM_TYPE_* flag                     *
 *                                                                            *
 * Return value: SUCCEED if host in maintenance without data collection       *
 *               FAIL otherwise                                               *
 *                                                                            *
 ******************************************************************************/
int	in_maintenance_without_data_collection(unsigned char maintenance_status, unsigned char maintenance_type,
		unsigned char type)
{
	if (HOST_MAINTENANCE_STATUS_ON != maintenance_status)
		return FAIL;

	if (MAINTENANCE_TYPE_NODATA != maintenance_type)
		return FAIL;

	if (ITEM_TYPE_INTERNAL == type)
		return FAIL;

	return SUCCEED;
}

static void	DCget_host(DC_HOST *dst_host, const TRX_DC_HOST *src_host)
{
	const TRX_DC_IPMIHOST		*ipmihost;
	const TRX_DC_HOST_INVENTORY	*host_inventory;

	dst_host->hostid = src_host->hostid;
	dst_host->proxy_hostid = src_host->proxy_hostid;
	strscpy(dst_host->host, src_host->host);
	trx_strlcpy_utf8(dst_host->name, src_host->name, sizeof(dst_host->name));
	dst_host->maintenance_status = src_host->maintenance_status;
	dst_host->maintenance_type = src_host->maintenance_type;
	dst_host->maintenance_from = src_host->maintenance_from;
	dst_host->errors_from = src_host->errors_from;
	dst_host->available = src_host->available;
	dst_host->disable_until = src_host->disable_until;
	dst_host->snmp_errors_from = src_host->snmp_errors_from;
	dst_host->snmp_available = src_host->snmp_available;
	dst_host->snmp_disable_until = src_host->snmp_disable_until;
	dst_host->ipmi_errors_from = src_host->ipmi_errors_from;
	dst_host->ipmi_available = src_host->ipmi_available;
	dst_host->ipmi_disable_until = src_host->ipmi_disable_until;
	dst_host->jmx_errors_from = src_host->jmx_errors_from;
	dst_host->jmx_available = src_host->jmx_available;
	dst_host->jmx_disable_until = src_host->jmx_disable_until;
	dst_host->status = src_host->status;
	strscpy(dst_host->error, src_host->error);
	strscpy(dst_host->snmp_error, src_host->snmp_error);
	strscpy(dst_host->ipmi_error, src_host->ipmi_error);
	strscpy(dst_host->jmx_error, src_host->jmx_error);
	dst_host->tls_connect = src_host->tls_connect;
	dst_host->tls_accept = src_host->tls_accept;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	strscpy(dst_host->tls_issuer, src_host->tls_issuer);
	strscpy(dst_host->tls_subject, src_host->tls_subject);

	if (NULL == src_host->tls_dc_psk)
	{
		*dst_host->tls_psk_identity = '\0';
		*dst_host->tls_psk = '\0';
	}
	else
	{
		strscpy(dst_host->tls_psk_identity, src_host->tls_dc_psk->tls_psk_identity);
		strscpy(dst_host->tls_psk, src_host->tls_dc_psk->tls_psk);
	}
#endif
	if (NULL != (ipmihost = (TRX_DC_IPMIHOST *)trx_hashset_search(&config->ipmihosts, &src_host->hostid)))
	{
		dst_host->ipmi_authtype = ipmihost->ipmi_authtype;
		dst_host->ipmi_privilege = ipmihost->ipmi_privilege;
		strscpy(dst_host->ipmi_username, ipmihost->ipmi_username);
		strscpy(dst_host->ipmi_password, ipmihost->ipmi_password);
	}
	else
	{
		dst_host->ipmi_authtype = TRX_IPMI_DEFAULT_AUTHTYPE;
		dst_host->ipmi_privilege = TRX_IPMI_DEFAULT_PRIVILEGE;
		*dst_host->ipmi_username = '\0';
		*dst_host->ipmi_password = '\0';
	}

	if (NULL != (host_inventory = (TRX_DC_HOST_INVENTORY *)trx_hashset_search(&config->host_inventories, &src_host->hostid)))
		dst_host->inventory_mode = (char)host_inventory->inventory_mode;
	else
		dst_host->inventory_mode = HOST_INVENTORY_DISABLED;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_host_by_hostid                                             *
 *                                                                            *
 * Purpose: Locate host in configuration cache                                *
 *                                                                            *
 * Parameters: host - [OUT] pointer to DC_HOST structure                      *
 *             hostid - [IN] host ID from database                            *
 *                                                                            *
 * Return value: SUCCEED if record located and FAIL otherwise                 *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
int	DCget_host_by_hostid(DC_HOST *host, trx_uint64_t hostid)
{
	int			ret = FAIL;
	const TRX_DC_HOST	*dc_host;

	RDLOCK_CACHE;

	if (NULL != (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &hostid)))
	{
		DCget_host(host, dc_host);
		ret = SUCCEED;
	}

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DCcheck_proxy_permissions                                        *
 *                                                                            *
 * Purpose:                                                                   *
 *     Check access rights for an active proxy and get the proxy ID           *
 *                                                                            *
 * Parameters:                                                                *
 *     host   - [IN] proxy name                                               *
 *     sock   - [IN] connection socket context                                *
 *     hostid - [OUT] proxy ID found in configuration cache                   *
 *     error  - [OUT] error message why access was denied                     *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - access is allowed, FAIL - access denied                      *
 *                                                                            *
 * Comments:                                                                  *
 *     Generating of error messages is done outside of configuration cache    *
 *     locking.                                                               *
 *                                                                            *
 ******************************************************************************/
int	DCcheck_proxy_permissions(const char *host, const trx_socket_t *sock, trx_uint64_t *hostid, char **error)
{
	const TRX_DC_HOST	*dc_host;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_tls_conn_attr_t	attr;

	if (TRX_TCP_SEC_TLS_CERT == sock->connection_type)
	{
		if (SUCCEED != trx_tls_get_attr_cert(sock, &attr))
		{
			*error = trx_strdup(*error, "internal error: cannot get connection attributes");
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
		}
	}
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
	else if (TRX_TCP_SEC_TLS_PSK == sock->connection_type)
	{
		if (SUCCEED != trx_tls_get_attr_psk(sock, &attr))
		{
			*error = trx_strdup(*error, "internal error: cannot get connection attributes");
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
		}
	}
#endif
	else if (TRX_TCP_SEC_UNENCRYPTED != sock->connection_type)
	{
		*error = trx_strdup(*error, "internal error: invalid connection type");
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}
#endif
	RDLOCK_CACHE;

	if (NULL == (dc_host = DCfind_proxy(host)))
	{
		UNLOCK_CACHE;
		*error = trx_dsprintf(*error, "proxy \"%s\" not found", host);
		return FAIL;
	}

	if (HOST_STATUS_PROXY_ACTIVE != dc_host->status)
	{
		UNLOCK_CACHE;
		*error = trx_dsprintf(*error, "proxy \"%s\" is configured in passive mode", host);
		return FAIL;
	}

	if (0 == ((unsigned int)dc_host->tls_accept & sock->connection_type))
	{
		UNLOCK_CACHE;
		*error = trx_dsprintf(NULL, "connection of type \"%s\" is not allowed for proxy \"%s\"",
				trx_tcp_connection_type_name(sock->connection_type), host);
		return FAIL;
	}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (TRX_TCP_SEC_TLS_CERT == sock->connection_type)
	{
		/* simplified match, not compliant with RFC 4517, 4518 */
		if ('\0' != *dc_host->tls_issuer && 0 != strcmp(dc_host->tls_issuer, attr.issuer))
		{
			UNLOCK_CACHE;
			*error = trx_dsprintf(*error, "proxy \"%s\" certificate issuer does not match", host);
			return FAIL;
		}

		/* simplified match, not compliant with RFC 4517, 4518 */
		if ('\0' != *dc_host->tls_subject && 0 != strcmp(dc_host->tls_subject, attr.subject))
		{
			UNLOCK_CACHE;
			*error = trx_dsprintf(*error, "proxy \"%s\" certificate subject does not match", host);
			return FAIL;
		}
	}
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
	else if (TRX_TCP_SEC_TLS_PSK == sock->connection_type)
	{
		if (NULL != dc_host->tls_dc_psk)
		{
			if (strlen(dc_host->tls_dc_psk->tls_psk_identity) != attr.psk_identity_len ||
					0 != memcmp(dc_host->tls_dc_psk->tls_psk_identity, attr.psk_identity,
					attr.psk_identity_len))
			{
				UNLOCK_CACHE;
				*error = trx_dsprintf(*error, "proxy \"%s\" is using false PSK identity", host);
				return FAIL;
			}
		}
		else
		{
			UNLOCK_CACHE;
			*error = trx_dsprintf(*error, "active proxy \"%s\" is connecting with PSK but there is no PSK"
					" in the database for this proxy", host);
			return FAIL;
		}
	}
#endif
#endif
	*hostid = dc_host->hostid;

	UNLOCK_CACHE;

	return SUCCEED;
}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
/******************************************************************************
 *                                                                            *
 * Function: DCget_psk_by_identity                                            *
 *                                                                            *
 * Purpose:                                                                   *
 *     Find PSK with the specified identity in configuration cache            *
 *                                                                            *
 * Parameters:                                                                *
 *     psk_identity - [IN] PSK identity to search for ('\0' terminated)       *
 *     psk_buf      - [OUT] output buffer for PSK value with size             *
 *                    HOST_TLS_PSK_LEN_MAX                                    *
 *     psk_usage    - [OUT] 0 - PSK not found, 1 - found in host PSKs,        *
 *                          2 - found in autoregistration PSK, 3 - found in   *
 *                          both                                              *
 * Return value:                                                              *
 *     PSK length in bytes if PSK found. 0 - if PSK not found.                *
 *                                                                            *
 * Comments:                                                                  *
 *     ATTENTION! This function's address and arguments are described and     *
 *     used in file src/libs/trxcrypto/tls.c for calling this function by     *
 *     pointer. If you ever change this DCget_psk_by_identity() function      *
 *     arguments or return value do not forget to synchronize changes with    *
 *     the src/libs/trxcrypto/tls.c.                                          *
 *                                                                            *
 ******************************************************************************/
size_t	DCget_psk_by_identity(const unsigned char *psk_identity, unsigned char *psk_buf, unsigned int *psk_usage)
{
	const TRX_DC_PSK	*psk_i;
	TRX_DC_PSK		psk_i_local;
	size_t			psk_len = 0;
	unsigned char		autoreg_psk_tmp[HOST_TLS_PSK_LEN_MAX];

	*psk_usage = 0;

	psk_i_local.tls_psk_identity = (const char *)psk_identity;

	RDLOCK_CACHE;

	/* Is it among host PSKs? */
	if (NULL != (psk_i = (TRX_DC_PSK *)trx_hashset_search(&config->psks, &psk_i_local)))
	{
		psk_len = trx_strlcpy((char *)psk_buf, psk_i->tls_psk, HOST_TLS_PSK_LEN_MAX);
		*psk_usage |= TRX_PSK_FOR_HOST;
	}

	/* Does it match autoregistration PSK? */
	if (0 != strcmp(config->autoreg_psk_identity, (const char *)psk_identity))
	{
		UNLOCK_CACHE;
		return psk_len;
	}

	if (0 == *psk_usage)	/* only as autoregistration PSK */
	{
		psk_len = trx_strlcpy((char *)psk_buf, config->autoreg_psk, HOST_TLS_PSK_LEN_MAX);
		UNLOCK_CACHE;
		*psk_usage |= TRX_PSK_FOR_AUTOREG;

		return psk_len;
	}

	/* the requested PSK is used as host PSK and as autoregistration PSK */
	trx_strlcpy((char *)autoreg_psk_tmp, config->autoreg_psk, sizeof(autoreg_psk_tmp));

	UNLOCK_CACHE;

	if (0 == strcmp((const char *)psk_buf, (const char *)autoreg_psk_tmp))
	{
		*psk_usage |= TRX_PSK_FOR_AUTOREG;
		return psk_len;
	}

	treegix_log(LOG_LEVEL_WARNING, "host PSK and autoregistration PSK have the same identity \"%s\" but"
			" different PSK values, autoregistration will not be allowed", psk_identity);
	return psk_len;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: DCget_autoregistration_psk                                       *
 *                                                                            *
 * Purpose:                                                                   *
 *     Copy autoregistration PSK identity and value from configuration cache  *
 *     into caller's buffers                                                  *
 *                                                                            *
 * Parameters:                                                                *
 *     psk_identity_buf     - [OUT] buffer for PSK identity                   *
 *     psk_identity_buf_len - [IN] buffer length for PSK identity             *
 *     psk_buf              - [OUT] buffer for PSK value                      *
 *     psk_buf_len          - [IN] buffer length for PSK value                *
 *                                                                            *
 * Comments: if autoregistration PSK is not configured then empty strings     *
 *           will be copied into buffers                                      *
 *                                                                            *
 ******************************************************************************/
void	DCget_autoregistration_psk(char *psk_identity_buf, size_t psk_identity_buf_len,
		unsigned char *psk_buf, size_t psk_buf_len)
{
	RDLOCK_CACHE;

	trx_strlcpy((char *)psk_identity_buf, config->autoreg_psk_identity, psk_identity_buf_len);
	trx_strlcpy((char *)psk_buf, config->autoreg_psk, psk_buf_len);

	UNLOCK_CACHE;
}

static void	DCget_interface(DC_INTERFACE *dst_interface, const TRX_DC_INTERFACE *src_interface)
{
	if (NULL != src_interface)
	{
		dst_interface->interfaceid = src_interface->interfaceid;
		strscpy(dst_interface->ip_orig, src_interface->ip);
		strscpy(dst_interface->dns_orig, src_interface->dns);
		strscpy(dst_interface->port_orig, src_interface->port);
		dst_interface->useip = src_interface->useip;
		dst_interface->type = src_interface->type;
		dst_interface->main = src_interface->main;
	}
	else
	{
		dst_interface->interfaceid = 0;
		*dst_interface->ip_orig = '\0';
		*dst_interface->dns_orig = '\0';
		*dst_interface->port_orig = '\0';
		dst_interface->useip = 1;
		dst_interface->type = INTERFACE_TYPE_UNKNOWN;
		dst_interface->main = 0;
	}

	dst_interface->addr = (1 == dst_interface->useip ? dst_interface->ip_orig : dst_interface->dns_orig);
	dst_interface->port = 0;
}

static void	DCget_item(DC_ITEM *dst_item, const TRX_DC_ITEM *src_item)
{
	const TRX_DC_NUMITEM		*numitem;
	const TRX_DC_LOGITEM		*logitem;
	const TRX_DC_SNMPITEM		*snmpitem;
	const TRX_DC_TRAPITEM		*trapitem;
	const TRX_DC_IPMIITEM		*ipmiitem;
	const TRX_DC_DBITEM		*dbitem;
	const TRX_DC_SSHITEM		*sshitem;
	const TRX_DC_TELNETITEM		*telnetitem;
	const TRX_DC_SIMPLEITEM		*simpleitem;
	const TRX_DC_JMXITEM		*jmxitem;
	const TRX_DC_CALCITEM		*calcitem;
	const TRX_DC_INTERFACE		*dc_interface;
	const TRX_DC_HTTPITEM		*httpitem;

	dst_item->itemid = src_item->itemid;
	dst_item->type = src_item->type;
	dst_item->value_type = src_item->value_type;
	strscpy(dst_item->key_orig, src_item->key);
	dst_item->key = NULL;
	dst_item->delay = trx_strdup(NULL, src_item->delay);
	dst_item->nextcheck = src_item->nextcheck;
	dst_item->state = src_item->state;
	dst_item->lastclock = src_item->lastclock;
	dst_item->flags = src_item->flags;
	dst_item->lastlogsize = src_item->lastlogsize;
	dst_item->mtime = src_item->mtime;
	dst_item->history = src_item->history;
	dst_item->inventory_link = src_item->inventory_link;
	dst_item->valuemapid = src_item->valuemapid;
	dst_item->status = src_item->status;
	dst_item->history_sec = src_item->history_sec;

	dst_item->error = trx_strdup(NULL, src_item->error);

	switch (src_item->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
		case ITEM_VALUE_TYPE_UINT64:
			numitem = (TRX_DC_NUMITEM *)trx_hashset_search(&config->numitems, &src_item->itemid);

			dst_item->trends = numitem->trends;
			dst_item->units = trx_strdup(NULL, numitem->units);
			break;
		case ITEM_VALUE_TYPE_LOG:
			if (NULL != (logitem = (TRX_DC_LOGITEM *)trx_hashset_search(&config->logitems, &src_item->itemid)))
				strscpy(dst_item->logtimefmt, logitem->logtimefmt);
			else
				*dst_item->logtimefmt = '\0';
			break;
	}

	switch (src_item->type)
	{
		case ITEM_TYPE_SNMPv1:
		case ITEM_TYPE_SNMPv2c:
		case ITEM_TYPE_SNMPv3:
			snmpitem = (TRX_DC_SNMPITEM *)trx_hashset_search(&config->snmpitems, &src_item->itemid);

			strscpy(dst_item->snmp_community_orig, snmpitem->snmp_community);
			strscpy(dst_item->snmp_oid_orig, snmpitem->snmp_oid);
			strscpy(dst_item->snmpv3_securityname_orig, snmpitem->snmpv3_securityname);
			dst_item->snmpv3_securitylevel = snmpitem->snmpv3_securitylevel;
			strscpy(dst_item->snmpv3_authpassphrase_orig, snmpitem->snmpv3_authpassphrase);
			strscpy(dst_item->snmpv3_privpassphrase_orig, snmpitem->snmpv3_privpassphrase);
			dst_item->snmpv3_authprotocol = snmpitem->snmpv3_authprotocol;
			dst_item->snmpv3_privprotocol = snmpitem->snmpv3_privprotocol;
			strscpy(dst_item->snmpv3_contextname_orig, snmpitem->snmpv3_contextname);

			dst_item->snmp_community = NULL;
			dst_item->snmp_oid = NULL;
			dst_item->snmpv3_securityname = NULL;
			dst_item->snmpv3_authpassphrase = NULL;
			dst_item->snmpv3_privpassphrase = NULL;
			dst_item->snmpv3_contextname = NULL;
			break;
		case ITEM_TYPE_TRAPPER:
			if (NULL != (trapitem = (TRX_DC_TRAPITEM *)trx_hashset_search(&config->trapitems, &src_item->itemid)))
				strscpy(dst_item->trapper_hosts, trapitem->trapper_hosts);
			else
				*dst_item->trapper_hosts = '\0';
			break;
		case ITEM_TYPE_IPMI:
			if (NULL != (ipmiitem = (TRX_DC_IPMIITEM *)trx_hashset_search(&config->ipmiitems, &src_item->itemid)))
				strscpy(dst_item->ipmi_sensor, ipmiitem->ipmi_sensor);
			else
				*dst_item->ipmi_sensor = '\0';
			break;
		case ITEM_TYPE_DB_MONITOR:
			if (NULL != (dbitem = (TRX_DC_DBITEM *)trx_hashset_search(&config->dbitems, &src_item->itemid)))
			{
				dst_item->params = trx_strdup(NULL, dbitem->params);
				strscpy(dst_item->username_orig, dbitem->username);
				strscpy(dst_item->password_orig, dbitem->password);
			}
			else
			{
				dst_item->params = trx_strdup(NULL, "");
				*dst_item->username_orig = '\0';
				*dst_item->password_orig = '\0';
			}
			dst_item->username = NULL;
			dst_item->password = NULL;

			break;
		case ITEM_TYPE_SSH:
			if (NULL != (sshitem = (TRX_DC_SSHITEM *)trx_hashset_search(&config->sshitems, &src_item->itemid)))
			{
				dst_item->authtype = sshitem->authtype;
				strscpy(dst_item->username_orig, sshitem->username);
				strscpy(dst_item->publickey_orig, sshitem->publickey);
				strscpy(dst_item->privatekey_orig, sshitem->privatekey);
				strscpy(dst_item->password_orig, sshitem->password);
				dst_item->params = trx_strdup(NULL, sshitem->params);
			}
			else
			{
				dst_item->authtype = 0;
				*dst_item->username_orig = '\0';
				*dst_item->publickey_orig = '\0';
				*dst_item->privatekey_orig = '\0';
				*dst_item->password_orig = '\0';
				dst_item->params = trx_strdup(NULL, "");
			}
			dst_item->username = NULL;
			dst_item->publickey = NULL;
			dst_item->privatekey = NULL;
			dst_item->password = NULL;
			break;
		case ITEM_TYPE_HTTPAGENT:
			if (NULL != (httpitem = (TRX_DC_HTTPITEM *)trx_hashset_search(&config->httpitems, &src_item->itemid)))
			{
				strscpy(dst_item->timeout_orig, httpitem->timeout);
				strscpy(dst_item->url_orig, httpitem->url);
				strscpy(dst_item->query_fields_orig, httpitem->query_fields);
				strscpy(dst_item->status_codes_orig, httpitem->status_codes);
				dst_item->follow_redirects = httpitem->follow_redirects;
				dst_item->post_type = httpitem->post_type;
				strscpy(dst_item->http_proxy_orig, httpitem->http_proxy);
				dst_item->headers = trx_strdup(NULL, httpitem->headers);
				dst_item->retrieve_mode = httpitem->retrieve_mode;
				dst_item->request_method = httpitem->request_method;
				dst_item->output_format = httpitem->output_format;
				strscpy(dst_item->ssl_cert_file_orig, httpitem->ssl_cert_file);
				strscpy(dst_item->ssl_key_file_orig, httpitem->ssl_key_file);
				strscpy(dst_item->ssl_key_password_orig, httpitem->ssl_key_password);
				dst_item->verify_peer = httpitem->verify_peer;
				dst_item->verify_host = httpitem->verify_host;
				dst_item->authtype = httpitem->authtype;
				strscpy(dst_item->username_orig, httpitem->username);
				strscpy(dst_item->password_orig, httpitem->password);
				dst_item->posts = trx_strdup(NULL, httpitem->posts);
				dst_item->allow_traps = httpitem->allow_traps;
				strscpy(dst_item->trapper_hosts, httpitem->trapper_hosts);
			}
			else
			{
				*dst_item->timeout_orig = '\0';
				*dst_item->url_orig = '\0';
				*dst_item->query_fields_orig = '\0';
				*dst_item->status_codes_orig = '\0';
				dst_item->follow_redirects = 0;
				dst_item->post_type = 0;
				*dst_item->http_proxy_orig = '\0';
				dst_item->headers = trx_strdup(NULL, "");
				dst_item->retrieve_mode = 0;
				dst_item->request_method = 0;
				dst_item->output_format = 0;
				*dst_item->ssl_cert_file_orig = '\0';
				*dst_item->ssl_key_file_orig = '\0';
				*dst_item->ssl_key_password_orig = '\0';
				dst_item->verify_peer = 0;
				dst_item->verify_host = 0;
				dst_item->authtype = 0;
				*dst_item->username_orig = '\0';
				*dst_item->password_orig = '\0';
				dst_item->posts = trx_strdup(NULL, "");
				dst_item->allow_traps = 0;
				*dst_item->trapper_hosts = '\0';
			}
			dst_item->timeout = NULL;
			dst_item->url = NULL;
			dst_item->query_fields = NULL;
			dst_item->status_codes = NULL;
			dst_item->http_proxy = NULL;
			dst_item->ssl_cert_file = NULL;
			dst_item->ssl_key_file = NULL;
			dst_item->ssl_key_password = NULL;
			dst_item->username = NULL;
			dst_item->password = NULL;
			break;
		case ITEM_TYPE_TELNET:
			if (NULL != (telnetitem = (TRX_DC_TELNETITEM *)trx_hashset_search(&config->telnetitems, &src_item->itemid)))
			{
				strscpy(dst_item->username_orig, telnetitem->username);
				strscpy(dst_item->password_orig, telnetitem->password);
				dst_item->params = trx_strdup(NULL, telnetitem->params);
			}
			else
			{
				*dst_item->username_orig = '\0';
				*dst_item->password_orig = '\0';
				dst_item->params = trx_strdup(NULL, "");
			}
			dst_item->username = NULL;
			dst_item->password = NULL;
			break;
		case ITEM_TYPE_SIMPLE:
			if (NULL != (simpleitem = (TRX_DC_SIMPLEITEM *)trx_hashset_search(&config->simpleitems, &src_item->itemid)))
			{
				strscpy(dst_item->username_orig, simpleitem->username);
				strscpy(dst_item->password_orig, simpleitem->password);
			}
			else
			{
				*dst_item->username_orig = '\0';
				*dst_item->password_orig = '\0';
			}
			dst_item->username = NULL;
			dst_item->password = NULL;
			break;
		case ITEM_TYPE_JMX:
			if (NULL != (jmxitem = (TRX_DC_JMXITEM *)trx_hashset_search(&config->jmxitems, &src_item->itemid)))
			{
				strscpy(dst_item->username_orig, jmxitem->username);
				strscpy(dst_item->password_orig, jmxitem->password);
				strscpy(dst_item->jmx_endpoint_orig, jmxitem->jmx_endpoint);
			}
			else
			{
				*dst_item->username_orig = '\0';
				*dst_item->password_orig = '\0';
				*dst_item->jmx_endpoint_orig = '\0';
			}
			dst_item->username = NULL;
			dst_item->password = NULL;
			dst_item->jmx_endpoint = NULL;
			break;
		case ITEM_TYPE_CALCULATED:
			calcitem = (TRX_DC_CALCITEM *)trx_hashset_search(&config->calcitems, &src_item->itemid);
			dst_item->params = trx_strdup(NULL, NULL != calcitem ? calcitem->params : "");
			break;
		default:
			/* nothing to do */;
	}

	dc_interface = (TRX_DC_INTERFACE *)trx_hashset_search(&config->interfaces, &src_item->interfaceid);

	DCget_interface(&dst_item->interface, dc_interface);

	if ('\0' != *src_item->port)
	{
		switch (src_item->type)
		{
			case ITEM_TYPE_SNMPv1:
			case ITEM_TYPE_SNMPv2c:
			case ITEM_TYPE_SNMPv3:
				strscpy(dst_item->interface.port_orig, src_item->port);
				break;
			default:
				/* nothing to do */;
		}
	}
}

void	DCconfig_clean_items(DC_ITEM *items, int *errcodes, size_t num)
{
	size_t	i;

	for (i = 0; i < num; i++)
	{
		if (NULL != errcodes && SUCCEED != errcodes[i])
			continue;

		if (ITEM_VALUE_TYPE_FLOAT == items[i].value_type || ITEM_VALUE_TYPE_UINT64 == items[i].value_type)
		{
			trx_free(items[i].units);
		}

		switch (items[i].type)
		{
			case ITEM_TYPE_HTTPAGENT:
				trx_free(items[i].headers);
				trx_free(items[i].posts);
				break;
			case ITEM_TYPE_DB_MONITOR:
			case ITEM_TYPE_SSH:
			case ITEM_TYPE_TELNET:
			case ITEM_TYPE_CALCULATED:
				trx_free(items[i].params);
				break;
		}

		trx_free(items[i].delay);
		trx_free(items[i].error);
	}
}

static void	DCget_function(DC_FUNCTION *dst_function, const TRX_DC_FUNCTION *src_function)
{
	size_t	sz_function, sz_parameter;

	dst_function->functionid = src_function->functionid;
	dst_function->triggerid = src_function->triggerid;
	dst_function->itemid = src_function->itemid;

	sz_function = strlen(src_function->function) + 1;
	sz_parameter = strlen(src_function->parameter) + 1;
	dst_function->function = (char *)trx_malloc(NULL, sz_function + sz_parameter);
	dst_function->parameter = dst_function->function + sz_function;
	memcpy(dst_function->function, src_function->function, sz_function);
	memcpy(dst_function->parameter, src_function->parameter, sz_parameter);
}

static void	DCget_trigger(DC_TRIGGER *dst_trigger, const TRX_DC_TRIGGER *src_trigger)
{
	int	i;

	dst_trigger->triggerid = src_trigger->triggerid;
	dst_trigger->description = trx_strdup(NULL, src_trigger->description);
	dst_trigger->expression_orig = trx_strdup(NULL, src_trigger->expression);
	dst_trigger->recovery_expression_orig = trx_strdup(NULL, src_trigger->recovery_expression);
	dst_trigger->error = trx_strdup(NULL, src_trigger->error);
	dst_trigger->timespec.sec = 0;
	dst_trigger->timespec.ns = 0;
	dst_trigger->priority = src_trigger->priority;
	dst_trigger->type = src_trigger->type;
	dst_trigger->value = src_trigger->value;
	dst_trigger->state = src_trigger->state;
	dst_trigger->new_value = TRIGGER_VALUE_UNKNOWN;
	dst_trigger->lastchange = src_trigger->lastchange;
	dst_trigger->topoindex = src_trigger->topoindex;
	dst_trigger->status = src_trigger->status;
	dst_trigger->recovery_mode = src_trigger->recovery_mode;
	dst_trigger->correlation_mode = src_trigger->correlation_mode;
	dst_trigger->correlation_tag = trx_strdup(NULL, src_trigger->correlation_tag);
	dst_trigger->opdata = trx_strdup(NULL, src_trigger->opdata);
	dst_trigger->flags = 0;

	dst_trigger->expression = NULL;
	dst_trigger->recovery_expression = NULL;
	dst_trigger->new_error = NULL;

	dst_trigger->expression = trx_strdup(NULL, src_trigger->expression);
	dst_trigger->recovery_expression = trx_strdup(NULL, src_trigger->recovery_expression);

	trx_vector_ptr_create(&dst_trigger->tags);

	if (0 != src_trigger->tags.values_num)
	{
		trx_vector_ptr_reserve(&dst_trigger->tags, src_trigger->tags.values_num);

		for (i = 0; i < src_trigger->tags.values_num; i++)
		{
			const trx_dc_trigger_tag_t	*dc_trigger_tag = (const trx_dc_trigger_tag_t *)src_trigger->tags.values[i];
			trx_tag_t			*tag;

			tag = (trx_tag_t *)trx_malloc(NULL, sizeof(trx_tag_t));
			tag->tag = trx_strdup(NULL, dc_trigger_tag->tag);
			tag->value = trx_strdup(NULL, dc_trigger_tag->value);

			trx_vector_ptr_append(&dst_trigger->tags, tag);
		}
	}
}

void	trx_free_tag(trx_tag_t *tag)
{
	trx_free(tag->tag);
	trx_free(tag->value);
	trx_free(tag);
}

void	trx_free_item_tag(trx_item_tag_t *item_tag)
{
	trx_free(item_tag->tag.tag);
	trx_free(item_tag->tag.value);
	trx_free(item_tag);
}

static void	DCclean_trigger(DC_TRIGGER *trigger)
{
	trx_free(trigger->new_error);
	trx_free(trigger->error);
	trx_free(trigger->expression_orig);
	trx_free(trigger->recovery_expression_orig);
	trx_free(trigger->expression);
	trx_free(trigger->recovery_expression);
	trx_free(trigger->description);
	trx_free(trigger->correlation_tag);
	trx_free(trigger->opdata);

	trx_vector_ptr_clear_ext(&trigger->tags, (trx_clean_func_t)trx_free_tag);
	trx_vector_ptr_destroy(&trigger->tags);
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_items_by_keys                                       *
 *                                                                            *
 * Purpose: locate item in configuration cache by host and key                *
 *                                                                            *
 * Parameters: items    - [OUT] pointer to array of DC_ITEM structures        *
 *             keys     - [IN] list of item keys with host names              *
 *             errcodes - [OUT] SUCCEED if record located and FAIL otherwise  *
 *             num      - [IN] number of elements in items, keys, errcodes    *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_get_items_by_keys(DC_ITEM *items, trx_host_key_t *keys, int *errcodes, size_t num)
{
	size_t			i;
	const TRX_DC_ITEM	*dc_item;
	const TRX_DC_HOST	*dc_host;

	RDLOCK_CACHE;

	for (i = 0; i < num; i++)
	{
		if (NULL == (dc_host = DCfind_host(keys[i].host)) ||
				NULL == (dc_item = DCfind_item(dc_host->hostid, keys[i].key)))
		{
			errcodes[i] = FAIL;
			continue;
		}

		DCget_host(&items[i].host, dc_host);
		DCget_item(&items[i], dc_item);
		errcodes[i] = SUCCEED;
	}

	UNLOCK_CACHE;
}

int	DCconfig_get_hostid_by_name(const char *host, trx_uint64_t *hostid)
{
	const TRX_DC_HOST	*dc_host;
	int			ret;

	RDLOCK_CACHE;

	if (NULL != (dc_host = DCfind_host(host)))
	{
		*hostid = dc_host->hostid;
		ret = SUCCEED;
	}
	else
		ret = FAIL;

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_items_by_itemids                                    *
 *                                                                            *
 * Purpose: Get item with specified ID                                        *
 *                                                                            *
 * Parameters: items    - [OUT] pointer to DC_ITEM structures                 *
 *             itemids  - [IN] array of item IDs                              *
 *             errcodes - [OUT] SUCCEED if item found, otherwise FAIL         *
 *             num      - [IN] number of elements                             *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_get_items_by_itemids(DC_ITEM *items, const trx_uint64_t *itemids, int *errcodes, size_t num)
{
	size_t			i;
	const TRX_DC_ITEM	*dc_item;
	const TRX_DC_HOST	*dc_host;

	RDLOCK_CACHE;

	for (i = 0; i < num; i++)
	{
		if (NULL == (dc_item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &itemids[i])) ||
				NULL == (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)))
		{
			errcodes[i] = FAIL;
			continue;
		}

		DCget_host(&items[i].host, dc_host);
		DCget_item(&items[i], dc_item);
		errcodes[i] = SUCCEED;
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_preproc_item_init                                             *
 *                                                                            *
 * Purpose: initialize new preprocessor item from configuration cache         *
 *                                                                            *
 * Parameters: item   - [OUT] the item to initialize                          *
 *             itemid - [IN] the item identifier                              *
 *                                                                            *
 * Return value: SUCCEED - the item was initialized successfully              *
 *               FAIL    - item with the specified itemid is not cached or    *
 *                         monitored                                          *
 *                                                                            *
 ******************************************************************************/
static int	dc_preproc_item_init(trx_preproc_item_t *item, trx_uint64_t itemid)
{
	const TRX_DC_ITEM	*dc_item;
	const TRX_DC_HOST	*dc_host;

	if (NULL == (dc_item = (const TRX_DC_ITEM *)trx_hashset_search(&config->items, &itemid)))
		return FAIL;

	if (ITEM_STATUS_ACTIVE != dc_item->status)
		return FAIL;

	if (NULL == (dc_host = (const TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)))
		return FAIL;

	if (HOST_STATUS_MONITORED != dc_host->status)
		return FAIL;

	item->itemid = itemid;
	item->type = dc_item->type;
	item->value_type = dc_item->value_type;

	item->dep_itemids = NULL;
	item->dep_itemids_num = 0;

	item->preproc_ops = NULL;
	item->preproc_ops_num = 0;
	item->update_time = 0;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_preprocessable_items                                *
 *                                                                            *
 * Purpose: get preprocessable items:                                         *
 *              * items with preprocessing steps                              *
 *              * items with dependent items                                  *
 *              * internal items                                              *
 *                                                                            *
 * Parameters: items       - [IN/OUT] hashset with DC_ITEMs                   *
 *             timestamp   - [IN/OUT] timestamp of a last update              *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_get_preprocessable_items(trx_hashset_t *items, int *timestamp)
{
	const TRX_DC_PREPROCITEM	*dc_preprocitem;
	const TRX_DC_MASTERITEM		*dc_masteritem;
	const TRX_DC_ITEM		*dc_item;
	const trx_dc_preproc_op_t	*dc_op;
	trx_preproc_item_t		*item, item_local;
	trx_hashset_iter_t		iter;
	trx_preproc_op_t		*op;
	int				i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* no changes */
	if (0 != *timestamp && *timestamp == config->item_sync_ts)
		goto out;

	trx_hashset_clear(items);
	*timestamp = config->item_sync_ts;

	RDLOCK_CACHE;

	trx_hashset_iter_reset(&config->preprocitems, &iter);
	while (NULL != (dc_preprocitem = (const TRX_DC_PREPROCITEM *)trx_hashset_iter_next(&iter)))
	{
		if (FAIL == dc_preproc_item_init(&item_local, dc_preprocitem->itemid))
			continue;

		item = (trx_preproc_item_t *)trx_hashset_insert(items, &item_local, sizeof(item_local));

		item->preproc_ops_num = dc_preprocitem->preproc_ops.values_num;
		item->preproc_ops = (trx_preproc_op_t *)trx_malloc(NULL, sizeof(trx_preproc_op_t) * item->preproc_ops_num);
		item->update_time = dc_preprocitem->update_time;

		for (i = 0; i < dc_preprocitem->preproc_ops.values_num; i++)
		{
			dc_op = (const trx_dc_preproc_op_t *)dc_preprocitem->preproc_ops.values[i];
			op = &item->preproc_ops[i];
			op->type = dc_op->type;
			op->params = trx_strdup(NULL, dc_op->params);
			op->error_handler = dc_op->error_handler;
			op->error_handler_params = trx_strdup(NULL, dc_op->error_handler_params);
		}
	}

	trx_hashset_iter_reset(&config->masteritems, &iter);
	while (NULL != (dc_masteritem = (const TRX_DC_MASTERITEM *)trx_hashset_iter_next(&iter)))
	{
		if (NULL == (item = (trx_preproc_item_t *)trx_hashset_search(items, &dc_masteritem->itemid)))
		{
			if (FAIL == dc_preproc_item_init(&item_local, dc_masteritem->itemid))
				continue;

			item = (trx_preproc_item_t *)trx_hashset_insert(items, &item_local, sizeof(item_local));
		}

		item->dep_itemids_num = dc_masteritem->dep_itemids.values_num;

		item->dep_itemids = (trx_uint64_pair_t *)trx_malloc(NULL,
				sizeof(trx_uint64_pair_t) * item->dep_itemids_num);

		memcpy(item->dep_itemids, dc_masteritem->dep_itemids.values,
				sizeof(trx_uint64_pair_t) * item->dep_itemids_num);
	}

	trx_hashset_iter_reset(&config->items, &iter);
	while (NULL != (dc_item = (const TRX_DC_ITEM *)trx_hashset_iter_next(&iter)))
	{
		if (ITEM_TYPE_INTERNAL != dc_item->type)
			continue;

		if (NULL == (item = (trx_preproc_item_t *)trx_hashset_search(items, &dc_item->itemid)))
		{
			if (FAIL == dc_preproc_item_init(&item_local, dc_item->itemid))
				continue;

			trx_hashset_insert(items, &item_local, sizeof(item_local));
		}
	}

	UNLOCK_CACHE;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s() items:%d", __func__, items->num_data);
}

void	DCconfig_get_hosts_by_itemids(DC_HOST *hosts, const trx_uint64_t *itemids, int *errcodes, size_t num)
{
	size_t			i;
	const TRX_DC_ITEM	*dc_item;
	const TRX_DC_HOST	*dc_host;

	RDLOCK_CACHE;

	for (i = 0; i < num; i++)
	{
		if (NULL == (dc_item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &itemids[i])) ||
				NULL == (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)))
		{
			errcodes[i] = FAIL;
			continue;
		}

		DCget_host(&hosts[i], dc_host);
		errcodes[i] = SUCCEED;
	}

	UNLOCK_CACHE;
}

void	DCconfig_get_triggers_by_triggerids(DC_TRIGGER *triggers, const trx_uint64_t *triggerids, int *errcode,
		size_t num)
{
	size_t			i;
	const TRX_DC_TRIGGER	*dc_trigger;

	RDLOCK_CACHE;

	for (i = 0; i < num; i++)
	{
		if (NULL == (dc_trigger = (const TRX_DC_TRIGGER *)trx_hashset_search(&config->triggers, &triggerids[i])))
		{
			errcode[i] = FAIL;
			continue;
		}

		DCget_trigger(&triggers[i], dc_trigger);
		errcode[i] = SUCCEED;
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_functions_by_functionids                            *
 *                                                                            *
 * Purpose: Get functions by IDs                                              *
 *                                                                            *
 * Parameters: functions   - [OUT] pointer to DC_FUNCTION structures          *
 *             functionids - [IN] array of function IDs                       *
 *             errcodes    - [OUT] SUCCEED if item found, otherwise FAIL      *
 *             num         - [IN] number of elements                          *
 *                                                                            *
 * Author: Aleksandrs Saveljevs, Alexander Vladishev                          *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_get_functions_by_functionids(DC_FUNCTION *functions, trx_uint64_t *functionids, int *errcodes,
		size_t num)
{
	size_t			i;
	const TRX_DC_FUNCTION	*dc_function;

	RDLOCK_CACHE;

	for (i = 0; i < num; i++)
	{
		if (NULL == (dc_function = (TRX_DC_FUNCTION *)trx_hashset_search(&config->functions, &functionids[i])))
		{
			errcodes[i] = FAIL;
			continue;
		}

		DCget_function(&functions[i], dc_function);
		errcodes[i] = SUCCEED;
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_clean_functions                                         *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_clean_functions(DC_FUNCTION *functions, int *errcodes, size_t num)
{
	size_t	i;

	for (i = 0; i < num; i++)
	{
		if (SUCCEED != errcodes[i])
			continue;

		trx_free(functions[i].function);
	}
}

void	DCconfig_clean_triggers(DC_TRIGGER *triggers, int *errcodes, size_t num)
{
	size_t	i;

	for (i = 0; i < num; i++)
	{
		if (SUCCEED != errcodes[i])
			continue;

		DCclean_trigger(&triggers[i]);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_lock_triggers_by_history_items                          *
 *                                                                            *
 * Purpose: Lock triggers for specified items so that multiple processes do   *
 *          not process one trigger simultaneously. Otherwise, this leads to  *
 *          problems like multiple successive OK events or escalations being  *
 *          started and not cancelled, because they are not seen in parallel  *
 *          transactions.                                                     *
 *                                                                            *
 * Parameters: history_items - [IN/OUT] list of history items history syncer  *
 *                                    wishes to take for processing; on       *
 *                                    output, the item locked field is set    *
 *                                    to 0 if the corresponding item cannot   *
 *                                    be taken                                *
 *             triggerids  - [OUT] list of trigger IDs that this function has *
 *                                 locked for processing; unlock those using  *
 *                                 DCconfig_unlock_triggers() function        *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 * Comments: This does not solve the problem fully (e.g., TRX-7484). There is *
 *           a significant time period between the place where we lock the    *
 *           triggers and the place where we process them. So it could happen *
 *           that a configuration cache update happens after we have locked   *
 *           the triggers and it turns out that in the updated configuration  *
 *           there is a new trigger for two of the items that two different   *
 *           history syncers have taken for processing. In that situation,    *
 *           the problem we are solving here might still happen. However,     *
 *           locking triggers makes this problem much less likely and only in *
 *           case configuration changes. On a stable configuration, it should *
 *           work without any problems.                                       *
 *                                                                            *
 * Return value: the number of items available for processing (unlocked).     *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_lock_triggers_by_history_items(trx_vector_ptr_t *history_items, trx_vector_uint64_t *triggerids)
{
	int			i, j, locked_num = 0;
	const TRX_DC_ITEM	*dc_item;
	TRX_DC_TRIGGER		*dc_trigger;
	trx_hc_item_t		*history_item;

	WRLOCK_CACHE;

	for (i = 0; i < history_items->values_num; i++)
	{
		history_item = (trx_hc_item_t *)history_items->values[i];

		if (0 != (TRX_DC_FLAG_NOVALUE & history_item->tail->flags))
			continue;

		if (NULL == (dc_item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &history_item->itemid)))
			continue;

		if (NULL == dc_item->triggers)
			continue;

		for (j = 0; NULL != (dc_trigger = dc_item->triggers[j]); j++)
		{
			if (TRIGGER_STATUS_ENABLED != dc_trigger->status)
				continue;

			if (1 == dc_trigger->locked)
			{
				locked_num++;
				history_item->status = TRX_HC_ITEM_STATUS_BUSY;
				goto next;
			}
		}

		for (j = 0; NULL != (dc_trigger = dc_item->triggers[j]); j++)
		{
			if (TRIGGER_STATUS_ENABLED != dc_trigger->status)
				continue;

			dc_trigger->locked = 1;
			trx_vector_uint64_append(triggerids, dc_trigger->triggerid);
		}
next:;
	}

	UNLOCK_CACHE;

	return history_items->values_num - locked_num;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_lock_triggers_by_triggerids                             *
 *                                                                            *
 * Purpose: Lock triggers so that multiple processes do not process one       *
 *          trigger simultaneously.                                           *
 *                                                                            *
 * Parameters: triggerids_in  - [IN] ids of triggers to lock                  *
 *             triggerids_out - [OUT] ids of locked triggers                  *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_lock_triggers_by_triggerids(trx_vector_uint64_t *triggerids_in, trx_vector_uint64_t *triggerids_out)
{
	int		i;
	TRX_DC_TRIGGER	*dc_trigger;

	if (0 == triggerids_in->values_num)
		return;

	WRLOCK_CACHE;

	for (i = 0; i < triggerids_in->values_num; i++)
	{
		if (NULL == (dc_trigger = (TRX_DC_TRIGGER *)trx_hashset_search(&config->triggers, &triggerids_in->values[i])))
			continue;

		if (1 == dc_trigger->locked)
			continue;

		dc_trigger->locked = 1;
		trx_vector_uint64_append(triggerids_out, dc_trigger->triggerid);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_unlock_triggers                                         *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_unlock_triggers(const trx_vector_uint64_t *triggerids)
{
	int		i;
	TRX_DC_TRIGGER	*dc_trigger;

	WRLOCK_CACHE;

	for (i = 0; i < triggerids->values_num; i++)
	{
		if (NULL == (dc_trigger = (TRX_DC_TRIGGER *)trx_hashset_search(&config->triggers, &triggerids->values[i])))
			continue;

		dc_trigger->locked = 0;
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_unlock_all_triggers                                     *
 *                                                                            *
 * Purpose: Unlocks all locked triggers before doing full history sync at     *
 *          program exit                                                      *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_unlock_all_triggers(void)
{
	TRX_DC_TRIGGER		*dc_trigger;
	trx_hashset_iter_t	iter;

	WRLOCK_CACHE;

	trx_hashset_iter_reset(&config->triggers, &iter);

	while (NULL != (dc_trigger = (TRX_DC_TRIGGER *)trx_hashset_iter_next(&iter)))
		dc_trigger->locked = 0;

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_triggers_by_itemids                                 *
 *                                                                            *
 * Purpose: get enabled triggers for specified items                          *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_get_triggers_by_itemids(trx_hashset_t *trigger_info, trx_vector_ptr_t *trigger_order,
		const trx_uint64_t *itemids, const trx_timespec_t *timespecs, int itemids_num)
{
	int			i, j, found;
	const TRX_DC_ITEM	*dc_item;
	const TRX_DC_TRIGGER	*dc_trigger;
	DC_TRIGGER		*trigger;

	RDLOCK_CACHE;

	for (i = 0; i < itemids_num; i++)
	{
		/* skip items which are not in configuration cache and items without triggers */

		if (NULL == (dc_item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &itemids[i])) || NULL == dc_item->triggers)
			continue;

		/* process all triggers for the specified item */

		for (j = 0; NULL != (dc_trigger = dc_item->triggers[j]); j++)
		{
			if (TRIGGER_STATUS_ENABLED != dc_trigger->status)
				continue;

			/* find trigger by id or create a new record in hashset if not found */
			trigger = (DC_TRIGGER *)DCfind_id(trigger_info, dc_trigger->triggerid, sizeof(DC_TRIGGER), &found);

			if (0 == found)
			{
				DCget_trigger(trigger, dc_trigger);
				trx_vector_ptr_append(trigger_order, trigger);
			}

			/* copy latest change timestamp */

			if (trigger->timespec.sec < timespecs[i].sec ||
					(trigger->timespec.sec == timespecs[i].sec &&
					trigger->timespec.ns < timespecs[i].ns))
			{
				/* DCconfig_get_triggers_by_itemids() function is called during trigger processing */
				/* when syncing history cache. A trigger cannot be processed by two syncers at the */
				/* same time, so its safe to update trigger timespec within read lock.             */
				trigger->timespec = timespecs[i];
			}
		}
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_find_active_time_function                               *
 *                                                                            *
 * Purpose: checks if the expression contains time based functions            *
 *                                                                            *
 ******************************************************************************/
static int	DCconfig_find_active_time_function(const char *expression)
{
	trx_uint64_t		functionid;
	const TRX_DC_FUNCTION	*dc_function;
	const TRX_DC_HOST	*dc_host;
	const TRX_DC_ITEM	*dc_item;

	while (SUCCEED == get_N_functionid(expression, 1, &functionid, &expression))
	{
		if (NULL == (dc_function = (TRX_DC_FUNCTION *)trx_hashset_search(&config->functions, &functionid)))
			continue;

		if (1 == dc_function->timer)
		{
			if (NULL == (dc_item = trx_hashset_search(&config->items, &dc_function->itemid)))
				continue;

			if (NULL == (dc_host = trx_hashset_search(&config->hosts, &dc_item->hostid)))
				continue;

			if (SUCCEED != DCin_maintenance_without_data_collection(dc_host, dc_item))
				return SUCCEED;
		}
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_get_timer_triggers_by_triggerids                          *
 *                                                                            *
 * Purpose: gets timer triggers from cache                                    *
 *                                                                            *
 * Parameters: trigger_info  - [IN/OUT] triggers                              *
 *             trigger_order - [IN/OUT] triggers in processing order          *
 *             triggerids    - [IN] identifiers of the triggers to retrieve   *
 *             ts            - [IN] current timestamp                         *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_get_timer_triggers_by_triggerids(trx_hashset_t *trigger_info, trx_vector_ptr_t *trigger_order,
		const trx_vector_uint64_t *triggerids, const trx_timespec_t *ts)
{
	int		i;
	TRX_DC_TRIGGER	*dc_trigger;

	RDLOCK_CACHE;

	for (i = 0; i < triggerids->values_num; i++)
	{
		if (NULL != (dc_trigger = (TRX_DC_TRIGGER *)trx_hashset_search(&config->triggers,
				&triggerids->values[i])))
		{
			DC_TRIGGER	*trigger, trigger_local;
			unsigned char	flags;

			if (SUCCEED == DCconfig_find_active_time_function(dc_trigger->expression))
			{
				flags = TRX_DC_TRIGGER_PROBLEM_EXPRESSION;
			}
			else
			{
				if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION != dc_trigger->recovery_mode)
					continue;

				if (TRIGGER_VALUE_PROBLEM != dc_trigger->value)
					continue;

				if (SUCCEED != DCconfig_find_active_time_function(dc_trigger->recovery_expression))
					continue;

				flags = 0;
			}

			trigger_local.triggerid = dc_trigger->triggerid;
			trigger = (DC_TRIGGER *)trx_hashset_insert(trigger_info, &trigger_local, sizeof(trigger_local));
			DCget_trigger(trigger, dc_trigger);

			/* DCconfig_get_triggers_by_itemids() function is called during trigger processing */
			/* when syncing history cache. A trigger cannot be processed by two syncers at the */
			/* same time, so its safe to update trigger timespec within read lock.             */
			trigger->timespec = *ts;
			trigger->flags = flags;

			trx_vector_ptr_append(trigger_order, trigger);
		}
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_get_timer_triggerids                                      *
 *                                                                            *
 * Purpose: gets triggerids from timer queue                                  *
 *                                                                            *
 * Parameters: triggerids - [OUT] timer tirggerids to process                 *
 *             now        - [IN] current time                                 *
 *             limit      - [IN] the maximum number of triggerids to return   *
 *                                                                            *
 * Comments: This function locks returned triggerids in configuration cache.  *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_get_timer_triggerids(trx_vector_uint64_t *triggerids, int now, int limit)
{
	WRLOCK_CACHE;

	while (SUCCEED != trx_binary_heap_empty(&config->timer_queue) && 0 != limit)
	{
		trx_binary_heap_elem_t	*elem;
		TRX_DC_TRIGGER		*dc_trigger;

		elem = trx_binary_heap_find_min(&config->timer_queue);
		dc_trigger = (TRX_DC_TRIGGER *)elem->data;

		if (dc_trigger->nextcheck > now)
			break;

		/* locked triggers are already being processed by other processes, we can skip them */
		if (0 == dc_trigger->locked)
		{
			trx_vector_uint64_append(triggerids, dc_trigger->triggerid);
			dc_trigger->locked = 1;
			limit--;
		}

		dc_trigger->nextcheck = dc_timer_calculate_nextcheck(now, dc_trigger->triggerid);
		trx_binary_heap_update_direct(&config->timer_queue, elem);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_clear_timer_queue                                         *
 *                                                                            *
 * Purpose: clears timer trigger queue                                        *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_clear_timer_queue(void)
{
	WRLOCK_CACHE;
	trx_binary_heap_clear(&config->timer_queue);
	UNLOCK_CACHE;
}

void	DCfree_triggers(trx_vector_ptr_t *triggers)
{
	int	i;

	for (i = 0; i < triggers->values_num; i++)
		DCclean_trigger((DC_TRIGGER *)triggers->values[i]);

	trx_vector_ptr_clear(triggers);
}

void	DCconfig_update_interface_snmp_stats(trx_uint64_t interfaceid, int max_snmp_succeed, int min_snmp_fail)
{
	TRX_DC_INTERFACE	*dc_interface;

	WRLOCK_CACHE;

	if (NULL != (dc_interface = (TRX_DC_INTERFACE *)trx_hashset_search(&config->interfaces, &interfaceid)) &&
			SNMP_BULK_ENABLED == dc_interface->bulk)
	{
		if (dc_interface->max_snmp_succeed < max_snmp_succeed)
			dc_interface->max_snmp_succeed = (unsigned char)max_snmp_succeed;

		if (dc_interface->min_snmp_fail > min_snmp_fail)
			dc_interface->min_snmp_fail = (unsigned char)min_snmp_fail;
	}

	UNLOCK_CACHE;
}

static int	DCconfig_get_suggested_snmp_vars_nolock(trx_uint64_t interfaceid, int *bulk)
{
	int			num;
	const TRX_DC_INTERFACE	*dc_interface;

	dc_interface = (const TRX_DC_INTERFACE *)trx_hashset_search(&config->interfaces, &interfaceid);

	if (NULL != bulk)
		*bulk = (NULL == dc_interface ? SNMP_BULK_DISABLED : dc_interface->bulk);

	if (NULL == dc_interface || SNMP_BULK_ENABLED != dc_interface->bulk)
		return 1;

	/* The general strategy is to multiply request size by 3/2 in order to approach the limit faster. */
	/* However, once we are over the limit, we change the strategy to increasing the value by 1. This */
	/* is deemed better than going backwards from the error because less timeouts are going to occur. */

	if (1 >= dc_interface->max_snmp_succeed || MAX_SNMP_ITEMS + 1 != dc_interface->min_snmp_fail)
		num = dc_interface->max_snmp_succeed + 1;
	else
		num = dc_interface->max_snmp_succeed * 3 / 2;

	if (num < dc_interface->min_snmp_fail)
		return num;

	/* If we have already found the optimal number of variables to query, we wish to base our suggestion on that */
	/* number. If we occasionally get a timeout in this area, it can mean two things: either the device's actual */
	/* limit is a bit lower than that (it can process requests above it, but only sometimes) or a UDP packet in  */
	/* one of the directions was lost. In order to account for the former, we allow ourselves to lower the count */
	/* of variables, but only up to two times. Otherwise, performance will gradually degrade due to the latter.  */

	return MAX(dc_interface->max_snmp_succeed - 2, dc_interface->min_snmp_fail - 1);
}

int	DCconfig_get_suggested_snmp_vars(trx_uint64_t interfaceid, int *bulk)
{
	int	ret;

	RDLOCK_CACHE;

	ret = DCconfig_get_suggested_snmp_vars_nolock(interfaceid, bulk);

	UNLOCK_CACHE;

	return ret;
}

static int	dc_get_interface_by_type(DC_INTERFACE *interface, trx_uint64_t hostid, unsigned char type)
{
	int				res = FAIL;
	const TRX_DC_INTERFACE		*dc_interface;
	const TRX_DC_INTERFACE_HT	*interface_ht;
	TRX_DC_INTERFACE_HT		interface_ht_local;

	interface_ht_local.hostid = hostid;
	interface_ht_local.type = type;

	if (NULL != (interface_ht = (const TRX_DC_INTERFACE_HT *)trx_hashset_search(&config->interfaces_ht, &interface_ht_local)))
	{
		dc_interface = interface_ht->interface_ptr;
		DCget_interface(interface, dc_interface);
		res = SUCCEED;
	}

	return res;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_interface_by_type                                   *
 *                                                                            *
 * Purpose: Locate main interface of specified type in configuration cache    *
 *                                                                            *
 * Parameters: interface - [OUT] pointer to DC_INTERFACE structure            *
 *             hostid - [IN] host ID                                          *
 *             type - [IN] interface type                                     *
 *                                                                            *
 * Return value: SUCCEED if record located and FAIL otherwise                 *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_interface_by_type(DC_INTERFACE *interface, trx_uint64_t hostid, unsigned char type)
{
	int	res;

	RDLOCK_CACHE;

	res = dc_get_interface_by_type(interface, hostid, type);

	UNLOCK_CACHE;

	return res;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_interface                                           *
 *                                                                            *
 * Purpose: Locate interface in configuration cache                           *
 *                                                                            *
 * Parameters: interface - [OUT] pointer to DC_INTERFACE structure            *
 *             hostid - [IN] host ID                                          *
 *             itemid - [IN] item ID                                          *
 *                                                                            *
 * Return value: SUCCEED if record located and FAIL otherwise                 *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_interface(DC_INTERFACE *interface, trx_uint64_t hostid, trx_uint64_t itemid)
{
	int			res = FAIL, i;
	const TRX_DC_ITEM	*dc_item;
	const TRX_DC_INTERFACE	*dc_interface;

	RDLOCK_CACHE;

	if (0 != itemid)
	{
		if (NULL == (dc_item = (const TRX_DC_ITEM *)trx_hashset_search(&config->items, &itemid)))
			goto unlock;

		if (0 != dc_item->interfaceid)
		{
			if (NULL == (dc_interface = (const TRX_DC_INTERFACE *)trx_hashset_search(&config->interfaces,
					&dc_item->interfaceid)))
			{
				goto unlock;
			}

			DCget_interface(interface, dc_interface);
			res = SUCCEED;
			goto unlock;
		}

		hostid = dc_item->hostid;
	}

	if (0 == hostid)
		goto unlock;

	for (i = 0; i < (int)ARRSIZE(INTERFACE_TYPE_PRIORITY); i++)
	{
		if (SUCCEED == (res = dc_get_interface_by_type(interface, hostid, INTERFACE_TYPE_PRIORITY[i])))
			break;
	}

unlock:
	UNLOCK_CACHE;

	return res;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_config_get_queue_nextcheck                                    *
 *                                                                            *
 * Purpose: Get nextcheck for selected queue                                  *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *                                                                            *
 * Return value: nextcheck or FAIL if no items for the specified queue        *
 *                                                                            *
 ******************************************************************************/
static int	dc_config_get_queue_nextcheck(trx_binary_heap_t *queue)
{
	int				nextcheck;
	const trx_binary_heap_elem_t	*min;
	const TRX_DC_ITEM		*dc_item;

	if (FAIL == trx_binary_heap_empty(queue))
	{
		min = trx_binary_heap_find_min(queue);
		dc_item = (const TRX_DC_ITEM *)min->data;

		nextcheck = dc_item->nextcheck;
	}
	else
		nextcheck = FAIL;

	return nextcheck;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_poller_nextcheck                                    *
 *                                                                            *
 * Purpose: Get nextcheck for selected poller                                 *
 *                                                                            *
 * Parameters: poller_type - [IN] poller type (TRX_POLLER_TYPE_...)           *
 *                                                                            *
 * Return value: nextcheck or FAIL if no items for selected poller            *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_poller_nextcheck(unsigned char poller_type)
{
	int			nextcheck;
	trx_binary_heap_t	*queue;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() poller_type:%d", __func__, (int)poller_type);

	queue = &config->queues[poller_type];

	RDLOCK_CACHE;

	nextcheck = dc_config_get_queue_nextcheck(queue);

	UNLOCK_CACHE;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, nextcheck);

	return nextcheck;
}

static void	dc_requeue_item(TRX_DC_ITEM *dc_item, const TRX_DC_HOST *dc_host, unsigned char new_state, int flags,
		int lastclock)
{
	unsigned char	old_poller_type;
	int		old_nextcheck;

	old_nextcheck = dc_item->nextcheck;
	DCitem_nextcheck_update(dc_item, dc_host, new_state, flags, lastclock, NULL);

	old_poller_type = dc_item->poller_type;
	DCitem_poller_type_update(dc_item, dc_host, flags);

	DCupdate_item_queue(dc_item, old_poller_type, old_nextcheck);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_requeue_item_at                                               *
 *                                                                            *
 * Purpose: requeues items at the specified time                              *
 *                                                                            *
 * Parameters: dc_item   - [IN] the item to reque                             *
 *             dc_host   - [IN] item's host                                   *
 *             nextcheck - [IN] the scheduled time                            *
 *                                                                            *
 ******************************************************************************/
static void	dc_requeue_item_at(TRX_DC_ITEM *dc_item, TRX_DC_HOST *dc_host, int nextcheck)
{
	unsigned char	old_poller_type;
	int		old_nextcheck;

	dc_item->queue_priority = TRX_QUEUE_PRIORITY_HIGH;

	old_nextcheck = dc_item->nextcheck;
	dc_item->nextcheck = nextcheck;

	old_poller_type = dc_item->poller_type;
	DCitem_poller_type_update(dc_item, dc_host, TRX_ITEM_COLLECTED);

	DCupdate_item_queue(dc_item, old_poller_type, old_nextcheck);
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_poller_items                                        *
 *                                                                            *
 * Purpose: Get array of items for selected poller                            *
 *                                                                            *
 * Parameters: poller_type - [IN] poller type (TRX_POLLER_TYPE_...)           *
 *             items       - [OUT] array of items                             *
 *                                                                            *
 * Return value: number of items in items array                               *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 * Comments: Items leave the queue only through this function. Pollers must   *
 *           always return the items they have taken using DCrequeue_items()  *
 *           or DCpoller_requeue_items().                                     *
 *                                                                            *
 *           Currently batch polling is supported only for JMX, SNMP and      *
 *           icmpping* simple checks. In other cases only single item is      *
 *           retrieved.                                                       *
 *                                                                            *
 *           IPMI poller queue are handled by DCconfig_get_ipmi_poller_items()*
 *           function.                                                        *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_poller_items(unsigned char poller_type, DC_ITEM *items)
{
	int			now, num = 0, max_items;
	trx_binary_heap_t	*queue;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() poller_type:%d", __func__, (int)poller_type);

	now = time(NULL);

	queue = &config->queues[poller_type];

	switch (poller_type)
	{
		case TRX_POLLER_TYPE_JAVA:
			max_items = MAX_JAVA_ITEMS;
			break;
		case TRX_POLLER_TYPE_PINGER:
			max_items = MAX_PINGER_ITEMS;
			break;
		default:
			max_items = 1;
	}

	WRLOCK_CACHE;

	while (num < max_items && FAIL == trx_binary_heap_empty(queue))
	{
		int				disable_until;
		const trx_binary_heap_elem_t	*min;
		TRX_DC_HOST			*dc_host;
		TRX_DC_ITEM			*dc_item;
		static const TRX_DC_ITEM	*dc_item_prev = NULL;

		min = trx_binary_heap_find_min(queue);
		dc_item = (TRX_DC_ITEM *)min->data;

		if (dc_item->nextcheck > now)
			break;

		if (0 != num)
		{
			if (SUCCEED == is_snmp_type(dc_item_prev->type))
			{
				if (0 != __config_snmp_item_compare(dc_item_prev, dc_item))
					break;
			}
			else if (ITEM_TYPE_JMX == dc_item_prev->type)
			{
				if (0 != __config_java_item_compare(dc_item_prev, dc_item))
					break;
			}
		}

		trx_binary_heap_remove_min(queue);
		dc_item->location = TRX_LOC_NOWHERE;

		if (NULL == (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		if (SUCCEED == DCin_maintenance_without_data_collection(dc_host, dc_item))
		{
			dc_requeue_item(dc_item, dc_host, dc_item->state, TRX_ITEM_COLLECTED, now);
			continue;
		}

		/* don't apply unreachable item/host throttling for prioritized items */
		if (TRX_QUEUE_PRIORITY_HIGH != dc_item->queue_priority)
		{
			if (0 == (disable_until = DCget_disable_until(dc_item, dc_host)))
			{
				/* move reachable items on reachable hosts to normal pollers */
				if (TRX_POLLER_TYPE_UNREACHABLE == poller_type &&
						TRX_QUEUE_PRIORITY_LOW != dc_item->queue_priority)
				{
					dc_requeue_item(dc_item, dc_host, dc_item->state, TRX_ITEM_COLLECTED, now);
					continue;
				}
			}
			else
			{
				/* move items on unreachable hosts to unreachable pollers or    */
				/* postpone checks on hosts that have been checked recently and */
				/* are still unreachable                                        */
				if (TRX_POLLER_TYPE_NORMAL == poller_type || TRX_POLLER_TYPE_JAVA == poller_type ||
						disable_until > now)
				{
					dc_requeue_item(dc_item, dc_host, dc_item->state,
							TRX_ITEM_COLLECTED | TRX_HOST_UNREACHABLE, now);
					continue;
				}

				DCincrease_disable_until(dc_item, dc_host, now);
			}
		}

		dc_item_prev = dc_item;
		dc_item->location = TRX_LOC_POLLER;
		DCget_host(&items[num].host, dc_host);
		DCget_item(&items[num], dc_item);
		num++;

		if (1 == num && TRX_POLLER_TYPE_NORMAL == poller_type && SUCCEED == is_snmp_type(dc_item->type) &&
				0 == (TRX_FLAG_DISCOVERY_RULE & dc_item->flags))
		{
			TRX_DC_SNMPITEM	*snmpitem;

			snmpitem = (TRX_DC_SNMPITEM *)trx_hashset_search(&config->snmpitems, &dc_item->itemid);

			if (TRX_SNMP_OID_TYPE_NORMAL == snmpitem->snmp_oid_type ||
					TRX_SNMP_OID_TYPE_DYNAMIC == snmpitem->snmp_oid_type)
			{
				max_items = DCconfig_get_suggested_snmp_vars_nolock(dc_item->interfaceid, NULL);
			}
		}
	}

	UNLOCK_CACHE;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, num);

	return num;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_ipmi_poller_items                                   *
 *                                                                            *
 * Purpose: Get array of items for IPMI poller                                *
 *                                                                            *
 * Parameters: now       - [IN] current timestamp                             *
 *             items     - [OUT] array of items                               *
 *             items_num - [IN] the number of items to get                    *
 *             nextcheck - [OUT] the next scheduled check                     *
 *                                                                            *
 * Return value: number of items in items array                               *
 *                                                                            *
 * Comments: IPMI items leave the queue only through this function. IPMI      *
 *           manager must always return the items they have taken using       *
 *           DCrequeue_items() or DCpoller_requeue_items().                   *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_ipmi_poller_items(int now, DC_ITEM *items, int items_num, int *nextcheck)
{
	int			num = 0;
	trx_binary_heap_t	*queue;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	queue = &config->queues[TRX_POLLER_TYPE_IPMI];

	WRLOCK_CACHE;

	while (num < items_num && FAIL == trx_binary_heap_empty(queue))
	{
		int				disable_until;
		const trx_binary_heap_elem_t	*min;
		TRX_DC_HOST			*dc_host;
		TRX_DC_ITEM			*dc_item;

		min = trx_binary_heap_find_min(queue);
		dc_item = (TRX_DC_ITEM *)min->data;

		if (dc_item->nextcheck > now)
			break;

		trx_binary_heap_remove_min(queue);
		dc_item->location = TRX_LOC_NOWHERE;

		if (NULL == (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		if (SUCCEED == DCin_maintenance_without_data_collection(dc_host, dc_item))
		{
			dc_requeue_item(dc_item, dc_host, dc_item->state, TRX_ITEM_COLLECTED, now);
			continue;
		}

		/* don't apply unreachable item/host throttling for prioritized items */
		if (TRX_QUEUE_PRIORITY_HIGH != dc_item->queue_priority)
		{
			if (0 != (disable_until = DCget_disable_until(dc_item, dc_host)))
			{
				if (disable_until > now)
				{
					dc_requeue_item(dc_item, dc_host, dc_item->state,
							TRX_ITEM_COLLECTED | TRX_HOST_UNREACHABLE, now);
					continue;
				}

				DCincrease_disable_until(dc_item, dc_host, now);
			}
		}

		dc_item->location = TRX_LOC_POLLER;
		DCget_host(&items[num].host, dc_host);
		DCget_item(&items[num], dc_item);
		num++;
	}

	*nextcheck = dc_config_get_queue_nextcheck(&config->queues[TRX_POLLER_TYPE_IPMI]);

	UNLOCK_CACHE;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, num);

	return num;
}


/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_snmp_interfaceids_by_addr                           *
 *                                                                            *
 * Purpose: get array of interface IDs for the specified address              *
 *                                                                            *
 * Return value: number of interface IDs returned                             *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_snmp_interfaceids_by_addr(const char *addr, trx_uint64_t **interfaceids)
{
	int				count = 0, i;
	const TRX_DC_INTERFACE_ADDR	*dc_interface_snmpaddr;
	TRX_DC_INTERFACE_ADDR		dc_interface_snmpaddr_local;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() addr:'%s'", __func__, addr);

	dc_interface_snmpaddr_local.addr = addr;

	RDLOCK_CACHE;

	if (NULL == (dc_interface_snmpaddr = (const TRX_DC_INTERFACE_ADDR *)trx_hashset_search(&config->interface_snmpaddrs, &dc_interface_snmpaddr_local)))
		goto unlock;

	*interfaceids = (trx_uint64_t *)trx_malloc(*interfaceids, dc_interface_snmpaddr->interfaceids.values_num * sizeof(trx_uint64_t));

	for (i = 0; i < dc_interface_snmpaddr->interfaceids.values_num; i++)
		(*interfaceids)[i] = dc_interface_snmpaddr->interfaceids.values[i];

	count = i;
unlock:
	UNLOCK_CACHE;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, count);

	return count;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_snmp_items_by_interfaceid                           *
 *                                                                            *
 * Purpose: get array of snmp trap items for the specified interfaceid        *
 *                                                                            *
 * Return value: number of items returned                                     *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
size_t	DCconfig_get_snmp_items_by_interfaceid(trx_uint64_t interfaceid, DC_ITEM **items)
{
	size_t				items_num = 0, items_alloc = 8;
	int				i;
	const TRX_DC_ITEM		*dc_item;
	const TRX_DC_INTERFACE_ITEM	*dc_interface_snmpitem;
	const TRX_DC_INTERFACE		*dc_interface;
	const TRX_DC_HOST		*dc_host;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() interfaceid:" TRX_FS_UI64, __func__, interfaceid);

	RDLOCK_CACHE;

	if (NULL == (dc_interface = (const TRX_DC_INTERFACE *)trx_hashset_search(&config->interfaces, &interfaceid)))
		goto unlock;

	if (NULL == (dc_host = (const TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_interface->hostid)))
		goto unlock;

	if (HOST_STATUS_MONITORED != dc_host->status)
		goto unlock;

	if (NULL == (dc_interface_snmpitem = (const TRX_DC_INTERFACE_ITEM *)trx_hashset_search(&config->interface_snmpitems, &interfaceid)))
		goto unlock;

	*items = (DC_ITEM *)trx_malloc(*items, items_alloc * sizeof(DC_ITEM));

	for (i = 0; i < dc_interface_snmpitem->itemids.values_num; i++)
	{
		if (NULL == (dc_item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &dc_interface_snmpitem->itemids.values[i])))
			continue;

		if (ITEM_STATUS_ACTIVE != dc_item->status)
			continue;

		if (SUCCEED == DCin_maintenance_without_data_collection(dc_host, dc_item))
			continue;

		if (0 == config->config->refresh_unsupported && ITEM_STATE_NOTSUPPORTED == dc_item->state)
			continue;

		if (items_num == items_alloc)
		{
			items_alloc += 8;
			*items = (DC_ITEM *)trx_realloc(*items, items_alloc * sizeof(DC_ITEM));
		}

		DCget_host(&(*items)[items_num].host, dc_host);
		DCget_item(&(*items)[items_num], dc_item);
		items_num++;
	}
unlock:
	UNLOCK_CACHE;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():" TRX_FS_SIZE_T, __func__, (trx_fs_size_t)items_num);

	return items_num;
}

static void	dc_requeue_items(const trx_uint64_t *itemids, const unsigned char *states, const int *lastclocks,
		const int *errcodes, size_t num)
{
	size_t		i;
	TRX_DC_ITEM	*dc_item;
	TRX_DC_HOST	*dc_host;

	for (i = 0; i < num; i++)
	{
		if (FAIL == errcodes[i])
			continue;

		if (NULL == (dc_item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &itemids[i])))
			continue;

		if (TRX_LOC_POLLER == dc_item->location)
			dc_item->location = TRX_LOC_NOWHERE;

		if (ITEM_STATUS_ACTIVE != dc_item->status)
			continue;

		if (NULL == (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		if (SUCCEED != trx_is_counted_in_item_queue(dc_item->type, dc_item->key))
			continue;

		switch (errcodes[i])
		{
			case SUCCEED:
			case NOTSUPPORTED:
			case AGENT_ERROR:
			case CONFIG_ERROR:
				dc_item->queue_priority = TRX_QUEUE_PRIORITY_NORMAL;
				dc_requeue_item(dc_item, dc_host, states[i], TRX_ITEM_COLLECTED, lastclocks[i]);
				break;
			case NETWORK_ERROR:
			case GATEWAY_ERROR:
			case TIMEOUT_ERROR:
				dc_item->queue_priority = TRX_QUEUE_PRIORITY_LOW;
				dc_requeue_item(dc_item, dc_host, states[i], TRX_ITEM_COLLECTED | TRX_HOST_UNREACHABLE,
						time(NULL));
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}
}

void	DCrequeue_items(const trx_uint64_t *itemids, const unsigned char *states, const int *lastclocks,
		const int *errcodes, size_t num)
{
	WRLOCK_CACHE;

	dc_requeue_items(itemids, states, lastclocks, errcodes, num);

	UNLOCK_CACHE;
}

void	DCpoller_requeue_items(const trx_uint64_t *itemids, const unsigned char *states, const int *lastclocks,
		const int *errcodes, size_t num, unsigned char poller_type, int *nextcheck)
{
	WRLOCK_CACHE;

	dc_requeue_items(itemids, states, lastclocks, errcodes, num);
	*nextcheck = dc_config_get_queue_nextcheck(&config->queues[poller_type]);

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_requeue_unreachable_items                                 *
 *                                                                            *
 * Purpose: requeue unreachable items                                         *
 *                                                                            *
 * Parameters: itemids     - [IN] the item id array                           *
 *             itemids_num - [IN] the number of values in itemids array       *
 *                                                                            *
 * Comments: This function is used when items must be put back in the queue   *
 *           without polling them. For example if a poller has taken a batch  *
 *           of items from queue, host becomes unreachable during while       *
 *           polling the items, so the unpolled items of the same host must   *
 *           be returned to queue without updating their status.              *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_requeue_unreachable_items(trx_uint64_t *itemids, size_t itemids_num)
{
	size_t		i;
	TRX_DC_ITEM	*dc_item;
	TRX_DC_HOST	*dc_host;

	WRLOCK_CACHE;

	for (i = 0; i < itemids_num; i++)
	{
		if (NULL == (dc_item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &itemids[i])))
			continue;

		if (TRX_LOC_POLLER == dc_item->location)
			dc_item->location = TRX_LOC_NOWHERE;

		if (ITEM_STATUS_ACTIVE != dc_item->status)
			continue;

		if (NULL == (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		dc_requeue_item(dc_item, dc_host, dc_item->state, TRX_ITEM_COLLECTED | TRX_HOST_UNREACHABLE,
				time(NULL));
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DChost_get_agent_availability                                    *
 *                                                                            *
 * Purpose: get host availability data for the specified agent                *
 *                                                                            *
 * Parameters: dc_host      - [IN] the host                                   *
 *             agent        - [IN] the agent (see TRX_FLAGS_AGENT_STATUS_*    *
 *                                 defines                                    *
 *             availability - [OUT] the host availability data                *
 *                                                                            *
 * Comments: The configuration cache must be locked already.                  *
 *                                                                            *
 ******************************************************************************/
static void	DChost_get_agent_availability(const TRX_DC_HOST *dc_host, unsigned char agent_type,
		trx_agent_availability_t *agent)
{

	agent->flags = TRX_FLAGS_AGENT_STATUS;

	switch (agent_type)
	{
		case TRX_AGENT_TREEGIX:
			agent->available = dc_host->available;
			agent->error = trx_strdup(agent->error, dc_host->error);
			agent->errors_from = dc_host->errors_from;
			agent->disable_until = dc_host->disable_until;
			break;
		case TRX_AGENT_SNMP:
			agent->available = dc_host->snmp_available;
			agent->error = trx_strdup(agent->error, dc_host->snmp_error);
			agent->errors_from = dc_host->snmp_errors_from;
			agent->disable_until = dc_host->snmp_disable_until;
			break;
		case TRX_AGENT_IPMI:
			agent->available = dc_host->ipmi_available;
			agent->error = trx_strdup(agent->error, dc_host->ipmi_error);
			agent->errors_from = dc_host->ipmi_errors_from;
			agent->disable_until = dc_host->ipmi_disable_until;
			break;
		case TRX_AGENT_JMX:
			agent->available = dc_host->jmx_available;
			agent->error = trx_strdup(agent->error, dc_host->jmx_error);
			agent->errors_from = dc_host->jmx_errors_from;
			agent->disable_until = dc_host->jmx_disable_until;
			break;
	}
}

static void	DCagent_set_availability(trx_agent_availability_t *av,  unsigned char *available, const char **error,
		int *errors_from, int *disable_until)
{
#define AGENT_AVAILABILITY_ASSIGN(flags, mask, dst, src)	\
	if (0 != (flags & mask))				\
	{							\
		if (dst != src)					\
			dst = src;				\
		else						\
			flags &= (~(mask));			\
	}

#define AGENT_AVAILABILITY_ASSIGN_STR(flags, mask, dst, src)	\
	if (0 != (flags & mask))				\
	{							\
		if (0 != strcmp(dst, src))			\
			DCstrpool_replace(1, &dst, src);	\
		else						\
			flags &= (~(mask));			\
	}

	AGENT_AVAILABILITY_ASSIGN(av->flags, TRX_FLAGS_AGENT_STATUS_AVAILABLE, *available, av->available);
	AGENT_AVAILABILITY_ASSIGN_STR(av->flags, TRX_FLAGS_AGENT_STATUS_ERROR, *error, av->error);
	AGENT_AVAILABILITY_ASSIGN(av->flags, TRX_FLAGS_AGENT_STATUS_ERRORS_FROM, *errors_from, av->errors_from);
	AGENT_AVAILABILITY_ASSIGN(av->flags, TRX_FLAGS_AGENT_STATUS_DISABLE_UNTIL, *disable_until, av->disable_until);

#undef AGENT_AVAILABILITY_ASSIGN_STR
#undef AGENT_AVAILABILITY_ASSIGN
}

/******************************************************************************
 *                                                                            *
 * Function: DChost_set_agent_availability                                    *
 *                                                                            *
 * Purpose: set host availability data in configuration cache                 *
 *                                                                            *
 * Parameters: dc_host      - [OUT] the host                                  *
 *             availability - [IN/OUT] the host availability data             *
 *                                                                            *
 * Return value: SUCCEED - at least one availability field was updated        *
 *               FAIL    - no availability fields were updated                *
 *                                                                            *
 * Comments: The configuration cache must be locked already.                  *
 *                                                                            *
 *           This function clears availability flags of non updated fields    *
 *           updated leaving only flags identifying changed fields.           *
 *                                                                            *
 ******************************************************************************/
static int	DChost_set_agent_availability(TRX_DC_HOST *dc_host, int now, unsigned char agent_type,
		trx_agent_availability_t *agent)
{
	switch (agent_type)
	{
		case TRX_AGENT_TREEGIX:
			DCagent_set_availability(agent, &dc_host->available,
					&dc_host->error, &dc_host->errors_from, &dc_host->disable_until);
			break;
		case TRX_AGENT_SNMP:
			DCagent_set_availability(agent, &dc_host->snmp_available,
					&dc_host->snmp_error, &dc_host->snmp_errors_from, &dc_host->snmp_disable_until);
			break;
		case TRX_AGENT_IPMI:
			DCagent_set_availability(agent, &dc_host->ipmi_available,
					&dc_host->ipmi_error, &dc_host->ipmi_errors_from, &dc_host->ipmi_disable_until);
			break;
		case TRX_AGENT_JMX:
			DCagent_set_availability(agent, &dc_host->jmx_available,
					&dc_host->jmx_error, &dc_host->jmx_errors_from, &dc_host->jmx_disable_until);
			break;
	}

	if (TRX_FLAGS_AGENT_STATUS_NONE == agent->flags)
		return FAIL;

	if (0 != (agent->flags & (TRX_FLAGS_AGENT_STATUS_AVAILABLE | TRX_FLAGS_AGENT_STATUS_ERROR)))
		dc_host->availability_ts = now;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: DChost_set_availability                                          *
 *                                                                            *
 * Purpose: set host availability data in configuration cache                 *
 *                                                                            *
 * Parameters: dc_host      - [OUT] the host                                  *
 *             availability - [IN/OUT] the host availability data             *
 *                                                                            *
 * Return value: SUCCEED - at least one availability field was updated        *
 *               FAIL    - no availability fields were updated                *
 *                                                                            *
 * Comments: The configuration cache must be locked already.                  *
 *                                                                            *
 *           This function clears availability flags of non updated fields    *
 *           updated leaving only flags identifying changed fields.           *
 *                                                                            *
 ******************************************************************************/
static int	DChost_set_availability(TRX_DC_HOST *dc_host, int now, trx_host_availability_t *ha)
{
	int		i;
	unsigned char	flags = TRX_FLAGS_AGENT_STATUS_NONE;

	DCagent_set_availability(&ha->agents[TRX_AGENT_TREEGIX], &dc_host->available, &dc_host->error,
			&dc_host->errors_from, &dc_host->disable_until);
	DCagent_set_availability(&ha->agents[TRX_AGENT_SNMP], &dc_host->snmp_available, &dc_host->snmp_error,
			&dc_host->snmp_errors_from, &dc_host->snmp_disable_until);
	DCagent_set_availability(&ha->agents[TRX_AGENT_IPMI], &dc_host->ipmi_available, &dc_host->ipmi_error,
			&dc_host->ipmi_errors_from, &dc_host->ipmi_disable_until);
	DCagent_set_availability(&ha->agents[TRX_AGENT_JMX], &dc_host->jmx_available, &dc_host->jmx_error,
			&dc_host->jmx_errors_from, &dc_host->jmx_disable_until);

	for (i = 0; i < TRX_AGENT_MAX; i++)
		flags |= ha->agents[i].flags;

	if (TRX_FLAGS_AGENT_STATUS_NONE == flags)
		return FAIL;

	if (0 != (flags & (TRX_FLAGS_AGENT_STATUS_AVAILABLE | TRX_FLAGS_AGENT_STATUS_ERROR)))
		dc_host->availability_ts = now;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_host_availability_init                                       *
 *                                                                            *
 * Purpose: initializes host availability data                                *
 *                                                                            *
 * Parameters: availability - [IN/OUT] host availability data                 *
 *                                                                            *
 ******************************************************************************/
void	trx_host_availability_init(trx_host_availability_t *availability, trx_uint64_t hostid)
{
	memset(availability, 0, sizeof(trx_host_availability_t));
	availability->hostid = hostid;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_host_availability_clean                                      *
 *                                                                            *
 * Purpose: releases resources allocated to store host availability data      *
 *                                                                            *
 * Parameters: availability - [IN] host availability data                     *
 *                                                                            *
 ******************************************************************************/
void	trx_host_availability_clean(trx_host_availability_t *ha)
{
	int	i;

	for (i = 0; i < TRX_AGENT_MAX; i++)
		trx_free(ha->agents[i].error);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_host_availability_free                                       *
 *                                                                            *
 * Purpose: frees host availability data                                      *
 *                                                                            *
 * Parameters: availability - [IN] host availability data                     *
 *                                                                            *
 ******************************************************************************/
void	trx_host_availability_free(trx_host_availability_t *availability)
{
	trx_host_availability_clean(availability);
	trx_free(availability);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_agent_availability_init                                      *
 *                                                                            *
 * Purpose: initializes agent availability with the specified data            *
 *                                                                            *
 * Parameters: availability  - [IN/OUT] agent availability data               *
 *             hostid        - [IN] the host identifier                       *
 *             flags         - [IN] the availability flags indicating which   *
 *                                  availability fields to set                *
 *             available     - [IN] the availability data                     *
 *             error         - [IN]                                           *
 *             errors_from   - [IN]                                           *
 *             disable_until - [IN]                                           *
 *                                                                            *
 ******************************************************************************/
static void	trx_agent_availability_init(trx_agent_availability_t *agent, unsigned char available, const char *error,
		int errors_from, int disable_until)
{
	agent->flags = TRX_FLAGS_AGENT_STATUS;
	agent->available = available;
	agent->error = trx_strdup(agent->error, error);
	agent->errors_from = errors_from;
	agent->disable_until = disable_until;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_host_availability_is_set                                     *
 *                                                                            *
 * Purpose: checks host availability if any agent availability field is set   *
 *                                                                            *
 * Parameters: availability - [IN] host availability data                     *
 *                                                                            *
 * Return value: SUCCEED - an agent availability field is set                 *
 *               FAIL - no agent availability fields are set                  *
 *                                                                            *
 ******************************************************************************/
int	trx_host_availability_is_set(const trx_host_availability_t *ha)
{
	int	i;

	for (i = 0; i < TRX_AGENT_MAX; i++)
	{
		if (TRX_FLAGS_AGENT_STATUS_NONE != ha->agents[i].flags)
			return SUCCEED;
	}

	return FAIL;
}

/**************************************************************************************
 *                                                                                    *
 * Host availability update example                                                   *
 *                                                                                    *
 *                                                                                    *
 *               |            UnreachablePeriod                                       *
 *               |               (conf file)                                          *
 *               |              ______________                                        *
 *               |             /              \                                       *
 *               |             p     p     p     p       p       p                    *
 *               |             o     o     o     o       o       o                    *
 *               |             l     l     l     l       l       l                    *
 *               |             l     l     l     l       l       l                    *
 *               | n                                                                  *
 *               | e           e     e     e     e       e       e                    *
 *     agent     | w   p   p   r     r     r     r       r       r       p   p   p    *
 *       polls   |     o   o   r     r     r     r       r       r       o   o   o    *
 *               | h   l   l   o     o     o     o       o       o       l   l   l    *
 *               | o   l   l   r     r     r     r       r       r       l   l   l    *
 *               | s                                                                  *
 *               | t   ok  ok  E1    E1    E2    E1      E1      E2      ok  ok  ok   *
 *  --------------------------------------------------------------------------------  *
 *  available    | 0   1   1   1     1     1     2       2       2       0   0   0    *
 *               |                                                                    *
 *  error        | ""  ""  ""  ""    ""    ""    E1      E1      E2      ""  ""  ""   *
 *               |                                                                    *
 *  errors_from  | 0   0   0   T4    T4    T4    T4      T4      T4      0   0   0    *
 *               |                                                                    *
 *  disable_until| 0   0   0   T5    T6    T7    T8      T9      T10     0   0   0    *
 *  --------------------------------------------------------------------------------  *
 *   timestamps  | T1  T2  T3  T4    T5    T6    T7      T8      T9     T10 T11 T12   *
 *               |  \_/ \_/ \_/ \___/ \___/ \___/ \_____/ \_____/ \_____/ \_/ \_/     *
 *               |   |   |   |    |     |     |      |       |       |     |   |      *
 *  polling      |  item delay   UnreachableDelay    UnavailableDelay     item |      *
 *      periods  |                 (conf file)         (conf file)         delay      *
 *                                                                                    *
 *                                                                                    *
 **************************************************************************************/

/******************************************************************************
 *                                                                            *
 * Function: DChost_activate                                                  *
 *                                                                            *
 * Purpose: set host as available based on the agent availability data        *
 *                                                                            *
 * Parameters: hostid     - [IN] the host identifier                          *
 *             agent_type - [IN] the agent type (see TRX_AGENT_* defines)     *
 *             ts         - [IN] the last timestamp                           *
 *             in         - [IN/OUT] IN: the caller's agent availability data *
 *                                  OUT: the agent availability data in cache *
 *                                       before changes                       *
 *             out        - [OUT] the agent availability data after changes   *
 *                                                                            *
 * Return value: SUCCEED - the host was activated successfully                *
 *               FAIL    - the host was already activated or activation       *
 *                         failed                                             *
 *                                                                            *
 * Comments: The host availability fields are updated according to the above  *
 *           schema.                                                          *
 *                                                                            *
 ******************************************************************************/
int	DChost_activate(trx_uint64_t hostid, unsigned char agent_type, const trx_timespec_t *ts,
		trx_agent_availability_t *in, trx_agent_availability_t *out)
{
	int		ret = FAIL;
	TRX_DC_HOST	*dc_host;

	/* don't try activating host if there were no errors detected */
	if (0 == in->errors_from && HOST_AVAILABLE_TRUE == in->available)
		goto out;

	WRLOCK_CACHE;

	if (NULL == (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &hostid)))
		goto unlock;

	/* Don't try activating host if:                  */
	/* - (server, proxy) it's not monitored any more; */
	/* - (server) it's monitored by proxy.            */
	if ((0 != (program_type & TRX_PROGRAM_TYPE_SERVER) && 0 != dc_host->proxy_hostid) ||
			HOST_STATUS_MONITORED != dc_host->status)
	{
		goto unlock;
	}

	DChost_get_agent_availability(dc_host, agent_type, in);
	trx_agent_availability_init(out, HOST_AVAILABLE_TRUE, "", 0, 0);
	DChost_set_agent_availability(dc_host, ts->sec, agent_type, out);

	if (TRX_FLAGS_AGENT_STATUS_NONE != out->flags)
		ret = SUCCEED;
unlock:
	UNLOCK_CACHE;
out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DChost_deactivate                                                *
 *                                                                            *
 * Purpose: attempt to set host as unavailable based on agent availability    *
 *                                                                            *
 * Parameters: hostid     - [IN] the host identifier                          *
 *             agent_type - [IN] the agent type (see TRX_AGENT_* defines)     *
 *             ts         - [IN] the last timestamp                           *
 *             in         - [IN/OUT] IN: the caller's host availability data  *
 *                                  OUT: the host availability data in cache  *
 *                                       before changes                       *
 *             out        - [OUT] the host availability data after changes    *
 *             error      - [IN] the error message                            *
 *                                                                            *
 * Return value: SUCCEED - the host was deactivated successfully              *
 *               FAIL    - the host was already deactivated or deactivation   *
 *                         failed                                             *
 *                                                                            *
 * Comments: The host availability fields are updated according to the above  *
 *           schema.                                                          *
 *                                                                            *
 ******************************************************************************/
int	DChost_deactivate(trx_uint64_t hostid, unsigned char agent_type, const trx_timespec_t *ts,
		trx_agent_availability_t *in, trx_agent_availability_t *out, const char *error_msg)
{
	int		ret = FAIL, errors_from,disable_until;
	const char	*error;
	unsigned char	available;
	TRX_DC_HOST	*dc_host;


	/* don't try deactivating host if the unreachable delay has not passed since the first error */
	if (CONFIG_UNREACHABLE_DELAY > ts->sec - in->errors_from)
		goto out;

	WRLOCK_CACHE;

	if (NULL == (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &hostid)))
		goto unlock;

	/* Don't try deactivating host if:                */
	/* - (server, proxy) it's not monitored any more; */
	/* - (server) it's monitored by proxy.            */
	if ((0 != (program_type & TRX_PROGRAM_TYPE_SERVER) && 0 != dc_host->proxy_hostid) ||
			HOST_STATUS_MONITORED != dc_host->status)
	{
		goto unlock;
	}

	DChost_get_agent_availability(dc_host, agent_type, in);

	available = in->available;
	error = in->error;

	if (0 == in->errors_from)
	{
		/* first error, schedule next unreachable check */
		errors_from = ts->sec;
		disable_until = ts->sec + CONFIG_UNREACHABLE_DELAY;
	}
	else
	{
		errors_from = in->errors_from;
		disable_until = in->disable_until;

		/* Check if other pollers haven't already attempted deactivating host. */
		/* In that case should wait the initial unreachable delay before       */
		/* trying to make it unavailable.                                      */
		if (CONFIG_UNREACHABLE_DELAY <= ts->sec - errors_from)
		{
			/* repeating error */
			if (CONFIG_UNREACHABLE_PERIOD > ts->sec - errors_from)
			{
				/* leave host available, schedule next unreachable check */
				disable_until = ts->sec + CONFIG_UNREACHABLE_DELAY;
			}
			else
			{
				/* make host unavailable, schedule next unavailable check */
				disable_until = ts->sec + CONFIG_UNAVAILABLE_DELAY;
				available = HOST_AVAILABLE_FALSE;
				error = error_msg;
			}
		}
	}

	trx_agent_availability_init(out, available, error, errors_from, disable_until);
	DChost_set_agent_availability(dc_host, ts->sec, agent_type, out);

	if (TRX_FLAGS_AGENT_STATUS_NONE != out->flags)
		ret = SUCCEED;
unlock:
	UNLOCK_CACHE;
out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DCset_hosts_availability                                         *
 *                                                                            *
 * Purpose: update availability of hosts in configuration cache and return    *
 *          the updated field flags                                           *
 *                                                                            *
 * Parameters: availabilities - [IN/OUT] the hosts availability data          *
 *                                                                            *
 * Return value: SUCCEED - at least one host availability data was updated    *
 *               FAIL    - no hosts were updated                              *
 *                                                                            *
 ******************************************************************************/
int	DCset_hosts_availability(trx_vector_ptr_t *availabilities)
{
	int			i;
	TRX_DC_HOST		*dc_host;
	trx_host_availability_t	*ha;
	int			ret = FAIL, now;

	now = time(NULL);

	WRLOCK_CACHE;

	for (i = 0; i < availabilities->values_num; i++)
	{
		ha = (trx_host_availability_t *)availabilities->values[i];

		if (NULL == (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &ha->hostid)))
		{
			int	j;

			/* reset availability flags so this host is ignored when saving availability diff to DB */
			for (j = 0; j < TRX_AGENT_MAX; j++)
				ha->agents[j].flags = TRX_FLAGS_AGENT_STATUS_NONE;

			continue;
		}

		if (SUCCEED == DChost_set_availability(dc_host, now, ha))
			ret = SUCCEED;
	}

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Comments: helper function for trigger dependency checking                  *
 *                                                                            *
 * Parameters: trigdep        - [IN] the trigger dependency data              *
 *             level          - [IN] the trigger dependency level             *
 *             triggerids     - [IN] the currently processing trigger ids     *
 *                                   for bulk trigger operations              *
 *                                   (optional, can be NULL)                  *
 *             master_triggerids - [OUT] unresolved master trigger ids        *
 *                                   for bulk trigger operations              *
 *                                   (optional together with triggerids       *
 *                                   parameter)                               *
 *                                                                            *
 * Return value: SUCCEED - trigger dependency check succeed / was unresolved  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: With bulk trigger processing a master trigger can be in the same *
 *           batch as dependent trigger. In this case it might be impossible  *
 *           to perform dependency check based on cashed trigger values. The  *
 *           unresolved master trigger ids will be added to master_triggerids *
 *           vector, so the dependency check can be performed after a new     *
 *           master trigger value has been calculated.                        *
 *                                                                            *
 ******************************************************************************/
static int	DCconfig_check_trigger_dependencies_rec(const TRX_DC_TRIGGER_DEPLIST *trigdep, int level,
		const trx_vector_uint64_t *triggerids, trx_vector_uint64_t *master_triggerids)
{
	int				i;
	const TRX_DC_TRIGGER		*next_trigger;
	const TRX_DC_TRIGGER_DEPLIST	*next_trigdep;

	if (TRX_TRIGGER_DEPENDENCY_LEVELS_MAX < level)
	{
		treegix_log(LOG_LEVEL_CRIT, "recursive trigger dependency is too deep (triggerid:" TRX_FS_UI64 ")",
				trigdep->triggerid);
		return SUCCEED;
	}

	if (0 != trigdep->dependencies.values_num)
	{
		for (i = 0; i < trigdep->dependencies.values_num; i++)
		{
			next_trigdep = (const TRX_DC_TRIGGER_DEPLIST *)trigdep->dependencies.values[i];

			if (NULL != (next_trigger = next_trigdep->trigger) &&
					TRIGGER_STATUS_ENABLED == next_trigger->status &&
					TRIGGER_FUNCTIONAL_TRUE == next_trigger->functional)
			{

				if (NULL == triggerids || FAIL == trx_vector_uint64_bsearch(triggerids,
						next_trigger->triggerid, TRX_DEFAULT_UINT64_COMPARE_FUNC))
				{
					if (TRIGGER_VALUE_PROBLEM == next_trigger->value)
						return FAIL;
				}
				else
					trx_vector_uint64_append(master_triggerids, next_trigger->triggerid);
			}

			if (FAIL == DCconfig_check_trigger_dependencies_rec(next_trigdep, level + 1, triggerids,
					master_triggerids))
			{
				return FAIL;
			}
		}
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_check_trigger_dependencies                              *
 *                                                                            *
 * Purpose: check whether any of trigger dependencies have value PROBLEM      *
 *                                                                            *
 * Return value: SUCCEED - trigger can change its value                       *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Author: Alexei Vladishev, Aleksandrs Saveljevs                             *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_check_trigger_dependencies(trx_uint64_t triggerid)
{
	int				ret = SUCCEED;
	const TRX_DC_TRIGGER_DEPLIST	*trigdep;

	RDLOCK_CACHE;

	if (NULL != (trigdep = (const TRX_DC_TRIGGER_DEPLIST *)trx_hashset_search(&config->trigdeps, &triggerid)))
		ret = DCconfig_check_trigger_dependencies_rec(trigdep, 0, NULL, NULL);

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Comments: helper function for DCconfig_sort_triggers_topologically()       *
 *                                                                            *
 ******************************************************************************/
static unsigned char	DCconfig_sort_triggers_topologically_rec(const TRX_DC_TRIGGER_DEPLIST *trigdep, int level)
{
	int				i;
	unsigned char			topoindex = 2, next_topoindex;
	const TRX_DC_TRIGGER_DEPLIST	*next_trigdep;

	if (32 < level)
	{
		treegix_log(LOG_LEVEL_CRIT, "recursive trigger dependency is too deep (triggerid:" TRX_FS_UI64 ")",
				trigdep->triggerid);
		goto exit;
	}

	if (0 == trigdep->trigger->topoindex)
	{
		treegix_log(LOG_LEVEL_CRIT, "trigger dependencies contain a cycle (triggerid:" TRX_FS_UI64 ")",
				trigdep->triggerid);
		goto exit;
	}

	trigdep->trigger->topoindex = 0;

	for (i = 0; i < trigdep->dependencies.values_num; i++)
	{
		next_trigdep = (const TRX_DC_TRIGGER_DEPLIST *)trigdep->dependencies.values[i];

		if (1 < (next_topoindex = next_trigdep->trigger->topoindex))
			goto next;

		if (0 == next_trigdep->dependencies.values_num)
			continue;

		next_topoindex = DCconfig_sort_triggers_topologically_rec(next_trigdep, level + 1);
next:
		if (topoindex < next_topoindex + 1)
			topoindex = next_topoindex + 1;
	}

	trigdep->trigger->topoindex = topoindex;
exit:
	return topoindex;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_sort_triggers_topologically                             *
 *                                                                            *
 * Purpose: assign each trigger an index based on trigger dependency topology *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 ******************************************************************************/
static void	DCconfig_sort_triggers_topologically(void)
{
	trx_hashset_iter_t		iter;
	TRX_DC_TRIGGER			*trigger;
	const TRX_DC_TRIGGER_DEPLIST	*trigdep;

	trx_hashset_iter_reset(&config->trigdeps, &iter);

	while (NULL != (trigdep = (TRX_DC_TRIGGER_DEPLIST *)trx_hashset_iter_next(&iter)))
	{
		trigger = trigdep->trigger;

		if (NULL == trigger || 1 < trigger->topoindex || 0 == trigdep->dependencies.values_num)
			continue;

		DCconfig_sort_triggers_topologically_rec(trigdep, 0);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_triggers_apply_changes                                  *
 *                                                                            *
 * Purpose: apply trigger value,state,lastchange or error changes to          *
 *          configuration cache after committed to database                   *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_triggers_apply_changes(trx_vector_ptr_t *trigger_diff)
{
	int			i;
	trx_trigger_diff_t	*diff;
	TRX_DC_TRIGGER		*dc_trigger;

	if (0 == trigger_diff->values_num)
		return;

	WRLOCK_CACHE;

	for (i = 0; i < trigger_diff->values_num; i++)
	{
		diff = (trx_trigger_diff_t *)trigger_diff->values[i];

		if (NULL == (dc_trigger = (TRX_DC_TRIGGER *)trx_hashset_search(&config->triggers, &diff->triggerid)))
			continue;

		if (0 != (diff->flags & TRX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE))
			dc_trigger->lastchange = diff->lastchange;

		if (0 != (diff->flags & TRX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE))
			dc_trigger->value = diff->value;

		if (0 != (diff->flags & TRX_FLAGS_TRIGGER_DIFF_UPDATE_STATE))
			dc_trigger->state = diff->state;

		if (0 != (diff->flags & TRX_FLAGS_TRIGGER_DIFF_UPDATE_ERROR))
			DCstrpool_replace(1, &dc_trigger->error, diff->error);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_stats                                               *
 *                                                                            *
 * Purpose: get statistics of the database cache                              *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
void	*DCconfig_get_stats(int request)
{
	static trx_uint64_t	value_uint;
	static double		value_double;

	switch (request)
	{
		case TRX_CONFSTATS_BUFFER_TOTAL:
			value_uint = config_mem->orig_size;
			return &value_uint;
		case TRX_CONFSTATS_BUFFER_USED:
			value_uint = config_mem->orig_size - config_mem->free_size;
			return &value_uint;
		case TRX_CONFSTATS_BUFFER_FREE:
			value_uint = config_mem->free_size;
			return &value_uint;
		case TRX_CONFSTATS_BUFFER_PUSED:
			value_double = 100 * (double)(config_mem->orig_size - config_mem->free_size) /
					config_mem->orig_size;
			return &value_double;
		case TRX_CONFSTATS_BUFFER_PFREE:
			value_double = 100 * (double)config_mem->free_size / config_mem->orig_size;
			return &value_double;
		default:
			return NULL;
	}
}

static void	DCget_proxy(DC_PROXY *dst_proxy, const TRX_DC_PROXY *src_proxy)
{
	const TRX_DC_HOST	*host;
	TRX_DC_INTERFACE_HT	*interface_ht, interface_ht_local;

	dst_proxy->hostid = src_proxy->hostid;
	dst_proxy->proxy_config_nextcheck = src_proxy->proxy_config_nextcheck;
	dst_proxy->proxy_data_nextcheck = src_proxy->proxy_data_nextcheck;
	dst_proxy->proxy_tasks_nextcheck = src_proxy->proxy_tasks_nextcheck;
	dst_proxy->last_cfg_error_time = src_proxy->last_cfg_error_time;
	dst_proxy->version = src_proxy->version;
	dst_proxy->lastaccess = src_proxy->lastaccess;
	dst_proxy->auto_compress = src_proxy->auto_compress;
	dst_proxy->last_version_error_time = src_proxy->last_version_error_time;

	if (NULL != (host = (const TRX_DC_HOST *)trx_hashset_search(&config->hosts, &src_proxy->hostid)))
	{
		strscpy(dst_proxy->host, host->host);
		strscpy(dst_proxy->proxy_address, src_proxy->proxy_address);

		dst_proxy->tls_connect = host->tls_connect;
		dst_proxy->tls_accept = host->tls_accept;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		strscpy(dst_proxy->tls_issuer, host->tls_issuer);
		strscpy(dst_proxy->tls_subject, host->tls_subject);

		if (NULL == host->tls_dc_psk)
		{
			*dst_proxy->tls_psk_identity = '\0';
			*dst_proxy->tls_psk = '\0';
		}
		else
		{
			strscpy(dst_proxy->tls_psk_identity, host->tls_dc_psk->tls_psk_identity);
			strscpy(dst_proxy->tls_psk, host->tls_dc_psk->tls_psk);
		}
#endif
	}
	else
	{
		/* DCget_proxy() is called only from DCconfig_get_proxypoller_hosts(), which is called only from */
		/* process_proxy(). So, this branch should never happen. */
		*dst_proxy->host = '\0';
		*dst_proxy->proxy_address = '\0';
		dst_proxy->tls_connect = TRX_TCP_SEC_TLS_PSK;	/* set PSK to deliberately fail in this case */
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		*dst_proxy->tls_psk_identity = '\0';
		*dst_proxy->tls_psk = '\0';
#endif
		THIS_SHOULD_NEVER_HAPPEN;
	}

	interface_ht_local.hostid = src_proxy->hostid;
	interface_ht_local.type = INTERFACE_TYPE_UNKNOWN;

	if (NULL != (interface_ht = (TRX_DC_INTERFACE_HT *)trx_hashset_search(&config->interfaces_ht, &interface_ht_local)))
	{
		const TRX_DC_INTERFACE	*interface = interface_ht->interface_ptr;

		strscpy(dst_proxy->addr_orig, interface->useip ? interface->ip : interface->dns);
		strscpy(dst_proxy->port_orig, interface->port);
	}
	else
	{
		*dst_proxy->addr_orig = '\0';
		*dst_proxy->port_orig = '\0';
	}

	dst_proxy->addr = NULL;
	dst_proxy->port = 0;
}

int	DCconfig_get_last_sync_time(void)
{
	return config->sync_ts;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_proxypoller_hosts                                   *
 *                                                                            *
 * Purpose: Get array of proxies for proxy poller                             *
 *                                                                            *
 * Parameters: hosts - [OUT] array of hosts                                   *
 *             max_hosts - [IN] elements in hosts array                       *
 *                                                                            *
 * Return value: number of proxies in hosts array                             *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: Proxies leave the queue only through this function. Pollers must *
 *           always return the proxies they have taken using DCrequeue_proxy. *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_proxypoller_hosts(DC_PROXY *proxies, int max_hosts)
{
	int			now, num = 0;
	trx_binary_heap_t	*queue;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	now = time(NULL);

	queue = &config->pqueue;

	WRLOCK_CACHE;

	while (num < max_hosts && FAIL == trx_binary_heap_empty(queue))
	{
		const trx_binary_heap_elem_t	*min;
		TRX_DC_PROXY			*dc_proxy;

		min = trx_binary_heap_find_min(queue);
		dc_proxy = (TRX_DC_PROXY *)min->data;

		if (dc_proxy->nextcheck > now)
			break;

		trx_binary_heap_remove_min(queue);
		dc_proxy->location = TRX_LOC_POLLER;

		DCget_proxy(&proxies[num], dc_proxy);
		num++;
	}

	UNLOCK_CACHE;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, num);

	return num;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_proxypoller_nextcheck                               *
 *                                                                            *
 * Purpose: Get nextcheck for passive proxies                                 *
 *                                                                            *
 * Return value: nextcheck or FAIL if no passive proxies in queue             *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_proxypoller_nextcheck(void)
{
	int			nextcheck;
	trx_binary_heap_t	*queue;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	queue = &config->pqueue;

	RDLOCK_CACHE;

	if (FAIL == trx_binary_heap_empty(queue))
	{
		const trx_binary_heap_elem_t	*min;
		const TRX_DC_PROXY		*dc_proxy;

		min = trx_binary_heap_find_min(queue);
		dc_proxy = (const TRX_DC_PROXY *)min->data;

		nextcheck = dc_proxy->nextcheck;
	}
	else
		nextcheck = FAIL;

	UNLOCK_CACHE;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, nextcheck);

	return nextcheck;
}

void	DCrequeue_proxy(trx_uint64_t hostid, unsigned char update_nextcheck, int proxy_conn_err)
{
	time_t		now;
	TRX_DC_HOST	*dc_host;
	TRX_DC_PROXY	*dc_proxy;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() update_nextcheck:%d", __func__, (int)update_nextcheck);

	now = time(NULL);

	WRLOCK_CACHE;

	if (NULL != (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &hostid)) &&
			NULL != (dc_proxy = (TRX_DC_PROXY *)trx_hashset_search(&config->proxies, &hostid)))
	{
		if (TRX_LOC_POLLER == dc_proxy->location)
			dc_proxy->location = TRX_LOC_NOWHERE;

		/* set or clear passive proxy misconfiguration error timestamp */
		if (SUCCEED == proxy_conn_err)
			dc_proxy->last_cfg_error_time = 0;
		else if (CONFIG_ERROR == proxy_conn_err)
			dc_proxy->last_cfg_error_time = (int)now;

		if (HOST_STATUS_PROXY_PASSIVE == dc_host->status)
		{
			if (0 != (update_nextcheck & TRX_PROXY_CONFIG_NEXTCHECK))
			{
				dc_proxy->proxy_config_nextcheck = (int)calculate_proxy_nextcheck(
						hostid, CONFIG_PROXYCONFIG_FREQUENCY, now);
			}

			if (0 != (update_nextcheck & TRX_PROXY_DATA_NEXTCHECK))
			{
				dc_proxy->proxy_data_nextcheck = (int)calculate_proxy_nextcheck(
						hostid, CONFIG_PROXYDATA_FREQUENCY, now);
			}
			if (0 != (update_nextcheck & TRX_PROXY_TASKS_NEXTCHECK))
			{
				dc_proxy->proxy_tasks_nextcheck = (int)calculate_proxy_nextcheck(
						hostid, TRX_TASK_UPDATE_FREQUENCY, now);
			}

			DCupdate_proxy_queue(dc_proxy);
		}
	}

	UNLOCK_CACHE;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	dc_get_host_macro(const trx_uint64_t *hostids, int host_num, const char *macro, const char *context,
		char **value, char **value_default)
{
	int			i, j;
	const TRX_DC_HMACRO_HM	*hmacro_hm;
	TRX_DC_HMACRO_HM	hmacro_hm_local;
	const TRX_DC_HTMPL	*htmpl;
	trx_vector_uint64_t	templateids;

	if (0 == host_num)
		return;

	hmacro_hm_local.macro = macro;

	for (i = 0; i < host_num; i++)
	{
		hmacro_hm_local.hostid = hostids[i];

		if (NULL != (hmacro_hm = (const TRX_DC_HMACRO_HM *)trx_hashset_search(&config->hmacros_hm, &hmacro_hm_local)))
		{
			for (j = 0; j < hmacro_hm->hmacros.values_num; j++)
			{
				const TRX_DC_HMACRO	*hmacro = (const TRX_DC_HMACRO *)hmacro_hm->hmacros.values[j];

				if (0 == strcmp(hmacro->macro, macro))
				{
					if (0 == trx_strcmp_null(hmacro->context, context))
					{
						*value = trx_strdup(*value, hmacro->value);
						return;
					}

					/* check for the default (without parameters) macro value */
					if (NULL == *value_default && NULL != context && NULL == hmacro->context)
						*value_default = trx_strdup(*value_default, hmacro->value);
				}
			}
		}
	}

	trx_vector_uint64_create(&templateids);
	trx_vector_uint64_reserve(&templateids, 32);

	for (i = 0; i < host_num; i++)
	{
		if (NULL != (htmpl = (const TRX_DC_HTMPL *)trx_hashset_search(&config->htmpls, &hostids[i])))
		{
			for (j = 0; j < htmpl->templateids.values_num; j++)
				trx_vector_uint64_append(&templateids, htmpl->templateids.values[j]);
		}
	}

	if (0 != templateids.values_num)
	{
		trx_vector_uint64_sort(&templateids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
		dc_get_host_macro(templateids.values, templateids.values_num, macro, context, value, value_default);
	}

	trx_vector_uint64_destroy(&templateids);
}

static void	dc_get_global_macro(const char *macro, const char *context, char **value, char **value_default)
{
	int			i;
	const TRX_DC_GMACRO_M	*gmacro_m;
	TRX_DC_GMACRO_M		gmacro_m_local;

	gmacro_m_local.macro = macro;

	if (NULL != (gmacro_m = (const TRX_DC_GMACRO_M *)trx_hashset_search(&config->gmacros_m, &gmacro_m_local)))
	{
		for (i = 0; i < gmacro_m->gmacros.values_num; i++)
		{
			const TRX_DC_GMACRO	*gmacro = (const TRX_DC_GMACRO *)gmacro_m->gmacros.values[i];

			if (0 == strcmp(gmacro->macro, macro))
			{
				if (0 == trx_strcmp_null(gmacro->context, context))
				{
					*value = trx_strdup(*value, gmacro->value);
					break;
				}

				/* check for the default (without parameters) macro value */
				if (NULL == *value_default && NULL != context && NULL == gmacro->context)
					*value_default = trx_strdup(*value_default, gmacro->value);
			}
		}
	}
}

static void	dc_get_user_macro(const trx_uint64_t *hostids, int hostids_num, const char *macro, const char *context,
		char **replace_to)
{
	char	*value = NULL, *value_default = NULL;

	/* User macros should be expanded according to the following priority: */
	/*                                                                     */
	/*  1) host context macro                                              */
	/*  2) global context macro                                            */
	/*  3) host base (default) macro                                       */
	/*  4) global base (default) macro                                     */
	/*                                                                     */
	/* We try to expand host macros first. If there is no perfect match on */
	/* the host level, we try to expand global macros, passing the default */
	/* macro value found on the host level, if any.                        */

	dc_get_host_macro(hostids, hostids_num, macro, context, &value, &value_default);

	if (NULL == value)
		dc_get_global_macro(macro, context, &value, &value_default);

	if (NULL != value)
	{
		trx_free(*replace_to);
		*replace_to = value;

		trx_free(value_default);
	}
	else if (NULL != value_default)
	{
		trx_free(*replace_to);
		*replace_to = value_default;
	}
}

void	DCget_user_macro(const trx_uint64_t *hostids, int hostids_num, const char *macro, char **replace_to)
{
	char	*name = NULL, *context = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() macro:'%s'", __func__, macro);

	if (SUCCEED != trx_user_macro_parse_dyn(macro, &name, &context, NULL))
		goto out;

	RDLOCK_CACHE;

	dc_get_user_macro(hostids, hostids_num, name, context, replace_to);

	UNLOCK_CACHE;

	trx_free(context);
	trx_free(name);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_expression_user_macro_validator                               *
 *                                                                            *
 * Purpose: validate user macro values in trigger expressions                 *
 *                                                                            *
 * Parameters: value   - [IN] the macro value                                 *
 *                                                                            *
 * Return value: SUCCEED - the macro value can be used in expression          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	dc_expression_user_macro_validator(const char *value)
{
	if (SUCCEED == is_double_suffix(value, TRX_FLAG_DOUBLE_SUFFIX))
		return SUCCEED;

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_expand_user_macros                                        *
 *                                                                            *
 * Purpose: expand user macros in the specified text value                    *
 *                                                                            *
 * Parameters: text           - [IN] the text value to expand                 *
 *             hostids        - [IN] an array of related hostids              *
 *             hostids_num    - [IN] the number of hostids                    *
 *             validator_func - [IN] an optional validator function           *
 *                                                                            *
 * Return value: The text value with expanded user macros. Unknown or invalid *
 *               macros will be left unresolved.                              *
 *                                                                            *
 * Comments: The returned value must be freed by the caller.                  *
 *           This function must be used only by configuration syncer          *
 *                                                                            *
 ******************************************************************************/
char	*trx_dc_expand_user_macros(const char *text, trx_uint64_t *hostids, int hostids_num,
		trx_macro_value_validator_func_t validator_func)
{
	trx_token_t	token;
	int		pos = 0, len, last_pos = 0;
	char		*str = NULL, *name = NULL, *context = NULL, *value = NULL;
	size_t		str_alloc = 0, str_offset = 0;

	if ('\0' == *text)
		return trx_strdup(NULL, text);

	for (; SUCCEED == trx_token_find(text, pos, &token, TRX_TOKEN_SEARCH_BASIC); pos++)
	{
		if (TRX_TOKEN_USER_MACRO != token.type)
			continue;

		if (SUCCEED != trx_user_macro_parse_dyn(text + token.loc.l, &name, &context, &len))
			continue;

		trx_strncpy_alloc(&str, &str_alloc, &str_offset, text + last_pos, token.loc.l - last_pos);
		dc_get_user_macro(hostids, hostids_num, name, context, &value);

		if (NULL != value && NULL != validator_func && FAIL == validator_func(value))
			trx_free(value);

		if (NULL != value)
		{
			trx_strcpy_alloc(&str, &str_alloc, &str_offset, value);
			trx_free(value);

		}
		else
		{
			trx_strncpy_alloc(&str, &str_alloc, &str_offset, text + token.loc.l,
					token.loc.r - token.loc.l + 1);
		}

		trx_free(name);
		trx_free(context);

		pos = token.loc.r;
		last_pos = pos + 1;
	}

	trx_strcpy_alloc(&str, &str_alloc, &str_offset, text + last_pos);

	return str;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_expression_expand_user_macros                                 *
 *                                                                            *
 * Purpose: expand user macros in trigger expression                          *
 *                                                                            *
 * Parameters: expression - [IN] the expression to expand                     *
 *             error      - [OUT] the error message                           *
 *                                                                            *
 * Return value: The expanded expression or NULL in the case of error.        *
 *               If NULL is returned the error message is set.                *
 *                                                                            *
 * Comments: The returned expression must be freed by the caller.             *
 *                                                                            *
 ******************************************************************************/
static char	*dc_expression_expand_user_macros(const char *expression)
{
	trx_vector_uint64_t	functionids, hostids;
	char			*out;

	trx_vector_uint64_create(&functionids);
	trx_vector_uint64_create(&hostids);

	get_functionids(&functionids, expression);
	trx_dc_get_hostids_by_functionids(functionids.values, functionids.values_num, &hostids);

	out = trx_dc_expand_user_macros(expression, hostids.values, hostids.values_num,
			dc_expression_user_macro_validator);

	if (NULL != strstr(out, "{$"))
	{
		treegix_log(LOG_LEVEL_DEBUG, "cannot evaluate expression: invalid macro value");
		trx_free(out);
	}

	trx_vector_uint64_destroy(&hostids);
	trx_vector_uint64_destroy(&functionids);

	return out;
}

/******************************************************************************
 *                                                                            *
 * Function: DCexpression_expand_user_macros                                  *
 *                                                                            *
 * Purpose: expand user macros in trigger expression                          *
 *                                                                            *
 * Parameters: expression - [IN] the expression to expand                     *
 *                                                                            *
 * Return value: The expanded expression or NULL in the case of error.        *
 *               If NULL is returned the error message is set.                *
 *                                                                            *
 * Comments: The returned expression must be freed by the caller.             *
 *           This function is a locking wrapper of                            *
 *           dc_expression_expand_user_macros() function for external usage.  *
 *                                                                            *
 ******************************************************************************/
char	*DCexpression_expand_user_macros(const char *expression)
{
	char	*expression_ex;

	RDLOCK_CACHE;

	expression_ex = dc_expression_expand_user_macros(expression);

	UNLOCK_CACHE;

	return expression_ex;
}

/******************************************************************************
 *                                                                            *
 * Function: DCfree_item_queue                                                *
 *                                                                            *
 * Purpose: frees the item queue data vector created by DCget_item_queue()    *
 *                                                                            *
 * Parameters: queue - [IN] the item queue data vector to free                *
 *                                                                            *
 ******************************************************************************/
void	DCfree_item_queue(trx_vector_ptr_t *queue)
{
	int	i;

	for (i = 0; i < queue->values_num; i++)
		trx_free(queue->values[i]);
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_item_queue                                                 *
 *                                                                            *
 * Purpose: retrieves vector of delayed items                                 *
 *                                                                            *
 * Parameters: queue - [OUT] the vector of delayed items (optional)           *
 *             from  - [IN] the minimum delay time in seconds (non-negative)  *
 *             to    - [IN] the maximum delay time in seconds or              *
 *                          TRX_QUEUE_TO_INFINITY if there is no limit        *
 *                                                                            *
 * Return value: the number of delayed items                                  *
 *                                                                            *
 ******************************************************************************/
int	DCget_item_queue(trx_vector_ptr_t *queue, int from, int to)
{
	trx_hashset_iter_t	iter;
	const TRX_DC_ITEM	*dc_item;
	int			now, nitems = 0, data_expected_from, delay;
	trx_queue_item_t	*queue_item;

	now = time(NULL);

	RDLOCK_CACHE;

	trx_hashset_iter_reset(&config->items, &iter);

	while (NULL != (dc_item = (const TRX_DC_ITEM *)trx_hashset_iter_next(&iter)))
	{
		const TRX_DC_HOST	*dc_host;

		if (ITEM_STATUS_ACTIVE != dc_item->status)
			continue;

		if (SUCCEED != trx_is_counted_in_item_queue(dc_item->type, dc_item->key))
			continue;

		if (NULL == (dc_host = (const TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		if (SUCCEED == DCin_maintenance_without_data_collection(dc_host, dc_item))
			continue;

		switch (dc_item->type)
		{
			case ITEM_TYPE_TREEGIX:
				if (HOST_AVAILABLE_TRUE != dc_host->available)
					continue;
				break;
			case ITEM_TYPE_TREEGIX_ACTIVE:
				if (dc_host->data_expected_from > (data_expected_from = dc_item->data_expected_from))
					data_expected_from = dc_host->data_expected_from;
				if (SUCCEED != trx_interval_preproc(dc_item->delay, &delay, NULL, NULL))
					continue;
				if (data_expected_from + delay > now)
					continue;
				break;
			case ITEM_TYPE_SNMPv1:
			case ITEM_TYPE_SNMPv2c:
			case ITEM_TYPE_SNMPv3:
				if (HOST_AVAILABLE_TRUE != dc_host->snmp_available)
					continue;
				break;
			case ITEM_TYPE_IPMI:
				if (HOST_AVAILABLE_TRUE != dc_host->ipmi_available)
					continue;
				break;
			case ITEM_TYPE_JMX:
				if (HOST_AVAILABLE_TRUE != dc_host->jmx_available)
					continue;
				break;
		}

		if (now - dc_item->nextcheck < from || (TRX_QUEUE_TO_INFINITY != to && now - dc_item->nextcheck >= to))
			continue;

		if (NULL != queue)
		{
			queue_item = (trx_queue_item_t *)trx_malloc(NULL, sizeof(trx_queue_item_t));
			queue_item->itemid = dc_item->itemid;
			queue_item->type = dc_item->type;
			queue_item->nextcheck = dc_item->nextcheck;
			queue_item->proxy_hostid = dc_host->proxy_hostid;

			trx_vector_ptr_append(queue, queue_item);
		}
		nitems++;
	}

	UNLOCK_CACHE;

	return nitems;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_trigger_items_hosts_enabled                                   *
 *                                                                            *
 * Purpose: check that functionids in trigger (recovery) expression           *
 *          correspond to enabled items and hosts                             *
 *                                                                            *
 * Parameters: expression - [IN] trigger (recovery) expression                *
 *                                                                            *
 * Return value: SUCCEED - all functionids correspond to enabled items and    *
 *                           enabled hosts                                    *
 *               FAIL    - at least one item or host is disabled              *
 *                                                                            *
 ******************************************************************************/
static int	dc_trigger_items_hosts_enabled(const char *expression)
{
	trx_uint64_t		functionid;
	const TRX_DC_ITEM	*dc_item;
	const TRX_DC_FUNCTION	*dc_function;
	const TRX_DC_HOST	*dc_host;
	const char		*p, *q;

	for (p = expression; '\0' != *p; p++)
	{
		if ('{' != *p)
			continue;

		if ('$' == p[1])
		{
			int	macro_r, context_l, context_r;

			if (SUCCEED == trx_user_macro_parse(p, &macro_r, &context_l, &context_r))
				p += macro_r;
			else
				p++;

			continue;
		}

		if (NULL == (q = strchr(p + 1, '}')))
			return FAIL;

		if (SUCCEED != is_uint64_n(p + 1, q - p - 1, &functionid))
			continue;

		if (NULL == (dc_function = (TRX_DC_FUNCTION *)trx_hashset_search(&config->functions, &functionid)) ||
				NULL == (dc_item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &dc_function->itemid)) ||
				ITEM_STATUS_ACTIVE != dc_item->status ||
				NULL == (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)) ||
				HOST_STATUS_MONITORED != dc_host->status)
		{
			return FAIL;
		}

		p = q;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_status_update                                                 *
 *                                                                            *
 * Purpose: check when status information stored in configuration cache was   *
 *          updated last time and update it if necessary                      *
 *                                                                            *
 * Comments: This function gathers the following information:                 *
 *             - number of enabled hosts (total and per proxy)                *
 *             - number of disabled hosts (total and per proxy)               *
 *             - number of enabled and supported items (total, per host and   *
 *                                                                 per proxy) *
 *             - number of enabled and not supported items (total, per host   *
 *                                                             and per proxy) *
 *             - number of disabled items (total and per proxy)               *
 *             - number of enabled triggers with value OK                     *
 *             - number of enabled triggers with value PROBLEM                *
 *             - number of disabled triggers                                  *
 *             - required performance (total and per proxy)                   *
 *           Gathered information can then be displayed in the frontend (see  *
 *           "status.get" request) and used in calculation of treegix[] items. *
 *                                                                            *
 * NOTE: Always call this function before accessing information stored in     *
 *       config->status as well as host and required performance counters     *
 *       stored in elements of config->proxies and item counters in elements  *
 *       of config->hosts.                                                    *
 *                                                                            *
 ******************************************************************************/
static void	dc_status_update(void)
{
#define TRX_STATUS_LIFETIME	SEC_PER_MIN

	trx_hashset_iter_t	iter;
	TRX_DC_PROXY		*dc_proxy;
	TRX_DC_HOST		*dc_host, *dc_proxy_host;
	const TRX_DC_ITEM	*dc_item;
	const TRX_DC_TRIGGER	*dc_trigger;

	if (0 != config->status->last_update && config->status->last_update + TRX_STATUS_LIFETIME > time(NULL))
		return;

	/* reset global counters */

	config->status->hosts_monitored = 0;
	config->status->hosts_not_monitored = 0;
	config->status->items_active_normal = 0;
	config->status->items_active_notsupported = 0;
	config->status->items_disabled = 0;
	config->status->triggers_enabled_ok = 0;
	config->status->triggers_enabled_problem = 0;
	config->status->triggers_disabled = 0;
	config->status->required_performance = 0.0;

	/* loop over proxies to reset per-proxy host and required performance counters */

	trx_hashset_iter_reset(&config->proxies, &iter);

	while (NULL != (dc_proxy = (TRX_DC_PROXY *)trx_hashset_iter_next(&iter)))
	{
		dc_proxy->hosts_monitored = 0;
		dc_proxy->hosts_not_monitored = 0;
		dc_proxy->required_performance = 0.0;
	}

	/* loop over hosts */

	trx_hashset_iter_reset(&config->hosts, &iter);

	while (NULL != (dc_host = (TRX_DC_HOST *)trx_hashset_iter_next(&iter)))
	{
		/* reset per-host/per-proxy item counters */

		dc_host->items_active_normal = 0;
		dc_host->items_active_notsupported = 0;
		dc_host->items_disabled = 0;

		/* gather per-proxy statistics of enabled and disabled hosts */
		switch (dc_host->status)
		{
			case HOST_STATUS_MONITORED:
				config->status->hosts_monitored++;
				if (0 == dc_host->proxy_hostid)
					break;
				if (NULL == (dc_proxy = (TRX_DC_PROXY *)trx_hashset_search(&config->proxies, &dc_host->proxy_hostid)))
					break;
				dc_proxy->hosts_monitored++;
				break;
			case HOST_STATUS_NOT_MONITORED:
				config->status->hosts_not_monitored++;
				if (0 == dc_host->proxy_hostid)
					break;
				if (NULL == (dc_proxy = (TRX_DC_PROXY *)trx_hashset_search(&config->proxies, &dc_host->proxy_hostid)))
					break;
				dc_proxy->hosts_not_monitored++;
				break;
		}
	}

	/* loop over items to gather per-host and per-proxy statistics */

	trx_hashset_iter_reset(&config->items, &iter);

	while (NULL != (dc_item = (TRX_DC_ITEM *)trx_hashset_iter_next(&iter)))
	{
		dc_proxy = NULL;
		dc_proxy_host = NULL;

		if (TRX_FLAG_DISCOVERY_NORMAL != dc_item->flags && TRX_FLAG_DISCOVERY_CREATED != dc_item->flags)
			continue;

		if (NULL == (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (0 != dc_host->proxy_hostid)
		{
			dc_proxy = (TRX_DC_PROXY *)trx_hashset_search(&config->proxies, &dc_host->proxy_hostid);
			dc_proxy_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_host->proxy_hostid);
		}

		switch (dc_item->status)
		{
			case ITEM_STATUS_ACTIVE:
				if (HOST_STATUS_MONITORED == dc_host->status)
				{
					int	delay;

					if (SUCCEED == trx_interval_preproc(dc_item->delay, &delay, NULL, NULL) &&
							0 != delay)
					{
						config->status->required_performance += 1.0 / delay;

						if (NULL != dc_proxy)
							dc_proxy->required_performance += 1.0 / delay;
					}

					switch (dc_item->state)
					{
						case ITEM_STATE_NORMAL:
							config->status->items_active_normal++;
							dc_host->items_active_normal++;
							if (NULL != dc_proxy_host)
								dc_proxy_host->items_active_normal++;
							break;
						case ITEM_STATE_NOTSUPPORTED:
							config->status->items_active_notsupported++;
							dc_host->items_active_notsupported++;
							if (NULL != dc_proxy_host)
								dc_proxy_host->items_active_notsupported++;
							break;
						default:
							THIS_SHOULD_NEVER_HAPPEN;
					}

					break;
				}
				TRX_FALLTHROUGH;
			case ITEM_STATUS_DISABLED:
				config->status->items_disabled++;
				if (NULL != dc_proxy_host)
					dc_proxy_host->items_disabled++;
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}

	/* loop over triggers to gather enabled and disabled trigger statistics */

	trx_hashset_iter_reset(&config->triggers, &iter);

	while (NULL != (dc_trigger = (TRX_DC_TRIGGER *)trx_hashset_iter_next(&iter)))
	{
		switch (dc_trigger->status)
		{
			case TRIGGER_STATUS_ENABLED:
				if (SUCCEED == dc_trigger_items_hosts_enabled(dc_trigger->expression) &&
						(TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION != dc_trigger->recovery_mode ||
						SUCCEED == dc_trigger_items_hosts_enabled(dc_trigger->recovery_expression)))
				{
					switch (dc_trigger->value)
					{
						case TRIGGER_VALUE_OK:
							config->status->triggers_enabled_ok++;
							break;
						case TRIGGER_VALUE_PROBLEM:
							config->status->triggers_enabled_problem++;
							break;
						default:
							THIS_SHOULD_NEVER_HAPPEN;
					}

					break;
				}
				TRX_FALLTHROUGH;
			case TRIGGER_STATUS_DISABLED:
				config->status->triggers_disabled++;
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}

	config->status->last_update = time(NULL);

#undef TRX_STATUS_LIFETIME
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_item_count                                                 *
 *                                                                            *
 * Purpose: return the number of active items                                 *
 *                                                                            *
 * Parameters: hostid - [IN] the host id, pass 0 to specify all hosts         *
 *                                                                            *
 * Return value: the number of active items                                   *
 *                                                                            *
 ******************************************************************************/
trx_uint64_t	DCget_item_count(trx_uint64_t hostid)
{
	trx_uint64_t		count;
	const TRX_DC_HOST	*dc_host;

	WRLOCK_CACHE;

	dc_status_update();

	if (0 == hostid)
		count = config->status->items_active_normal + config->status->items_active_notsupported;
	else if (NULL != (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &hostid)))
		count = dc_host->items_active_normal + dc_host->items_active_notsupported;
	else
		count = 0;

	UNLOCK_CACHE;

	return count;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_item_unsupported_count                                     *
 *                                                                            *
 * Purpose: return the number of active unsupported items                     *
 *                                                                            *
 * Parameters: hostid - [IN] the host id, pass 0 to specify all hosts         *
 *                                                                            *
 * Return value: the number of active unsupported items                       *
 *                                                                            *
 ******************************************************************************/
trx_uint64_t	DCget_item_unsupported_count(trx_uint64_t hostid)
{
	trx_uint64_t		count;
	const TRX_DC_HOST	*dc_host;

	WRLOCK_CACHE;

	dc_status_update();

	if (0 == hostid)
		count = config->status->items_active_notsupported;
	else if (NULL != (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &hostid)))
		count = dc_host->items_active_notsupported;
	else
		count = 0;

	UNLOCK_CACHE;

	return count;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_trigger_count                                              *
 *                                                                            *
 * Purpose: count active triggers                                             *
 *                                                                            *
 ******************************************************************************/
trx_uint64_t	DCget_trigger_count(void)
{
	trx_uint64_t	count;

	WRLOCK_CACHE;

	dc_status_update();

	count = config->status->triggers_enabled_ok + config->status->triggers_enabled_problem;

	UNLOCK_CACHE;

	return count;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_host_count                                                 *
 *                                                                            *
 * Purpose: count monitored and not monitored hosts                           *
 *                                                                            *
 ******************************************************************************/
trx_uint64_t	DCget_host_count(void)
{
	trx_uint64_t	nhosts;

	WRLOCK_CACHE;

	dc_status_update();

	nhosts = config->status->hosts_monitored;

	UNLOCK_CACHE;

	return nhosts;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_required_performance                                       *
 *                                                                            *
 * Return value: the required nvps number                                     *
 *                                                                            *
 ******************************************************************************/
double	DCget_required_performance(void)
{
	double	nvps;

	WRLOCK_CACHE;

	dc_status_update();

	nvps = config->status->required_performance;

	UNLOCK_CACHE;

	return nvps;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_count_stats_all                                            *
 *                                                                            *
 * Purpose: retrieves all internal metrics of the configuration cache         *
 *                                                                            *
 * Parameters: stats - [OUT] the configuration cache statistics               *
 *                                                                            *
 ******************************************************************************/
void	DCget_count_stats_all(trx_config_cache_info_t *stats)
{
	WRLOCK_CACHE;

	dc_status_update();

	stats->hosts = config->status->hosts_monitored;
	stats->items = config->status->items_active_normal + config->status->items_active_notsupported;
	stats->items_unsupported = config->status->items_active_notsupported;
	stats->requiredperformance = config->status->required_performance;

	UNLOCK_CACHE;
}

static void	proxy_counter_ui64_push(trx_vector_ptr_t *vector, trx_uint64_t proxyid, trx_uint64_t counter)
{
	trx_proxy_counter_t	*proxy_counter;

	proxy_counter = (trx_proxy_counter_t *)trx_malloc(NULL, sizeof(trx_proxy_counter_t));
	proxy_counter->proxyid = proxyid;
	proxy_counter->counter_value.ui64 = counter;
	trx_vector_ptr_append(vector, proxy_counter);
}

static void	proxy_counter_dbl_push(trx_vector_ptr_t *vector, trx_uint64_t proxyid, double counter)
{
	trx_proxy_counter_t	*proxy_counter;

	proxy_counter = (trx_proxy_counter_t *)trx_malloc(NULL, sizeof(trx_proxy_counter_t));
	proxy_counter->proxyid = proxyid;
	proxy_counter->counter_value.dbl = counter;
	trx_vector_ptr_append(vector, proxy_counter);
}

void	DCget_status(trx_vector_ptr_t *hosts_monitored, trx_vector_ptr_t *hosts_not_monitored,
		trx_vector_ptr_t *items_active_normal, trx_vector_ptr_t *items_active_notsupported,
		trx_vector_ptr_t *items_disabled, trx_uint64_t *triggers_enabled_ok,
		trx_uint64_t *triggers_enabled_problem, trx_uint64_t *triggers_disabled,
		trx_vector_ptr_t *required_performance)
{
	trx_hashset_iter_t	iter;
	const TRX_DC_PROXY	*dc_proxy;
	const TRX_DC_HOST	*dc_proxy_host;

	WRLOCK_CACHE;

	dc_status_update();

	proxy_counter_ui64_push(hosts_monitored, 0, config->status->hosts_monitored);
	proxy_counter_ui64_push(hosts_not_monitored, 0, config->status->hosts_not_monitored);
	proxy_counter_ui64_push(items_active_normal, 0, config->status->items_active_normal);
	proxy_counter_ui64_push(items_active_notsupported, 0, config->status->items_active_notsupported);
	proxy_counter_ui64_push(items_disabled, 0, config->status->items_disabled);
	*triggers_enabled_ok = config->status->triggers_enabled_ok;
	*triggers_enabled_problem = config->status->triggers_enabled_problem;
	*triggers_disabled = config->status->triggers_disabled;
	proxy_counter_dbl_push(required_performance, 0, config->status->required_performance);

	trx_hashset_iter_reset(&config->proxies, &iter);

	while (NULL != (dc_proxy = (TRX_DC_PROXY *)trx_hashset_iter_next(&iter)))
	{
		proxy_counter_ui64_push(hosts_monitored, dc_proxy->hostid, dc_proxy->hosts_monitored);
		proxy_counter_ui64_push(hosts_not_monitored, dc_proxy->hostid, dc_proxy->hosts_not_monitored);

		if (NULL != (dc_proxy_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_proxy->hostid)))
		{
			proxy_counter_ui64_push(items_active_normal, dc_proxy->hostid,
					dc_proxy_host->items_active_normal);
			proxy_counter_ui64_push(items_active_notsupported, dc_proxy->hostid,
					dc_proxy_host->items_active_notsupported);
			proxy_counter_ui64_push(items_disabled, dc_proxy->hostid, dc_proxy_host->items_disabled);
		}

		proxy_counter_dbl_push(required_performance, dc_proxy->hostid, dc_proxy->required_performance);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_expressions_by_names                                       *
 *                                                                            *
 * Purpose: retrieves global expression data from cache                       *
 *                                                                            *
 * Parameters: expressions  - [OUT] a vector of expression data pointers      *
 *             names        - [IN] a vector containing expression names       *
 *             names_num    - [IN] the number of items in names vector        *
 *                                                                            *
 * Comment: The expressions vector contains allocated data, which must be     *
 *          freed afterwards with trx_regexp_clean_expressions() function.    *
 *                                                                            *
 ******************************************************************************/
void	DCget_expressions_by_names(trx_vector_ptr_t *expressions, const char * const *names, int names_num)
{
	int			i, iname;
	const TRX_DC_EXPRESSION	*expression;
	const TRX_DC_REGEXP	*regexp;
	TRX_DC_REGEXP		search_regexp;

	RDLOCK_CACHE;

	for (iname = 0; iname < names_num; iname++)
	{
		search_regexp.name = names[iname];

		if (NULL != (regexp = (const TRX_DC_REGEXP *)trx_hashset_search(&config->regexps, &search_regexp)))
		{
			for (i = 0; i < regexp->expressionids.values_num; i++)
			{
				trx_uint64_t		expressionid = regexp->expressionids.values[i];
				trx_expression_t	*rxp;

				if (NULL == (expression = (const TRX_DC_EXPRESSION *)trx_hashset_search(&config->expressions, &expressionid)))
					continue;

				rxp = (trx_expression_t *)trx_malloc(NULL, sizeof(trx_expression_t));
				rxp->name = trx_strdup(NULL, regexp->name);
				rxp->expression = trx_strdup(NULL, expression->expression);
				rxp->exp_delimiter = expression->delimiter;
				rxp->case_sensitive = expression->case_sensitive;
				rxp->expression_type = expression->type;

				trx_vector_ptr_append(expressions, rxp);
			}
		}
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_expression                                                 *
 *                                                                            *
 * Purpose: retrieves regular expression data from cache                      *
 *                                                                            *
 * Parameters: expressions  - [OUT] a vector of expression data pointers      *
 *             name         - [IN] the regular expression name                *
 *                                                                            *
 * Comment: The expressions vector contains allocated data, which must be     *
 *          freed afterwards with trx_regexp_clean_expressions() function.    *
 *                                                                            *
 ******************************************************************************/
void	DCget_expressions_by_name(trx_vector_ptr_t *expressions, const char *name)
{
	DCget_expressions_by_names(expressions, &name, 1);
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_data_expected_from                                         *
 *                                                                            *
 * Purpose: Returns time since which data is expected for the given item. We  *
 *          would not mind not having data for the item before that time, but *
 *          since that time we expect data to be coming.                      *
 *                                                                            *
 * Parameters: itemid  - [IN] the item id                                     *
 *             seconds - [OUT] the time data is expected as a Unix timestamp  *
 *                                                                            *
 ******************************************************************************/
int	DCget_data_expected_from(trx_uint64_t itemid, int *seconds)
{
	const TRX_DC_ITEM	*dc_item;
	const TRX_DC_HOST	*dc_host;
	int			ret = FAIL;

	RDLOCK_CACHE;

	if (NULL == (dc_item = (const TRX_DC_ITEM *)trx_hashset_search(&config->items, &itemid)))
		goto unlock;

	if (ITEM_STATUS_ACTIVE != dc_item->status)
		goto unlock;

	if (NULL == (dc_host = (const TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)))
		goto unlock;

	if (HOST_STATUS_MONITORED != dc_host->status)
		goto unlock;

	*seconds = MAX(dc_item->data_expected_from, dc_host->data_expected_from);

	ret = SUCCEED;
unlock:
	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_get_hostids_by_functionids                                    *
 *                                                                            *
 * Purpose: get host identifiers for the specified list of functions          *
 *                                                                            *
 * Parameters: functionids     - [IN] the function ids                        *
 *             functionids_num - [IN] the number of function ids              *
 *             hostids         - [OUT] the host ids                           *
 *                                                                            *
 * Comments: this function must be used only by configuration syncer          *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_get_hostids_by_functionids(const trx_uint64_t *functionids, int functionids_num,
		trx_vector_uint64_t *hostids)
{
	const TRX_DC_FUNCTION	*function;
	const TRX_DC_ITEM	*item;
	int			i;

	for (i = 0; i < functionids_num; i++)
	{
		if (NULL == (function = (const TRX_DC_FUNCTION *)trx_hashset_search(&config->functions, &functionids[i])))
				continue;

		if (NULL != (item = (const TRX_DC_ITEM *)trx_hashset_search(&config->items, &function->itemid)))
			trx_vector_uint64_append(hostids, item->hostid);
	}

	trx_vector_uint64_sort(hostids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_uniq(hostids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_hostids_by_functionids                                     *
 *                                                                            *
 * Purpose: get function host ids grouped by an object (trigger) id           *
 *                                                                            *
 * Parameters: functionids - [IN] the function ids                            *
 *             hostids     - [OUT] the host ids                               *
 *                                                                            *
 ******************************************************************************/
void	DCget_hostids_by_functionids(trx_vector_uint64_t *functionids, trx_vector_uint64_t *hostids)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	RDLOCK_CACHE;

	trx_dc_get_hostids_by_functionids(functionids->values, functionids->values_num, hostids);

	UNLOCK_CACHE;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s(): found %d hosts", __func__, hostids->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_get_hosts_by_functionids                                      *
 *                                                                            *
 * Purpose: get hosts for the specified list of functions                     *
 *                                                                            *
 * Parameters: functionids     - [IN] the function ids                        *
 *             functionids_num - [IN] the number of function ids              *
 *             hosts           - [OUT] hosts                                  *
 *                                                                            *
 ******************************************************************************/
static void	dc_get_hosts_by_functionids(const trx_uint64_t *functionids, int functionids_num, trx_hashset_t *hosts)
{
	const TRX_DC_FUNCTION	*dc_function;
	const TRX_DC_ITEM	*dc_item;
	const TRX_DC_HOST	*dc_host;
	DC_HOST			host;
	int			i;

	for (i = 0; i < functionids_num; i++)
	{
		if (NULL == (dc_function = (const TRX_DC_FUNCTION *)trx_hashset_search(&config->functions, &functionids[i])))
			continue;

		if (NULL == (dc_item = (const TRX_DC_ITEM *)trx_hashset_search(&config->items, &dc_function->itemid)))
			continue;

		if (NULL == (dc_host = (const TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		DCget_host(&host, dc_host);
		trx_hashset_insert(hosts, &host, sizeof(host));
	}
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_hosts_by_functionids                                       *
 *                                                                            *
 * Purpose: get hosts for the specified list of functions                     *
 *                                                                            *
 * Parameters: functionids - [IN] the function ids                            *
 *             hosts       - [OUT] hosts                                      *
 *                                                                            *
 ******************************************************************************/
void	DCget_hosts_by_functionids(const trx_vector_uint64_t *functionids, trx_hashset_t *hosts)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	RDLOCK_CACHE;

	dc_get_hosts_by_functionids(functionids->values, functionids->values_num, hosts);

	UNLOCK_CACHE;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s(): found %d hosts", __func__, hosts->num_data);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_config_get                                                   *
 *                                                                            *
 * Purpose: get global configuration data                                     *
 *                                                                            *
 * Parameters: cfg   - [OUT] the global configuration data                    *
 *             flags - [IN] the flags specifying fields to get,               *
 *                          see TRX_CONFIG_FLAGS_ defines                     *
 *                                                                            *
 * Comments: It's recommended to cleanup 'cfg' structure after use with       *
 *           trx_config_clean() function even if only simple fields were      *
 *           requested.                                                       *
 *                                                                            *
 ******************************************************************************/
void	trx_config_get(trx_config_t *cfg, trx_uint64_t flags)
{
	RDLOCK_CACHE;

	if (0 != (flags & TRX_CONFIG_FLAGS_SEVERITY_NAME))
	{
		int	i;

		cfg->severity_name = (char **)trx_malloc(NULL, TRIGGER_SEVERITY_COUNT * sizeof(char *));

		for (i = 0; i < TRIGGER_SEVERITY_COUNT; i++)
			cfg->severity_name[i] = trx_strdup(NULL, config->config->severity_name[i]);
	}

	if (0 != (flags & TRX_CONFIG_FLAGS_DISCOVERY_GROUPID))
		cfg->discovery_groupid = config->config->discovery_groupid;

	if (0 != (flags & TRX_CONFIG_FLAGS_DEFAULT_INVENTORY_MODE))
		cfg->default_inventory_mode = config->config->default_inventory_mode;

	if (0 != (flags & TRX_CONFIG_FLAGS_REFRESH_UNSUPPORTED))
		cfg->refresh_unsupported = config->config->refresh_unsupported;

	if (0 != (flags & TRX_CONFIG_FLAGS_SNMPTRAP_LOGGING))
		cfg->snmptrap_logging = config->config->snmptrap_logging;

	if (0 != (flags & TRX_CONFIG_FLAGS_HOUSEKEEPER))
		cfg->hk = config->config->hk;

	if (0 != (flags & TRX_CONFIG_FLAGS_DB_EXTENSION))
		cfg->db_extension = trx_strdup(NULL, config->config->db_extension);

	if (0 != (flags & TRX_CONFIG_FLAGS_AUTOREG_TLS_ACCEPT))
		cfg->autoreg_tls_accept = config->config->autoreg_tls_accept;

	UNLOCK_CACHE;

	cfg->flags = flags;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_config_clean                                                 *
 *                                                                            *
 * Purpose: cleans global configuration data structure filled                 *
 *          by trx_config_get() function                                      *
 *                                                                            *
 * Parameters: cfg   - [IN] the global configuration data                     *
 *                                                                            *
 ******************************************************************************/
void	trx_config_clean(trx_config_t *cfg)
{
	if (0 != (cfg->flags & TRX_CONFIG_FLAGS_SEVERITY_NAME))
	{
		int	i;

		for (i = 0; i < TRIGGER_SEVERITY_COUNT; i++)
			trx_free(cfg->severity_name[i]);

		trx_free(cfg->severity_name);
	}

	if (0 != (cfg->flags & TRX_CONFIG_FLAGS_DB_EXTENSION))
		trx_free(cfg->db_extension);
}

/******************************************************************************
 *                                                                            *
 * Function: DCreset_hosts_availability                                       *
 *                                                                            *
 * Purpose: resets host availability for disabled hosts and hosts without     *
 *          enabled items for the corresponding interface                     *
 *                                                                            *
 * Parameters: hosts - [OUT] changed host availability data                   *
 *                                                                            *
 * Return value: SUCCEED - host availability was reset for at least one host  *
 *               FAIL    - no hosts required availability reset               *
 *                                                                            *
 * Comments: This function resets host availability in configuration cache.   *
 *           The caller must perform corresponding database updates based     *
 *           on returned host availability reset data. On server the function *
 *           skips hosts handled by proxies.                                  *
 *                                                                            *
 ******************************************************************************/
int	DCreset_hosts_availability(trx_vector_ptr_t *hosts)
{
	TRX_DC_HOST		*host;
	trx_hashset_iter_t	iter;
	trx_host_availability_t	*ha = NULL;
	int			now;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	now = time(NULL);

	WRLOCK_CACHE;

	trx_hashset_iter_reset(&config->hosts, &iter);

	while (NULL != (host = (TRX_DC_HOST *)trx_hashset_iter_next(&iter)))
	{
		int	items_num = 0, snmp_items_num = 0, ipmi_items_num = 0, jmx_items_num = 0;

		/* On server skip hosts handled by proxies. They are handled directly */
		/* when receiving hosts' availability data from proxies.              */
		/* Unless a host was just (re)assigned to a proxy or the proxy has    */
		/* not updated its status during the maximum proxy heartbeat period.  */
		/* In this case reset all interfaces to unknown status.               */
		if (0 == host->reset_availability &&
				0 != (program_type & TRX_PROGRAM_TYPE_SERVER) && 0 != host->proxy_hostid)
		{
			TRX_DC_PROXY	*proxy;

			if (NULL != (proxy = (TRX_DC_PROXY *)trx_hashset_search(&config->proxies, &host->proxy_hostid)))
			{
				/* SEC_PER_MIN is a tolerance interval, it was chosen arbitrarily */
				if (TRX_PROXY_HEARTBEAT_FREQUENCY_MAX + SEC_PER_MIN >= now - proxy->lastaccess)
					continue;
			}

			host->reset_availability = 1;
		}

		if (NULL == ha)
			ha = (trx_host_availability_t *)trx_malloc(NULL, sizeof(trx_host_availability_t));

		trx_host_availability_init(ha, host->hostid);

		if (0 == host->reset_availability)
		{
			items_num = host->items_num;
			snmp_items_num = host->snmp_items_num;
			ipmi_items_num = host->ipmi_items_num;
			jmx_items_num = host->jmx_items_num;
		}

		if (0 == items_num && HOST_AVAILABLE_UNKNOWN != host->available)
			trx_agent_availability_init(&ha->agents[TRX_AGENT_TREEGIX], HOST_AVAILABLE_UNKNOWN, "", 0, 0);

		if (0 == snmp_items_num && HOST_AVAILABLE_UNKNOWN != host->snmp_available)
			trx_agent_availability_init(&ha->agents[TRX_AGENT_SNMP], HOST_AVAILABLE_UNKNOWN, "", 0, 0);

		if (0 == ipmi_items_num && HOST_AVAILABLE_UNKNOWN != host->ipmi_available)
			trx_agent_availability_init(&ha->agents[TRX_AGENT_IPMI], HOST_AVAILABLE_UNKNOWN, "", 0, 0);

		if (0 == jmx_items_num && HOST_AVAILABLE_UNKNOWN != host->jmx_available)
			trx_agent_availability_init(&ha->agents[TRX_AGENT_JMX], HOST_AVAILABLE_UNKNOWN, "", 0, 0);

		if (SUCCEED == trx_host_availability_is_set(ha))
		{
			if (SUCCEED == DChost_set_availability(host, now, ha))
			{
				trx_vector_ptr_append(hosts, ha);
				ha = NULL;
			}
			else
				trx_host_availability_clean(ha);
		}

		host->reset_availability = 0;
	}
	UNLOCK_CACHE;

	trx_free(ha);

	trx_vector_ptr_sort(hosts, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() hosts:%d", __func__, hosts->values_num);

	return 0 == hosts->values_num ? FAIL : SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_hosts_availability                                         *
 *                                                                            *
 * Purpose: gets availability data for hosts with availability data changed   *
 *          in period from last availability update to the specified          *
 *          timestamp                                                         *
 *                                                                            *
 * Parameters: hosts - [OUT] changed host availability data                   *
 *             ts    - [OUT] the availability diff timestamp                  *
 *                                                                            *
 * Return value: SUCCEED - availability was changed for at least one host     *
 *               FAIL    - no host availability was changed                   *
 *                                                                            *
 ******************************************************************************/
int	DCget_hosts_availability(trx_vector_ptr_t *hosts, int *ts)
{
	const TRX_DC_HOST	*host;
	trx_hashset_iter_t	iter;
	trx_host_availability_t	*ha = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	RDLOCK_CACHE;

	*ts = time(NULL);

	trx_hashset_iter_reset(&config->hosts, &iter);

	while (NULL != (host = (const TRX_DC_HOST *)trx_hashset_iter_next(&iter)))
	{
		if (config->availability_diff_ts <= host->availability_ts && host->availability_ts < *ts)
		{
			ha = (trx_host_availability_t *)trx_malloc(NULL, sizeof(trx_host_availability_t));
			trx_host_availability_init(ha, host->hostid);

			trx_agent_availability_init(&ha->agents[TRX_AGENT_TREEGIX], host->available, host->error,
					host->errors_from, host->disable_until);
			trx_agent_availability_init(&ha->agents[TRX_AGENT_SNMP], host->snmp_available, host->snmp_error,
					host->snmp_errors_from, host->snmp_disable_until);
			trx_agent_availability_init(&ha->agents[TRX_AGENT_IPMI], host->ipmi_available, host->ipmi_error,
					host->ipmi_errors_from, host->ipmi_disable_until);
			trx_agent_availability_init(&ha->agents[TRX_AGENT_JMX], host->jmx_available, host->jmx_error,
					host->jmx_errors_from, host->jmx_disable_until);

			trx_vector_ptr_append(hosts, ha);
		}
	}

	UNLOCK_CACHE;

	trx_vector_ptr_sort(hosts, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() hosts:%d", __func__, hosts->values_num);

	return 0 == hosts->values_num ? FAIL : SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: DCtouch_hosts_availability                                       *
 *                                                                            *
 * Purpose: sets availability timestamp to current time for the specified     *
 *          hosts                                                             *
 *                                                                            *
 * Parameters: hostids - [IN] the host identifiers                            *
 *                                                                            *
 ******************************************************************************/
void	DCtouch_hosts_availability(const trx_vector_uint64_t *hostids)
{
	TRX_DC_HOST	*dc_host;
	int		i, now;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() hostids:%d", __func__, hostids->values_num);

	now = time(NULL);

	WRLOCK_CACHE;

	for (i = 0; i < hostids->values_num; i++)
	{
		if (NULL != (dc_host = trx_hashset_search(&config->hosts, &hostids->values[i])))
			dc_host->availability_ts = now;
	}

	UNLOCK_CACHE;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_condition_clean                                           *
 *                                                                            *
 * Purpose: cleans condition data structure                                   *
 *                                                                            *
 * Parameters: condition - [IN] the condition data to free                    *
 *                                                                            *
 ******************************************************************************/
void	trx_db_condition_clean(DB_CONDITION *condition)
{
	trx_free(condition->value2);
	trx_free(condition->value);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_conditions_eval_clean                                        *
 *                                                                            *
 * Purpose: cleans condition data structures from hashset                     *
 *                                                                            *
 * Parameters: uniq_conditions - [IN] hashset with data structures to clean   *
 *                                                                            *
 ******************************************************************************/
void	trx_conditions_eval_clean(trx_hashset_t *uniq_conditions)
{
	trx_hashset_iter_t	iter;
	DB_CONDITION		*condition;

	trx_hashset_iter_reset(uniq_conditions, &iter);

	while (NULL != (condition = (DB_CONDITION *)trx_hashset_iter_next(&iter)))
		trx_db_condition_clean(condition);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_action_eval_free                                             *
 *                                                                            *
 * Purpose: frees action evaluation data structure                            *
 *                                                                            *
 * Parameters: action - [IN] the action evaluation to free                    *
 *                                                                            *
 ******************************************************************************/
void	trx_action_eval_free(trx_action_eval_t *action)
{
	trx_free(action->formula);

	trx_vector_ptr_destroy(&action->conditions);

	trx_free(action);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_action_copy_conditions                                        *
 *                                                                            *
 * Purpose: copies configuration cache action conditions to the specified     *
 *          vector                                                            *
 *                                                                            *
 * Parameters: dc_action  - [IN] the source action                            *
 *             conditions - [OUT] the conditions vector                       *
 *                                                                            *
 ******************************************************************************/
static void	dc_action_copy_conditions(const trx_dc_action_t *dc_action, trx_vector_ptr_t *conditions)
{
	int				i;
	DB_CONDITION			*condition;
	trx_dc_action_condition_t	*dc_condition;

	trx_vector_ptr_reserve(conditions, dc_action->conditions.values_num);

	for (i = 0; i < dc_action->conditions.values_num; i++)
	{
		dc_condition = (trx_dc_action_condition_t *)dc_action->conditions.values[i];

		condition = (DB_CONDITION *)trx_malloc(NULL, sizeof(DB_CONDITION));

		condition->conditionid = dc_condition->conditionid;
		condition->actionid = dc_action->actionid;
		condition->conditiontype = dc_condition->conditiontype;
		condition->op = dc_condition->op;
		condition->value = trx_strdup(NULL, dc_condition->value);
		condition->value2 = trx_strdup(NULL, dc_condition->value2);

		trx_vector_ptr_append(conditions, condition);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_action_eval_create                                            *
 *                                                                            *
 * Purpose: creates action evaluation data from configuration cache action    *
 *                                                                            *
 * Parameters: dc_action - [IN] the source action                             *
 *                                                                            *
 * Return value: the action evaluation data                                   *
 *                                                                            *
 * Comments: The returned value must be freed with trx_action_eval_free()     *
 *           function later.                                                  *
 *                                                                            *
 ******************************************************************************/
static trx_action_eval_t	*dc_action_eval_create(const trx_dc_action_t *dc_action)
{
	trx_action_eval_t		*action;

	action = (trx_action_eval_t *)trx_malloc(NULL, sizeof(trx_action_eval_t));

	action->actionid = dc_action->actionid;
	action->eventsource = dc_action->eventsource;
	action->evaltype = dc_action->evaltype;
	action->opflags = dc_action->opflags;
	action->formula = trx_strdup(NULL, dc_action->formula);
	trx_vector_ptr_create(&action->conditions);

	dc_action_copy_conditions(dc_action, &action->conditions);

	return action;
}

/******************************************************************************
 *                                                                            *
 * Function: prepare_actions_eval                                             *
 *                                                                            *
 * Purpose: make actions to point, to conditions from hashset, where all      *
 *          conditions are unique, this ensures that we don't double check    *
 *          same conditions.                                                  *
 *                                                                            *
 * Parameters: actions         - [IN/OUT] all conditions are added to hashset *
 *                                        then cleaned, actions will now      *
 *                                        point to conditions from hashset.   *
 *                                        for custom expression also          *
 *                                        replaces formula                    *
 *             uniq_conditions - [OUT]    unique conditions that actions      *
 *                                        point to (several sources)          *
 *                                                                            *
 * Comments: The returned conditions must be freed with                       *
 *           trx_conditions_eval_clean() function later.                      *
 *                                                                            *
 ******************************************************************************/
static void	prepare_actions_eval(trx_vector_ptr_t *actions, trx_hashset_t *uniq_conditions)
{
	int	i, j;

	for (i = 0; i < actions->values_num; i++)
	{
		trx_action_eval_t	*action = (trx_action_eval_t *)actions->values[i];

		for (j = 0; j < action->conditions.values_num; j++)
		{
			DB_CONDITION	*uniq_condition = NULL, *condition = (DB_CONDITION *)action->conditions.values[j];

			if (EVENT_SOURCE_COUNT <= action->eventsource)
			{
				trx_db_condition_clean(condition);
			}
			else if (NULL == (uniq_condition = (DB_CONDITION *)trx_hashset_search(&uniq_conditions[action->eventsource],
					condition)))
			{
				uniq_condition = (DB_CONDITION *)trx_hashset_insert(&uniq_conditions[action->eventsource],
						condition, sizeof(DB_CONDITION));
			}
			else
			{
				if (CONDITION_EVAL_TYPE_EXPRESSION == action->evaltype)
				{
					char	search[TRX_MAX_UINT64_LEN + 2];
					char	replace[TRX_MAX_UINT64_LEN + 2];
					char	*old_formula;

					trx_snprintf(search, sizeof(search), "{" TRX_FS_UI64 "}",
							condition->conditionid);
					trx_snprintf(replace, sizeof(replace), "{" TRX_FS_UI64 "}",
							uniq_condition->conditionid);

					old_formula = action->formula;
					action->formula = string_replace(action->formula, search, replace);
					trx_free(old_formula);
				}

				trx_db_condition_clean(condition);
			}

			trx_free(action->conditions.values[j]);
			action->conditions.values[j] = uniq_condition;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_get_actions_eval                                          *
 *                                                                            *
 * Purpose: gets action evaluation data                                       *
 *                                                                            *
 * Parameters: actions         - [OUT] the action evaluation data             *
 *             uniq_conditions - [OUT] unique conditions that actions         *
 *                                     point to (several sources)             *
 *             opflags         - [IN] flags specifying which actions to get   *
 *                                    based on their operation classes        *
 *                                    (see TRX_ACTION_OPCLASS_* defines)      *
 *                                                                            *
 * Comments: The returned actions and conditions must be freed with           *
 *           trx_action_eval_free() and trx_conditions_eval_clean()           *
 *           functions later.                                                 *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_get_actions_eval(trx_vector_ptr_t *actions, trx_hashset_t *uniq_conditions, unsigned char opflags)
{
	const trx_dc_action_t		*dc_action;
	trx_hashset_iter_t		iter;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	RDLOCK_CACHE;

	trx_hashset_iter_reset(&config->actions, &iter);

	while (NULL != (dc_action = (const trx_dc_action_t *)trx_hashset_iter_next(&iter)))
	{
		if (0 != (opflags & dc_action->opflags))
			trx_vector_ptr_append(actions, dc_action_eval_create(dc_action));
	}

	UNLOCK_CACHE;

	prepare_actions_eval(actions, uniq_conditions);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() actions:%d", __func__, actions->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_set_availability_update_ts                                   *
 *                                                                            *
 * Purpose: sets timestamp of the last availability update                    *
 *                                                                            *
 * Parameter: ts - [IN] the last availability update timestamp                *
 *                                                                            *
 * Comments: This function is used only by proxies when preparing host        *
 *           availability data to be sent to server.                          *
 *                                                                            *
 ******************************************************************************/
void	trx_set_availability_diff_ts(int ts)
{
	/* this data can't be accessed simultaneously from multiple processes - locking is not necessary */
	config->availability_diff_ts = ts;
}

/******************************************************************************
 *                                                                            *
 * Function: corr_condition_clean                                             *
 *                                                                            *
 * Purpose: frees correlation condition                                       *
 *                                                                            *
 * Parameter: condition - [IN] the condition to free                          *
 *                                                                            *
 ******************************************************************************/
static void	corr_condition_clean(trx_corr_condition_t *condition)
{
	switch (condition->type)
	{
		case TRX_CORR_CONDITION_OLD_EVENT_TAG:
			/* break; is not missing here */
		case TRX_CORR_CONDITION_NEW_EVENT_TAG:
			trx_free(condition->data.tag.tag);
			break;
		case TRX_CORR_CONDITION_EVENT_TAG_PAIR:
			trx_free(condition->data.tag_pair.oldtag);
			trx_free(condition->data.tag_pair.newtag);
			break;
		case TRX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
			/* break; is not missing here */
		case TRX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
			trx_free(condition->data.tag_value.tag);
			trx_free(condition->data.tag_value.value);
			break;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_correlation_free                                              *
 *                                                                            *
 * Purpose: frees global correlation rule                                     *
 *                                                                            *
 * Parameter: condition - [IN] the condition to free                          *
 *                                                                            *
 ******************************************************************************/
static void	dc_correlation_free(trx_correlation_t *correlation)
{
	trx_free(correlation->name);
	trx_free(correlation->formula);

	trx_vector_ptr_clear_ext(&correlation->operations, trx_ptr_free);
	trx_vector_ptr_destroy(&correlation->operations);
	trx_vector_ptr_destroy(&correlation->conditions);

	trx_free(correlation);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_corr_condition_copy                                           *
 *                                                                            *
 * Purpose: copies cached correlation condition to memory                     *
 *                                                                            *
 * Parameter: dc_condition - [IN] the condition to copy                       *
 *            condition    - [OUT] the destination condition                  *
 *                                                                            *
 * Return value: The cloned correlation condition.                            *
 *                                                                            *
 ******************************************************************************/
static void	dc_corr_condition_copy(const trx_dc_corr_condition_t *dc_condition, trx_corr_condition_t *condition)
{
	condition->type = dc_condition->type;

	switch (condition->type)
	{
		case TRX_CORR_CONDITION_OLD_EVENT_TAG:
			/* break; is not missing here */
		case TRX_CORR_CONDITION_NEW_EVENT_TAG:
			condition->data.tag.tag = trx_strdup(NULL, dc_condition->data.tag.tag);
			break;
		case TRX_CORR_CONDITION_EVENT_TAG_PAIR:
			condition->data.tag_pair.oldtag = trx_strdup(NULL, dc_condition->data.tag_pair.oldtag);
			condition->data.tag_pair.newtag = trx_strdup(NULL, dc_condition->data.tag_pair.newtag);
			break;
		case TRX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
			/* break; is not missing here */
		case TRX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
			condition->data.tag_value.tag = trx_strdup(NULL, dc_condition->data.tag_value.tag);
			condition->data.tag_value.value = trx_strdup(NULL, dc_condition->data.tag_value.value);
			condition->data.tag_value.op = dc_condition->data.tag_value.op;
			break;
		case TRX_CORR_CONDITION_NEW_EVENT_HOSTGROUP:
			condition->data.group.groupid = dc_condition->data.group.groupid;
			condition->data.group.op = dc_condition->data.group.op;
			break;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_corr_operation_dup                                        *
 *                                                                            *
 * Purpose: clones cached correlation operation to memory                     *
 *                                                                            *
 * Parameter: operation - [IN] the operation to clone                         *
 *                                                                            *
 * Return value: The cloned correlation operation.                            *
 *                                                                            *
 ******************************************************************************/
static trx_corr_operation_t	*trx_dc_corr_operation_dup(const trx_dc_corr_operation_t *dc_operation)
{
	trx_corr_operation_t	*operation;

	operation = (trx_corr_operation_t *)trx_malloc(NULL, sizeof(trx_corr_operation_t));
	operation->type = dc_operation->type;

	return operation;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_correlation_formula_dup                                       *
 *                                                                            *
 * Purpose: clones cached correlation formula, generating it if necessary     *
 *                                                                            *
 * Parameter: correlation - [IN] the correlation                              *
 *                                                                            *
 * Return value: The cloned correlation formula.                              *
 *                                                                            *
 ******************************************************************************/
static char	*dc_correlation_formula_dup(const trx_dc_correlation_t *dc_correlation)
{
#define TRX_OPERATION_TYPE_UNKNOWN	0
#define TRX_OPERATION_TYPE_OR		1
#define TRX_OPERATION_TYPE_AND		2

	char				*formula = NULL;
	const char			*op = NULL;
	size_t				formula_alloc = 0, formula_offset = 0;
	int				i, last_type = -1, last_op = TRX_OPERATION_TYPE_UNKNOWN;
	const trx_dc_corr_condition_t	*dc_condition;
	trx_uint64_t			last_id;

	if (CONDITION_EVAL_TYPE_EXPRESSION == dc_correlation->evaltype || 0 == dc_correlation->conditions.values_num)
		return trx_strdup(NULL, dc_correlation->formula);

	dc_condition = (const trx_dc_corr_condition_t *)dc_correlation->conditions.values[0];

	switch (dc_correlation->evaltype)
	{
		case CONDITION_EVAL_TYPE_OR:
			op = " or";
			break;
		case CONDITION_EVAL_TYPE_AND:
			op = " and";
			break;
	}

	if (NULL != op)
	{
		trx_snprintf_alloc(&formula, &formula_alloc, &formula_offset, "{" TRX_FS_UI64 "}",
				dc_condition->corr_conditionid);

		for (i = 1; i < dc_correlation->conditions.values_num; i++)
		{
			dc_condition = (const trx_dc_corr_condition_t *)dc_correlation->conditions.values[i];

			trx_strcpy_alloc(&formula, &formula_alloc, &formula_offset, op);
			trx_snprintf_alloc(&formula, &formula_alloc, &formula_offset, " {" TRX_FS_UI64 "}",
					dc_condition->corr_conditionid);
		}

		return formula;
	}

	last_id = dc_condition->corr_conditionid;
	last_type = dc_condition->type;

	for (i = 1; i < dc_correlation->conditions.values_num; i++)
	{
		dc_condition = (const trx_dc_corr_condition_t *)dc_correlation->conditions.values[i];

		if (last_type == dc_condition->type)
		{
			if (last_op != TRX_OPERATION_TYPE_OR)
				trx_chrcpy_alloc(&formula, &formula_alloc, &formula_offset, '(');

			trx_snprintf_alloc(&formula, &formula_alloc, &formula_offset, "{" TRX_FS_UI64 "} or ", last_id);
			last_op = TRX_OPERATION_TYPE_OR;
		}
		else
		{
			trx_snprintf_alloc(&formula, &formula_alloc, &formula_offset, "{" TRX_FS_UI64 "}", last_id);

			if (last_op == TRX_OPERATION_TYPE_OR)
				trx_chrcpy_alloc(&formula, &formula_alloc, &formula_offset, ')');

			trx_strcpy_alloc(&formula, &formula_alloc, &formula_offset, " and ");

			last_op = TRX_OPERATION_TYPE_AND;
		}

		last_type = dc_condition->type;
		last_id = dc_condition->corr_conditionid;
	}

	trx_snprintf_alloc(&formula, &formula_alloc, &formula_offset, "{" TRX_FS_UI64 "}", last_id);

	if (last_op == TRX_OPERATION_TYPE_OR)
		trx_chrcpy_alloc(&formula, &formula_alloc, &formula_offset, ')');

	return formula;

#undef TRX_OPERATION_TYPE_UNKNOWN
#undef TRX_OPERATION_TYPE_OR
#undef TRX_OPERATION_TYPE_AND
}

void	trx_dc_correlation_rules_init(trx_correlation_rules_t *rules)
{
	trx_vector_ptr_create(&rules->correlations);
	trx_hashset_create_ext(&rules->conditions, 0, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC,
			(trx_clean_func_t)corr_condition_clean, TRX_DEFAULT_MEM_MALLOC_FUNC,
			TRX_DEFAULT_MEM_REALLOC_FUNC, TRX_DEFAULT_MEM_FREE_FUNC);

	rules->sync_ts = 0;
}

void	trx_dc_correlation_rules_clean(trx_correlation_rules_t *rules)
{
	trx_vector_ptr_clear_ext(&rules->correlations, (trx_clean_func_t)dc_correlation_free);
	trx_hashset_clear(&rules->conditions);
}

void	trx_dc_correlation_rules_free(trx_correlation_rules_t *rules)
{
	trx_dc_correlation_rules_clean(rules);
	trx_vector_ptr_destroy(&rules->correlations);
	trx_hashset_destroy(&rules->conditions);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_correlation_get_rules                                     *
 *                                                                            *
 * Purpose: gets correlation rules from configuration cache                   *
 *                                                                            *
 * Parameter: rules   - [IN/OUT] the correlation rules                        *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_correlation_rules_get(trx_correlation_rules_t *rules)
{
	int				i;
	trx_hashset_iter_t		iter;
	const trx_dc_correlation_t	*dc_correlation;
	const trx_dc_corr_condition_t	*dc_condition;
	trx_correlation_t		*correlation;
	trx_corr_condition_t		*condition, condition_local;

	RDLOCK_CACHE;

	/* The correlation rules are refreshed only if the sync timestamp   */
	/* does not match current configuration cache sync timestamp. This  */
	/* allows to locally cache the correlation rules.                   */
	if (config->sync_ts == rules->sync_ts)
	{
		UNLOCK_CACHE;
		return;
	}

	trx_dc_correlation_rules_clean(rules);

	trx_hashset_iter_reset(&config->correlations, &iter);
	while (NULL != (dc_correlation = (const trx_dc_correlation_t *)trx_hashset_iter_next(&iter)))
	{
		correlation = (trx_correlation_t *)trx_malloc(NULL, sizeof(trx_correlation_t));
		correlation->correlationid = dc_correlation->correlationid;
		correlation->evaltype = dc_correlation->evaltype;
		correlation->name = trx_strdup(NULL, dc_correlation->name);
		correlation->formula = dc_correlation_formula_dup(dc_correlation);
		trx_vector_ptr_create(&correlation->conditions);
		trx_vector_ptr_create(&correlation->operations);

		for (i = 0; i < dc_correlation->conditions.values_num; i++)
		{
			dc_condition = (const trx_dc_corr_condition_t *)dc_correlation->conditions.values[i];
			condition_local.corr_conditionid = dc_condition->corr_conditionid;
			condition = (trx_corr_condition_t *)trx_hashset_insert(&rules->conditions, &condition_local, sizeof(condition_local));
			dc_corr_condition_copy(dc_condition, condition);
			trx_vector_ptr_append(&correlation->conditions, condition);
		}

		for (i = 0; i < dc_correlation->operations.values_num; i++)
		{
			trx_vector_ptr_append(&correlation->operations,
					trx_dc_corr_operation_dup((const trx_dc_corr_operation_t *)dc_correlation->operations.values[i]));
		}

		trx_vector_ptr_append(&rules->correlations, correlation);
	}

	rules->sync_ts = config->sync_ts;

	UNLOCK_CACHE;

	trx_vector_ptr_sort(&rules->correlations, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_hostgroup_cache_nested_groupids                               *
 *                                                                            *
 * Purpose: cache nested group identifiers                                    *
 *                                                                            *
 ******************************************************************************/
void	dc_hostgroup_cache_nested_groupids(trx_dc_hostgroup_t *parent_group)
{
	trx_dc_hostgroup_t	*group;

	if (0 == (parent_group->flags & TRX_DC_HOSTGROUP_FLAGS_NESTED_GROUPIDS))
	{
		int	index, len;

		trx_vector_uint64_create_ext(&parent_group->nested_groupids, __config_mem_malloc_func,
				__config_mem_realloc_func, __config_mem_free_func);

		index = trx_vector_ptr_bsearch(&config->hostgroups_name, parent_group, dc_compare_hgroups);
		len = strlen(parent_group->name);

		while (++index < config->hostgroups_name.values_num)
		{
			group = (trx_dc_hostgroup_t *)config->hostgroups_name.values[index];

			if (0 != strncmp(group->name, parent_group->name, len))
				break;

			if ('\0' == group->name[len] || '/' == group->name[len])
				trx_vector_uint64_append(&parent_group->nested_groupids, group->groupid);
		}

		parent_group->flags |= TRX_DC_HOSTGROUP_FLAGS_NESTED_GROUPIDS;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_maintenance_precache_nested_groups                            *
 *                                                                            *
 * Purpose: pre-caches nested groups for groups used in running maintenances  *
 *                                                                            *
 ******************************************************************************/
static void	dc_maintenance_precache_nested_groups(void)
{
	trx_hashset_iter_t	iter;
	trx_dc_maintenance_t	*maintenance;
	trx_vector_uint64_t	groupids;
	int			i;
	trx_dc_hostgroup_t	*group;

	if (0 == config->maintenances.num_data)
		return;

	trx_vector_uint64_create(&groupids);
	trx_hashset_iter_reset(&config->maintenances, &iter);
	while (NULL != (maintenance = (trx_dc_maintenance_t *)trx_hashset_iter_next(&iter)))
	{
		if (TRX_MAINTENANCE_RUNNING != maintenance->state)
			continue;

		trx_vector_uint64_append_array(&groupids, maintenance->groupids.values,
				maintenance->groupids.values_num);
	}

	trx_vector_uint64_sort(&groupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_uniq(&groupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	for (i = 0; i < groupids.values_num; i++)
	{
		if (NULL != (group = (trx_dc_hostgroup_t *)trx_hashset_search(&config->hostgroups,
				&groupids.values[i])))
		{
			dc_hostgroup_cache_nested_groupids(group);
		}
	}

	trx_vector_uint64_destroy(&groupids);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_get_nested_hostgroupids                                       *
 *                                                                            *
 * Purpose: gets nested group ids for the specified host group                *
 *          (including the target group id)                                   *
 *                                                                            *
 * Parameter: groupid         - [IN] the parent group identifier              *
 *            nested_groupids - [OUT] the nested + parent group ids           *
 *                                                                            *
 ******************************************************************************/
void	dc_get_nested_hostgroupids(trx_uint64_t groupid, trx_vector_uint64_t *nested_groupids)
{
	trx_dc_hostgroup_t	*parent_group;

	trx_vector_uint64_append(nested_groupids, groupid);

	/* The target group id will not be found in the configuration cache if target group was removed */
	/* between call to this function and the configuration cache look-up below. The target group id */
	/* is nevertheless returned so that the SELECT statements of the callers work even if no group  */
	/* was found.                                                                                   */

	if (NULL != (parent_group = (trx_dc_hostgroup_t *)trx_hashset_search(&config->hostgroups, &groupid)))
	{
		dc_hostgroup_cache_nested_groupids(parent_group);

		if (0 != parent_group->nested_groupids.values_num)
		{
			trx_vector_uint64_append_array(nested_groupids, parent_group->nested_groupids.values,
					parent_group->nested_groupids.values_num);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_get_nested_hostgroupids                                   *
 *                                                                            *
 * Purpose: gets nested group ids for the specified host groups               *
 *                                                                            *
 * Parameter: groupids        - [IN] the parent group identifiers             *
 *            groupids_num    - [IN] the number of parent groups              *
 *            nested_groupids - [OUT] the nested + parent group ids           *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_get_nested_hostgroupids(trx_uint64_t *groupids, int groupids_num, trx_vector_uint64_t *nested_groupids)
{
	int	i;

	WRLOCK_CACHE;

	for (i = 0; i < groupids_num; i++)
		dc_get_nested_hostgroupids(groupids[i], nested_groupids);

	UNLOCK_CACHE;

	trx_vector_uint64_sort(nested_groupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_uniq(nested_groupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_get_nested_hostgroupids_by_names                          *
 *                                                                            *
 * Purpose: gets nested group ids for the specified host groups               *
 *                                                                            *
 * Parameter: names           - [IN] the parent group names                   *
 *            names_num       - [IN] the number of parent groups              *
 *            nested_groupids - [OUT] the nested + parent group ids           *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_get_nested_hostgroupids_by_names(char **names, int names_num, trx_vector_uint64_t *nested_groupids)
{
	int	i, index;

	WRLOCK_CACHE;

	for (i = 0; i < names_num; i++)
	{
		trx_dc_hostgroup_t	group_local, *group;

		group_local.name = names[i];

		if (FAIL != (index = trx_vector_ptr_bsearch(&config->hostgroups_name, &group_local,
				dc_compare_hgroups)))
		{
			group = (trx_dc_hostgroup_t *)config->hostgroups_name.values[index];
			dc_get_nested_hostgroupids(group->groupid, nested_groupids);
		}
	}

	UNLOCK_CACHE;

	trx_vector_uint64_sort(nested_groupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_uniq(nested_groupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_get_active_proxy_by_name                                  *
 *                                                                            *
 * Purpose: gets active proxy data by its name from configuration cache       *
 *                                                                            *
 * Parameters:                                                                *
 *     name  - [IN] the proxy name                                            *
 *     proxy - [OUT] the proxy data                                           *
 *     error - [OUT] error message                                            *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - proxy data were retrieved successfully                       *
 *     FAIL    - failed to retrieve proxy data, error message is set          *
 *                                                                            *
 ******************************************************************************/
int	trx_dc_get_active_proxy_by_name(const char *name, DC_PROXY *proxy, char **error)
{
	int			ret = FAIL;
	const TRX_DC_HOST	*dc_host;
	const TRX_DC_PROXY	*dc_proxy;

	RDLOCK_CACHE;

	if (NULL == (dc_host = DCfind_proxy(name)))
	{
		*error = trx_dsprintf(*error, "proxy \"%s\" not found", name);
		goto out;
	}

	if (HOST_STATUS_PROXY_ACTIVE != dc_host->status)
	{
		*error = trx_dsprintf(*error, "proxy \"%s\" is configured for passive mode", name);
		goto out;
	}

	if (NULL == (dc_proxy = (const TRX_DC_PROXY *)trx_hashset_search(&config->proxies, &dc_host->hostid)))
	{
		*error = trx_dsprintf(*error, "proxy \"%s\" not found in configuration cache", name);
		goto out;
	}

	DCget_proxy(proxy, dc_proxy);
	ret = SUCCEED;
out:
	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_items_update_nextcheck                                    *
 *                                                                            *
 * Purpose: updates item nextcheck values in configuration cache              *
 *                                                                            *
 * Parameters: items      - [IN] the items to update                          *
 *             values     - [IN] the items values containing new properties   *
 *             errcodes   - [IN] item error codes. Update only items with     *
 *                               SUCCEED code                                 *
 *             values_num - [IN] the number of elements in items,values and   *
 *                               errcodes arrays                              *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_items_update_nextcheck(DC_ITEM *items, trx_agent_value_t *values, int *errcodes, size_t values_num)
{
	size_t		i;
	TRX_DC_ITEM	*dc_item;
	TRX_DC_HOST	*dc_host;

	WRLOCK_CACHE;

	for (i = 0; i < values_num; i++)
	{
		if (FAIL == errcodes[i])
			continue;

		if (NULL == (dc_item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &items[i].itemid)))
			continue;

		if (ITEM_STATUS_ACTIVE != dc_item->status)
			continue;

		if (NULL == (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		if (TRX_LOC_NOWHERE != dc_item->location)
			continue;

		/* update nextcheck for items that are counted in queue for monitoring purposes */
		if (SUCCEED == trx_is_counted_in_item_queue(dc_item->type, dc_item->key))
			DCitem_nextcheck_update(dc_item, dc_host, items[i].state, TRX_ITEM_COLLECTED, values[i].ts.sec,
					NULL);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_get_host_interfaces                                       *
 *                                                                            *
 * Purpose: get data of all network interfaces for a host in configuration    *
 *          cache                                                             *
 *                                                                            *
 * Parameter: hostid     - [IN] the host identifier                           *
 *            interfaces - [OUT] array with interface data                    *
 *            n          - [OUT] number of allocated 'interfaces' elements    *
 *                                                                            *
 * Return value: SUCCEED - interface data retrieved successfully              *
 *               FAIL    - host not found                                     *
 *                                                                            *
 * Comments: if host is found but has no interfaces (should not happen) this  *
 *           function sets 'n' to 0 and no memory is allocated for            *
 *           'interfaces'. It is a caller responsibility to deallocate        *
 *           memory of 'interfaces' and its components.                       *
 *                                                                            *
 ******************************************************************************/
int	trx_dc_get_host_interfaces(trx_uint64_t hostid, DC_INTERFACE2 **interfaces, int *n)
{
	const TRX_DC_HOST	*host;
	int			i, ret = FAIL;

	if (0 == hostid)
		return FAIL;

	RDLOCK_CACHE;

	/* find host entry in 'config->hosts' hashset */

	if (NULL == (host = (const TRX_DC_HOST *)trx_hashset_search(&config->hosts, &hostid)))
		goto unlock;

	/* allocate memory for results */

	if (0 < (*n = host->interfaces_v.values_num))
		*interfaces = (DC_INTERFACE2 *)trx_malloc(NULL, sizeof(DC_INTERFACE2) * (size_t)*n);

	/* copy data about all host interfaces */

	for (i = 0; i < *n; i++)
	{
		const TRX_DC_INTERFACE	*src = (const TRX_DC_INTERFACE *)host->interfaces_v.values[i];
		DC_INTERFACE2		*dst = *interfaces + i;

		dst->interfaceid = src->interfaceid;
		dst->type = src->type;
		dst->main = src->main;
		dst->bulk = src->bulk;
		dst->useip = src->useip;
		strscpy(dst->ip_orig, src->ip);
		strscpy(dst->dns_orig, src->dns);
		strscpy(dst->port_orig, src->port);
		dst->addr = (1 == src->useip ? dst->ip_orig : dst->dns_orig);
	}

	ret = SUCCEED;
unlock:
	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_items_apply_changes                                     *
 *                                                                            *
 * Purpose: apply item state, error, mtime, lastlogsize changes to            *
 *          configuration cache                                               *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_items_apply_changes(const trx_vector_ptr_t *item_diff)
{
	int			i;
	const trx_item_diff_t	*diff;
	TRX_DC_ITEM		*dc_item;

	if (0 == item_diff->values_num)
		return;

	WRLOCK_CACHE;

	for (i = 0; i < item_diff->values_num; i++)
	{
		diff = (const trx_item_diff_t *)item_diff->values[i];

		if (NULL == (dc_item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &diff->itemid)))
			continue;

		if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_LASTLOGSIZE & diff->flags))
			dc_item->lastlogsize = diff->lastlogsize;

		if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_MTIME & diff->flags))
			dc_item->mtime = diff->mtime;

		if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_ERROR & diff->flags))
			DCstrpool_replace(1, &dc_item->error, diff->error);

		if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_STATE & diff->flags))
			dc_item->state = diff->state;

		if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_LASTCLOCK & diff->flags))
			dc_item->lastclock = diff->lastclock;
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_update_inventory_values                                 *
 *                                                                            *
 * Purpose: update automatic inventory in configuration cache                 *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_update_inventory_values(const trx_vector_ptr_t *inventory_values)
{
	TRX_DC_HOST_INVENTORY	*host_inventory = NULL;
	int			i;

	WRLOCK_CACHE;

	for (i = 0; i < inventory_values->values_num; i++)
	{
		const trx_inventory_value_t	*inventory_value = (trx_inventory_value_t *)inventory_values->values[i];
		const char			**value;

		if (NULL == host_inventory || inventory_value->hostid != host_inventory->hostid)
		{
			host_inventory = (TRX_DC_HOST_INVENTORY *)trx_hashset_search(&config->host_inventories_auto, &inventory_value->hostid);

			if (NULL == host_inventory)
				continue;
		}

		value = &host_inventory->values[inventory_value->idx];

		DCstrpool_replace((NULL != *value ? 1 : 0), value, inventory_value->value);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_get_host_inventory_value_by_hostid                            *
 *                                                                            *
 * Purpose: find inventory value in automatically populated cache, if not     *
 *          found then look in main inventory cache                           *
 *                                                                            *
 * Comments: This function must be called inside configuration cache read     *
 *           (or write) lock.                                                 *
 *                                                                            *
 ******************************************************************************/
static int	dc_get_host_inventory_value_by_hostid(trx_uint64_t hostid, char **replace_to, int value_idx)
{
	const TRX_DC_HOST_INVENTORY	*dc_inventory;

	if (NULL != (dc_inventory = (const TRX_DC_HOST_INVENTORY *)trx_hashset_search(&config->host_inventories_auto,
			&hostid)) && NULL != dc_inventory->values[value_idx])
	{
		*replace_to = trx_strdup(*replace_to, dc_inventory->values[value_idx]);
		return SUCCEED;
	}

	if (NULL != (dc_inventory = (const TRX_DC_HOST_INVENTORY *)trx_hashset_search(&config->host_inventories,
			&hostid)))
	{
		*replace_to = trx_strdup(*replace_to, dc_inventory->values[value_idx]);
		return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_host_inventory_value_by_itemid                             *
 *                                                                            *
 * Purpose: find inventory value in automatically populated cache, if not     *
 *          found then look in main inventory cache                           *
 *                                                                            *
 ******************************************************************************/
int	DCget_host_inventory_value_by_itemid(trx_uint64_t itemid, char **replace_to, int value_idx)
{
	const TRX_DC_ITEM	*dc_item;
	int			ret = FAIL;

	RDLOCK_CACHE;

	if (NULL != (dc_item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &itemid)))
		ret = dc_get_host_inventory_value_by_hostid(dc_item->hostid, replace_to, value_idx);

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_host_inventory_value_by_hostid                             *
 *                                                                            *
 * Purpose: find inventory value in automatically populated cache, if not     *
 *          found then look in main inventory cache                           *
 *                                                                            *
 ******************************************************************************/
int	DCget_host_inventory_value_by_hostid(trx_uint64_t hostid, char **replace_to, int value_idx)
{
	int	ret;

	RDLOCK_CACHE;

	ret = dc_get_host_inventory_value_by_hostid(hostid, replace_to, value_idx);

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_get_trigger_dependencies                                  *
 *                                                                            *
 * Purpose: checks/returns trigger dependencies for a set of triggers         *
 *                                                                            *
 * Parameter: triggerids  - [IN] the currently processing trigger ids         *
 *            deps        - [OUT] list of dependency check results for failed *
 *                                or unresolved dependencies                  *
 *                                                                            *
 * Comments: This function returns list of trx_trigger_dep_t structures       *
 *           for failed or unresolved dependency checks.                      *
 *           Dependency check is failed if any of the master triggers that    *
 *           are not being processed in this batch (present in triggerids     *
 *           vector) has a problem value.                                     *
 *           Dependency check is unresolved if a master trigger is being      *
 *           processed in this batch (present in triggerids vector) and no    *
 *           other master triggers have problem value.                        *
 *           Dependency check is successful if all master triggers (if any)   *
 *           have OK value and are not being processed in this batch.         *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_get_trigger_dependencies(const trx_vector_uint64_t *triggerids, trx_vector_ptr_t *deps)
{
	int				i, ret;
	const TRX_DC_TRIGGER_DEPLIST	*trigdep;
	trx_vector_uint64_t		masterids;
	trx_trigger_dep_t		*dep;

	trx_vector_uint64_create(&masterids);
	trx_vector_uint64_reserve(&masterids, 64);

	RDLOCK_CACHE;

	for (i = 0; i < triggerids->values_num; i++)
	{
		if (NULL == (trigdep = (TRX_DC_TRIGGER_DEPLIST *)trx_hashset_search(&config->trigdeps, &triggerids->values[i])))
			continue;

		if (FAIL == (ret = DCconfig_check_trigger_dependencies_rec(trigdep, 0, triggerids, &masterids)) ||
				0 != masterids.values_num)
		{
			dep = (trx_trigger_dep_t *)trx_malloc(NULL, sizeof(trx_trigger_dep_t));
			dep->triggerid = triggerids->values[i];
			trx_vector_uint64_create(&dep->masterids);

			if (SUCCEED == ret)
			{
				dep->status = TRX_TRIGGER_DEPENDENCY_UNRESOLVED;
				trx_vector_uint64_append_array(&dep->masterids, masterids.values, masterids.values_num);
			}
			else
				dep->status = TRX_TRIGGER_DEPENDENCY_FAIL;

			trx_vector_ptr_append(deps, dep);
		}

		trx_vector_uint64_clear(&masterids);
	}

	UNLOCK_CACHE;

	trx_vector_uint64_destroy(&masterids);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_reschedule_items                                          *
 *                                                                            *
 * Purpose: reschedules items that are processed by the target daemon         *
 *                                                                            *
 * Parameter: itemids       - [IN] the item identifiers                       *
 *            nextcheck     - [IN] the schedueld time                         *
 *            proxy_hostids - [OUT] the proxy_hostids of the given itemids    *
 *                                  (optional, can be NULL)                   *
 *                                                                            *
 * Comments: On server this function reschedules items monitored by server.   *
 *           On proxy only items monitored by the proxy is accessible, so     *
 *           all items can be safely rescheduled.                             *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_reschedule_items(const trx_vector_uint64_t *itemids, int nextcheck, trx_uint64_t *proxy_hostids)
{
	int		i;
	TRX_DC_ITEM	*dc_item;
	TRX_DC_HOST	*dc_host;
	trx_uint64_t	proxy_hostid;

	WRLOCK_CACHE;

	for (i = 0; i < itemids->values_num; i++)
	{
		if (NULL == (dc_item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &itemids->values[i])) ||
				NULL == (dc_host = (TRX_DC_HOST *)trx_hashset_search(&config->hosts, &dc_item->hostid)))
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot perform check now for itemid [" TRX_FS_UI64 "]"
					": item is not in cache", itemids->values[i]);

			proxy_hostid = 0;
		}
		else if (0 == dc_item->schedulable)
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot perform check now for item \"%s\" on host \"%s\""
					": item configuration error", dc_item->key, dc_host->host);

			proxy_hostid = 0;
		}
		else if (0 == (proxy_hostid = dc_host->proxy_hostid) ||
				SUCCEED == is_item_processed_by_server(dc_item->type, dc_item->key))
		{
			dc_requeue_item_at(dc_item, dc_host, nextcheck);
			proxy_hostid = 0;
		}

		if (NULL != proxy_hostids)
			proxy_hostids[i] = proxy_hostid;
	}

	UNLOCK_CACHE;
}


/******************************************************************************
 *                                                                            *
 * Function: trx_dc_update_proxy                                              *
 *                                                                            *
 * Purpose: updates changed proxy data in configuration cache and updates     *
 *          diff flags to reflect the updated data                            *
 *                                                                            *
 * Parameter: diff - [IN/OUT] the properties to update                        *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_update_proxy(trx_proxy_diff_t *diff)
{
	TRX_DC_PROXY	*proxy;

	WRLOCK_CACHE;

	if (diff->lastaccess < config->proxy_lastaccess_ts)
		diff->lastaccess = config->proxy_lastaccess_ts;

	if (NULL != (proxy = (TRX_DC_PROXY *)trx_hashset_search(&config->proxies, &diff->hostid)))
	{
		if (0 != (diff->flags & TRX_FLAGS_PROXY_DIFF_UPDATE_LASTACCESS))
		{
			if (proxy->lastaccess != diff->lastaccess)
				proxy->lastaccess = diff->lastaccess;

			/* proxy last access in database is updated separately in  */
			/* every TRX_PROXY_LASTACCESS_UPDATE_FREQUENCY seconds     */
			diff->flags &= (~TRX_FLAGS_PROXY_DIFF_UPDATE_LASTACCESS);
		}

		if (0 != (diff->flags & TRX_FLAGS_PROXY_DIFF_UPDATE_VERSION))
		{
			if (proxy->version != diff->version)
				proxy->version = diff->version;
			else
				diff->flags &= (~TRX_FLAGS_PROXY_DIFF_UPDATE_VERSION);
		}

		if (0 != (diff->flags & TRX_FLAGS_PROXY_DIFF_UPDATE_COMPRESS))
		{
			if (proxy->auto_compress != diff->compress)
				proxy->auto_compress = diff->compress;
			else
				diff->flags &= (~TRX_FLAGS_PROXY_DIFF_UPDATE_COMPRESS);
		}
		if (0 != (diff->flags & TRX_FLAGS_PROXY_DIFF_UPDATE_LASTERROR))
		{
			if (proxy-> last_version_error_time != diff->last_version_error_time)
				proxy->last_version_error_time = diff->last_version_error_time;
			diff->flags &= (~TRX_FLAGS_PROXY_DIFF_UPDATE_LASTERROR);
		}
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_get_proxy_lastaccess                                      *
 *                                                                            *
 * Purpose: returns proxy lastaccess changes since last lastaccess request    *
 *                                                                            *
 * Parameter: lastaccess - [OUT] last access updates for proxies that need    *
 *                               to be synced with database, sorted by        *
 *                               hostid                                       *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_get_proxy_lastaccess(trx_vector_uint64_pair_t *lastaccess)
{
	TRX_DC_PROXY	*proxy;
	int		now;

	if (TRX_PROXY_LASTACCESS_UPDATE_FREQUENCY < (now = time(NULL)) - config->proxy_lastaccess_ts)
	{
		trx_hashset_iter_t	iter;

		WRLOCK_CACHE;

		trx_hashset_iter_reset(&config->proxies, &iter);

		while (NULL != (proxy = (TRX_DC_PROXY *)trx_hashset_iter_next(&iter)))
		{
			if (proxy->lastaccess >= config->proxy_lastaccess_ts)
			{
				trx_uint64_pair_t	pair = {proxy->hostid, proxy->lastaccess};

				trx_vector_uint64_pair_append(lastaccess, pair);
			}
		}

		config->proxy_lastaccess_ts = now;

		UNLOCK_CACHE;

		trx_vector_uint64_pair_sort(lastaccess, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_get_session_token                                         *
 *                                                                            *
 * Purpose: returns session token                                             *
 *                                                                            *
 * Return value: pointer to session token (NULL for server).                  *
 *                                                                            *
 * Comments: The session token is generated during configuration cache        *
 *           initialization and is not changed later. Therefore no locking    *
 *           is required.                                                     *
 *                                                                            *
 ******************************************************************************/
const char	*trx_dc_get_session_token(void)
{
	return config->session_token;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_get_or_create_data_session                                *
 *                                                                            *
 * Purpose: returns data session, creates a new session if none found         *
 *                                                                            *
 * Parameter: hostid - [IN] the host (proxy) identifier                       *
 *            token  - [IN] the session token (not NULL)                      *
 *                                                                            *
 * Return value: pointer to data session.                                     *
 *                                                                            *
 * Comments: The last_valueid property of the returned session object can be  *
 *           updated directly without locking cache because only one data     *
 *           session is updated at the same time and after retrieving the     *
 *           session object will not be deleted for 24 hours.                 *
 *                                                                            *
 ******************************************************************************/
trx_data_session_t	*trx_dc_get_or_create_data_session(trx_uint64_t hostid, const char *token)
{
	trx_data_session_t	*session, session_local;
	time_t			now;

	now = time(NULL);
	session_local.hostid = hostid;
	session_local.token = token;

	RDLOCK_CACHE;
	session = (trx_data_session_t *)trx_hashset_search(&config->data_sessions, &session_local);
	UNLOCK_CACHE;

	if (NULL == session)
	{
		WRLOCK_CACHE;
		session = (trx_data_session_t *)trx_hashset_insert(&config->data_sessions, &session_local,
				sizeof(session_local));
		session->token = dc_strdup(token);
		UNLOCK_CACHE;

		session->last_valueid = 0;
	}

	session->lastaccess = now;

	return session;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dc_cleanup_data_sessions                                     *
 *                                                                            *
 * Purpose: removes data sessions not accessed for 24 hours                   *
 *                                                                            *
 ******************************************************************************/
void	trx_dc_cleanup_data_sessions(void)
{
	trx_data_session_t	*session;
	trx_hashset_iter_t	iter;
	time_t			now;

	now = time(NULL);

	WRLOCK_CACHE;

	trx_hashset_iter_reset(&config->data_sessions, &iter);
	while (NULL != (session = (trx_data_session_t *)trx_hashset_iter_next(&iter)))
	{
		if (session->lastaccess + SEC_PER_DAY <= now)
		{
			__config_mem_free_func((char *)session->token);
			trx_hashset_iter_remove(&iter);
		}
	}

	UNLOCK_CACHE;
}

static void	trx_gather_tags_from_host(trx_uint64_t hostid, trx_vector_ptr_t *item_tags)
{
	trx_dc_host_tag_index_t 	*dc_tag_index;
	trx_dc_host_tag_t		*dc_tag;
	trx_item_tag_t			*tag;
	int				i;

	if (NULL != (dc_tag_index = trx_hashset_search(&config->host_tags_index, &hostid)))
	{
		for (i = 0; i < dc_tag_index->tags.values_num; i++)
		{
			dc_tag = (trx_dc_host_tag_t *)dc_tag_index->tags.values[i];
			tag = (trx_item_tag_t *) trx_malloc(NULL, sizeof(trx_item_tag_t));
			tag->tag.tag = trx_strdup(NULL, dc_tag->tag);
			tag->tag.value = trx_strdup(NULL, dc_tag->value);
			trx_vector_ptr_append(item_tags, tag);
		}
	}
}

static void	trx_gather_tags_from_template_chain(trx_uint64_t itemid, trx_vector_ptr_t *item_tags)
{
	TRX_DC_TEMPLATE_ITEM	*item;

	if (NULL != (item = (TRX_DC_TEMPLATE_ITEM *)trx_hashset_search(&config->template_items, &itemid)))
	{
		trx_gather_tags_from_host(item->hostid, item_tags);

		if (0 != item->templateid)
			trx_gather_tags_from_template_chain(item->templateid, item_tags);
	}
}

static void	trx_get_item_tags(trx_uint64_t itemid, trx_vector_ptr_t *item_tags)
{
	TRX_DC_ITEM		*item;
	TRX_DC_PROTOTYPE_ITEM	*lld_item;
	trx_item_tag_t		*tag;
	int			n, i;

	if (NULL == (item = (TRX_DC_ITEM *)trx_hashset_search(&config->items, &itemid)))
		return;

	n = item_tags->values_num;

	trx_gather_tags_from_host(item->hostid, item_tags);

	if (0 != item->templateid)
		trx_gather_tags_from_template_chain(item->templateid, item_tags);

	/* check for discovered item */
	if (0 != item->parent_itemid && 4 == item->flags)
	{
		if (NULL != (lld_item = (TRX_DC_PROTOTYPE_ITEM *)trx_hashset_search(&config->prototype_items,
				&item->parent_itemid)))
		{
			if (0 != lld_item->templateid)
				trx_gather_tags_from_template_chain(lld_item->templateid, item_tags);
		}
	}

	/* assign hostid and itemid values to newly gathered tags */
	for (i = n; i < item_tags->values_num; i++)
	{
		tag = (trx_item_tag_t *)item_tags->values[i];
		tag->hostid = item->hostid;
		tag->itemid = item->itemid;
	}
}

void	trx_dc_get_item_tags_by_functionids(const trx_uint64_t *functionids, size_t functionids_num,
		trx_vector_ptr_t *item_tags)
{
	const TRX_DC_FUNCTION	*dc_function;
	size_t			i;

	RDLOCK_CACHE;

	for (i = 0; i < functionids_num; i++)
	{
		if (NULL == (dc_function = (const TRX_DC_FUNCTION *)trx_hashset_search(&config->functions,
				&functionids[i])))
		{
			continue;
		}

		trx_get_item_tags(dc_function->itemid, item_tags);
	}

	UNLOCK_CACHE;
}

#ifdef HAVE_TESTS
#	include "../../../tests/libs/trxdbcache/dc_item_poller_type_update_test.c"
#endif
