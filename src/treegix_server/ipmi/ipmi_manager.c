

#include "common.h"

#ifdef HAVE_OPENIPMI

#include "dbcache.h"
#include "daemon.h"
#include "trxself.h"
#include "log.h"
#include "trxipcservice.h"
#include "trxalgo.h"
#include "trxserver.h"
#include "preproc.h"

#include "ipmi_manager.h"
#include "ipmi_protocol.h"
#include "checks_ipmi.h"
#include "ipmi.h"

#include "../poller/poller.h"

#define TRX_IPMI_MANAGER_DELAY	1

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

extern int	CONFIG_IPMIPOLLER_FORKS;

#define TRX_IPMI_POLLER_INIT		0
#define TRX_IPMI_POLLER_READY		1
#define TRX_IPMI_POLLER_BUSY		2

#define TRX_IPMI_MANAGER_CLEANUP_DELAY		SEC_PER_HOUR
#define TRX_IPMI_MANAGER_HOST_TTL		SEC_PER_DAY

/* IPMI request queued by pollers */
typedef struct
{
	/* internal requestid */
	trx_uint64_t		requestid;

	/* target host id */
	trx_uint64_t		hostid;

	/* itemid, set for value requests */
	trx_uint64_t		itemid;

	/* the current item state (supported/unsupported) */
	unsigned char		item_state;

	/* the request message */
	trx_ipc_message_t	message;

	/* the source client for external requests (command request) */
	trx_ipc_client_t	*client;
}
trx_ipmi_request_t;

/* IPMI poller data */
typedef struct
{
	/* the connected IPMI poller client */
	trx_ipc_client_t	*client;

	/* the request queue */
	trx_binary_heap_t	requests;

	/* the currently processing request */
	trx_ipmi_request_t	*request;

	/* the number of hosts handled by the poller */
	int			hosts_num;
}
trx_ipmi_poller_t;

/* cached host data */
typedef struct
{
	trx_uint64_t		hostid;
	int			disable_until;
	int			lastcheck;
	trx_ipmi_poller_t	*poller;
}
trx_ipmi_manager_host_t;

/* IPMI manager data */
typedef struct
{
	/* IPMI poller vector, created during manager initialization */
	trx_vector_ptr_t	pollers;

	/* IPMI pollers indexed by IPC service clients */
	trx_hashset_t		pollers_client;

	/* IPMI pollers sorted by number of hosts being monitored */
	trx_binary_heap_t	pollers_load;

	/* the next poller index to be assigned to new IPC service clients */
	int			next_poller_index;

	/* monitored hosts cache */
	trx_hashset_t		hosts;
}
trx_ipmi_manager_t;

/* pollers_client hashset support */

static trx_hash_t	poller_hash_func(const void *d)
{
	const trx_ipmi_poller_t	*poller = *(const trx_ipmi_poller_t **)d;

	trx_hash_t hash =  TRX_DEFAULT_PTR_HASH_FUNC(&poller->client);

	return hash;
}

static int	poller_compare_func(const void *d1, const void *d2)
{
	const trx_ipmi_poller_t	*p1 = *(const trx_ipmi_poller_t **)d1;
	const trx_ipmi_poller_t	*p2 = *(const trx_ipmi_poller_t **)d2;

	TRX_RETURN_IF_NOT_EQUAL(p1->client, p2->client);
	return 0;
}

/* pollers_load binary heap support */

static int	ipmi_poller_compare_load(const void *d1, const void *d2)
{
	const trx_binary_heap_elem_t	*e1 = (const trx_binary_heap_elem_t *)d1;
	const trx_binary_heap_elem_t	*e2 = (const trx_binary_heap_elem_t *)d2;

	const trx_ipmi_poller_t		*p1 = (const trx_ipmi_poller_t *)e1->data;
	const trx_ipmi_poller_t		*p2 = (const trx_ipmi_poller_t *)e2->data;

	return p1->hosts_num - p2->hosts_num;
}

/* pollers requests binary heap support */

static int	ipmi_request_priority(const trx_ipmi_request_t *request)
{
	switch (request->message.code)
	{
		case TRX_IPC_IPMI_VALUE_REQUEST:
			return 1;
		case TRX_IPC_IPMI_SCRIPT_REQUEST:
			return 0;
		default:
			return INT_MAX;
	}
}

/* There can be two request types in the queue - TRX_IPC_IPMI_VALUE_REQUEST and TRX_IPC_IPMI_COMMAND_REQUEST. */
/* Prioritize command requests over value requests.                                                           */
static int	ipmi_request_compare(const void *d1, const void *d2)
{
	const trx_binary_heap_elem_t	*e1 = (const trx_binary_heap_elem_t *)d1;
	const trx_binary_heap_elem_t	*e2 = (const trx_binary_heap_elem_t *)d2;

	const trx_ipmi_request_t	*r1 = (const trx_ipmi_request_t *)e1->data;
	const trx_ipmi_request_t	*r2 = (const trx_ipmi_request_t *)e2->data;

	TRX_RETURN_IF_NOT_EQUAL(ipmi_request_priority(r1), ipmi_request_priority(r2));
	TRX_RETURN_IF_NOT_EQUAL(r1->requestid, r2->requestid);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_request_create                                              *
 *                                                                            *
 * Purpose: creates an IPMI request                                           *
 *                                                                            *
 * Parameters: hostid - [IN] the target hostid                                *
 *                                                                            *
 ******************************************************************************/
static trx_ipmi_request_t	*ipmi_request_create(trx_uint64_t hostid)
{
	static trx_uint64_t	next_requestid = 1;
	trx_ipmi_request_t	*request;

	request = (trx_ipmi_request_t *)trx_malloc(NULL, sizeof(trx_ipmi_request_t));
	memset(request, 0, sizeof(trx_ipmi_request_t));
	request->requestid = next_requestid++;
	request->hostid = hostid;

	return request;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_request_free                                                *
 *                                                                            *
 * Purpose: frees IPMI request                                                *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_request_free(trx_ipmi_request_t *request)
{
	trx_ipc_message_clean(&request->message);
	trx_free(request);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_pop_request                                          *
 *                                                                            *
 * Purpose: pops the next queued request from IPMI poller request queue       *
 *                                                                            *
 * Parameters: poller - [IN] the IPMI poller                                  *
 *                                                                            *
 * Return value: The next request to process or NULL if the queue is empty.   *
 *                                                                            *
 ******************************************************************************/
static trx_ipmi_request_t	*ipmi_poller_pop_request(trx_ipmi_poller_t *poller)
{
	trx_binary_heap_elem_t	*el;
	trx_ipmi_request_t	*request;

	if (SUCCEED == trx_binary_heap_empty(&poller->requests))
		return NULL;

	el = trx_binary_heap_find_min(&poller->requests);
	request = (trx_ipmi_request_t *)el->data;
	trx_binary_heap_remove_min(&poller->requests);

	return request;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_push_request                                         *
 *                                                                            *
 * Purpose: pushes the requests into IPMI poller request queue                *
 *                                                                            *
 * Parameters: poller  - [IN] the IPMI poller                                 *
 *             request - [IN] the IPMI request to push                        *
 *                                                                            *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_poller_push_request(trx_ipmi_poller_t *poller, trx_ipmi_request_t *request)
{
	trx_binary_heap_elem_t	el = {0, (void *)request};

	trx_binary_heap_insert(&poller->requests, &el);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_send_request                                         *
 *                                                                            *
 * Purpose: sends request to IPMI poller                                      *
 *                                                                            *
 * Parameters: poller  - [IN] the IPMI poller                                 *
 *             message - [IN] the message to send                             *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_poller_send_request(trx_ipmi_poller_t *poller, trx_ipmi_request_t *request)
{
	if (FAIL == trx_ipc_client_send(poller->client, request->message.code, request->message.data,
			request->message.size))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot send data to IPMI poller");
		exit(EXIT_FAILURE);
	}

	poller->request = request;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_schedule_request                                     *
 *                                                                            *
 * Purpose: schedules request to IPMI poller                                  *
 *                                                                            *
 * Parameters: poller  - [IN] the IPMI poller                                 *
 *             request - [IN] the request to send                             *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_poller_schedule_request(trx_ipmi_poller_t *poller, trx_ipmi_request_t *request)
{
	if (NULL == poller->request && NULL != poller->client)
		ipmi_poller_send_request(poller, request);
	else
		ipmi_poller_push_request(poller, request);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_free_request                                         *
 *                                                                            *
 * Purpose: frees the current request processed by IPMI poller                *
 *                                                                            *
 * Parameters: poller  - [IN] the IPMI poller                                 *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_poller_free_request(trx_ipmi_poller_t *poller)
{
	ipmi_request_free(poller->request);
	poller->request = NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_free                                                 *
 *                                                                            *
 * Purpose: frees IPMI poller                                                 *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_poller_free(trx_ipmi_poller_t *poller)
{
	trx_ipmi_request_t	*request;

	trx_ipc_client_close(poller->client);

	while (NULL != (request = ipmi_poller_pop_request(poller)))
		ipmi_request_free(request);

	trx_binary_heap_destroy(&poller->requests);

	trx_free(poller);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_init                                                *
 *                                                                            *
 * Purpose: initializes IPMI manager                                          *
 *                                                                            *
 * Parameters: manager - [IN] the manager to initialize                       *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_init(trx_ipmi_manager_t *manager)
{
	int			i;
	trx_ipmi_poller_t	*poller;
	trx_binary_heap_elem_t	elem = {0};

	treegix_log(LOG_LEVEL_DEBUG, "In %s() pollers:%d", __func__, CONFIG_IPMIPOLLER_FORKS);

	trx_vector_ptr_create(&manager->pollers);
	trx_hashset_create(&manager->pollers_client, 0, poller_hash_func, poller_compare_func);
	trx_binary_heap_create(&manager->pollers_load, ipmi_poller_compare_load, 0);

	manager->next_poller_index = 0;

	for (i = 0; i < CONFIG_IPMIPOLLER_FORKS; i++)
	{
		poller = (trx_ipmi_poller_t *)trx_malloc(NULL, sizeof(trx_ipmi_poller_t));

		poller->client = NULL;
		poller->request = NULL;
		poller->hosts_num = 0;

		trx_binary_heap_create(&poller->requests, ipmi_request_compare, 0);

		trx_vector_ptr_append(&manager->pollers, poller);

		/* add poller to load balancing poller queue */
		elem.data = (const void *)poller;
		trx_binary_heap_insert(&manager->pollers_load, &elem);
	}

	trx_hashset_create(&manager->hosts, 0, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_destroy                                             *
 *                                                                            *
 * Purpose: destroys IPMI manager                                             *
 *                                                                            *
 * Parameters: manager - [IN] the manager to destroy                          *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_destroy(trx_ipmi_manager_t *manager)
{
	trx_hashset_destroy(&manager->hosts);
	trx_binary_heap_destroy(&manager->pollers_load);
	trx_hashset_destroy(&manager->pollers_client);
	trx_vector_ptr_clear_ext(&manager->pollers, (trx_clean_func_t)ipmi_poller_free);
	trx_vector_ptr_destroy(&manager->pollers);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_host_cleanup                                        *
 *                                                                            *
 * Purpose: performs cleanup of monitored hosts cache                         *
 *                                                                            *
 * Parameters: manager - [IN] the manager                                     *
 *             now     - [IN] the current time                                *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_host_cleanup(trx_ipmi_manager_t *manager, int now)
{
	trx_hashset_iter_t	iter;
	trx_ipmi_manager_host_t	*host;
	trx_ipmi_poller_t	*poller;
	int			i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() pollers:%d", __func__, CONFIG_IPMIPOLLER_FORKS);

	trx_hashset_iter_reset(&manager->hosts, &iter);
	while (NULL != (host = (trx_ipmi_manager_host_t *)trx_hashset_iter_next(&iter)))
	{
		if (host->lastcheck + TRX_IPMI_MANAGER_HOST_TTL <= now)
		{
			host->poller->hosts_num--;
			trx_hashset_iter_remove(&iter);
		}
	}

	for (i = 0; i < manager->pollers.values_num; i++)
	{
		poller = (trx_ipmi_poller_t *)manager->pollers.values[i];

		if (NULL != poller->client)
			trx_ipc_client_send(poller->client, TRX_IPC_IPMI_CLEANUP_REQUEST, NULL, 0);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_register_poller                                     *
 *                                                                            *
 * Purpose: registers IPMI poller                                             *
 *                                                                            *
 * Parameters: manager - [IN] the manager                                     *
 *             client  - [IN] the connected IPMI poller                       *
 *                                                                            *
 ******************************************************************************/
static trx_ipmi_poller_t	*ipmi_manager_register_poller(trx_ipmi_manager_t *manager, trx_ipc_client_t *client,
		trx_ipc_message_t *message)
{
	trx_ipmi_poller_t	*poller = NULL;
	pid_t			ppid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	memcpy(&ppid, message->data, sizeof(ppid));

	if (ppid != getppid())
	{
		trx_ipc_client_close(client);
		treegix_log(LOG_LEVEL_DEBUG, "refusing connection from foreign process");
	}
	else
	{
		if (manager->next_poller_index == manager->pollers.values_num)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		poller = (trx_ipmi_poller_t *)manager->pollers.values[manager->next_poller_index++];
		poller->client = client;

		trx_hashset_insert(&manager->pollers_client, &poller, sizeof(trx_ipmi_poller_t *));
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return poller;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_get_poller_by_client                                *
 *                                                                            *
 * Purpose: returns IPMI poller by connected client                           *
 *                                                                            *
 * Parameters: manager - [IN] the manager                                     *
 *             client  - [IN] the connected IPMI poller                       *
 *                                                                            *
 * Return value: The IPMI poller                                              *
 *                                                                            *
 ******************************************************************************/
static trx_ipmi_poller_t	*ipmi_manager_get_poller_by_client(trx_ipmi_manager_t *manager,
		trx_ipc_client_t *client)
{
	trx_ipmi_poller_t	**poller, poller_local, *plocal = &poller_local;

	plocal->client = client;

	poller = (trx_ipmi_poller_t **)trx_hashset_search(&manager->pollers_client, &plocal);

	if (NULL == poller)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	return *poller;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_get_host_poller                                     *
 *                                                                            *
 * Purpose: returns IPMI poller to be assigned to a new host                  *
 *                                                                            *
 * Parameters: manager - [IN] the manager                                     *
 *                                                                            *
 * Return value: The IPMI poller                                              *
 *                                                                            *
 * Comments: This function will return IPMI poller with least monitored hosts.*
 *                                                                            *
 ******************************************************************************/
static trx_ipmi_poller_t	*ipmi_manager_get_host_poller(trx_ipmi_manager_t *manager)
{
	trx_ipmi_poller_t	*poller;
	trx_binary_heap_elem_t	el;

	el = *trx_binary_heap_find_min(&manager->pollers_load);
	trx_binary_heap_remove_min(&manager->pollers_load);

	poller = (trx_ipmi_poller_t *)el.data;
	poller->hosts_num++;

	trx_binary_heap_insert(&manager->pollers_load, &el);

	return poller;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_process_poller_queue                                *
 *                                                                            *
 * Purpose: processes IPMI poller request queue                               *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             poller  - [IN] the IPMI poller                                 *
 *             now     - [IN] the current time                                *
 *                                                                            *
 * Comments: This function will send the next request in queue to the poller, *
 *           skipping requests for unreachable hosts for unreachable period.  *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_process_poller_queue(trx_ipmi_manager_t *manager, trx_ipmi_poller_t *poller, int now)
{
	trx_ipmi_request_t	*request;
	trx_ipmi_manager_host_t	*host;

	while (NULL != (request = ipmi_poller_pop_request(poller)))
	{
		switch (request->message.code)
		{
			case TRX_IPC_IPMI_COMMAND_REQUEST:
			case TRX_IPC_IPMI_CLEANUP_REQUEST:
				break;
			case TRX_IPC_IPMI_VALUE_REQUEST:
				if (NULL == (host = (trx_ipmi_manager_host_t *)trx_hashset_search(&manager->hosts, &request->hostid)))
				{
					THIS_SHOULD_NEVER_HAPPEN;
					ipmi_request_free(request);
					continue;
				}
				if (now < host->disable_until)
				{
					trx_dc_requeue_unreachable_items(&request->itemid, 1);
					ipmi_request_free(request);
					continue;
				}
				break;
		}

		ipmi_poller_send_request(poller, request);
		break;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_cache_host                                          *
 *                                                                            *
 * Purpose: caches host to keep local copy of its availability data           *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             hostid  - [IN] the host identifier                             *
 *             now     - [IN] the current time                                *
 *                                                                            *
 * Return value: The cached host.                                             *
 *                                                                            *
 ******************************************************************************/
static trx_ipmi_manager_host_t	*ipmi_manager_cache_host(trx_ipmi_manager_t *manager, trx_uint64_t hostid, int now)
{
	trx_ipmi_manager_host_t	*host;

	if (NULL == (host = (trx_ipmi_manager_host_t *)trx_hashset_search(&manager->hosts, &hostid)))
	{
		trx_ipmi_manager_host_t	host_local;

		host_local.hostid = hostid;
		host = (trx_ipmi_manager_host_t *)trx_hashset_insert(&manager->hosts, &host_local, sizeof(host_local));

		host->disable_until = 0;
		host->poller = ipmi_manager_get_host_poller(manager);
	}

	host->lastcheck = now;

	return host;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_update_host                                         *
 *                                                                            *
 * Purpose: updates cached host                                               *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             host    - [IN] the host                                        *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_update_host(trx_ipmi_manager_t *manager, const DC_HOST *host)
{
	trx_ipmi_manager_host_t	*ipmi_host;

	if (NULL == (ipmi_host = (trx_ipmi_manager_host_t *)trx_hashset_search(&manager->hosts, &host->hostid)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return;
	}

	ipmi_host->disable_until = host->ipmi_disable_until;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_activate_host                                       *
 *                                                                            *
 * Purpose: tries to activate item's host after receiving response            *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             itemid  - [IN] the item identifier                             *
 *             ts      - [IN] the activation timestamp                        *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_activate_host(trx_ipmi_manager_t *manager, trx_uint64_t itemid, trx_timespec_t *ts)
{
	DC_ITEM	item;
	int	errcode;

	DCconfig_get_items_by_itemids(&item, &itemid, &errcode, 1);

	trx_activate_item_host(&item, ts);
	ipmi_manager_update_host(manager, &item.host);

	DCconfig_clean_items(&item, &errcode, 1);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_deactivate_host                                     *
 *                                                                            *
 * Purpose: tries to deactivate item's host after receiving host level error  *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             itemid  - [IN] the item identifier                             *
 *             ts      - [IN] the deactivation timestamp                      *
 *             error   - [IN] the error                                       *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_deactivate_host(trx_ipmi_manager_t *manager, trx_uint64_t itemid, trx_timespec_t *ts,
		const char *error)
{
	DC_ITEM	item;
	int	errcode;

	DCconfig_get_items_by_itemids(&item, &itemid, &errcode, 1);

	trx_deactivate_item_host(&item, ts, error);
	ipmi_manager_update_host(manager, &item.host);

	DCconfig_clean_items(&item, &errcode, 1);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_process_value_result                                *
 *                                                                            *
 * Purpose: processes IPMI check result received from IPMI poller             *
 *                                                                            *
 * Parameters: manager   - [IN] the IPMI manager                              *
 *             client    - [IN] the client (IPMI poller)                      *
 *             message   - [IN] the received TRX_IPC_IPMI_VALUE_RESULT message*
 *             now       - [IN] the current time                              *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_process_value_result(trx_ipmi_manager_t *manager, trx_ipc_client_t *client,
		trx_ipc_message_t *message, int now)
{
	char			*value;
	trx_timespec_t		ts;
	unsigned char		state;
	int			errcode;
	AGENT_RESULT		result;
	trx_ipmi_poller_t	*poller;
	trx_uint64_t		itemid;

	if (NULL == (poller = ipmi_manager_get_poller_by_client(manager, client)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return;
	}
	itemid = poller->request->itemid;

	trx_ipmi_deserialize_result(message->data, &ts, &errcode, &value);

	/* update host availability */
	switch (errcode)
	{
		case SUCCEED:
		case NOTSUPPORTED:
		case AGENT_ERROR:
			ipmi_manager_activate_host(manager, itemid, &ts);
			break;
		case NETWORK_ERROR:
		case GATEWAY_ERROR:
		case TIMEOUT_ERROR:
			ipmi_manager_deactivate_host(manager, itemid, &ts, value);
			break;
		case CONFIG_ERROR:
			/* nothing to do */
			break;
	}

	/* add received data to history cache */
	switch (errcode)
	{
		case SUCCEED:
			state = ITEM_STATE_NORMAL;
			if (NULL != value)
			{
				init_result(&result);
				SET_TEXT_RESULT(&result, value);
				value = NULL;
				trx_preprocess_item_value(itemid, ITEM_VALUE_TYPE_TEXT, 0, &result, &ts, state, NULL);
				free_result(&result);
			}
			break;

		case NOTSUPPORTED:
		case AGENT_ERROR:
		case CONFIG_ERROR:
			state = ITEM_STATE_NOTSUPPORTED;
			trx_preprocess_item_value(itemid, ITEM_VALUE_TYPE_TEXT, 0, NULL, &ts, state, value);
			break;
		default:
			/* don't change item's state when network related error occurs */
			state = poller->request->item_state;
	}

	trx_free(value);

	/* put back the item in configuration cache IPMI poller queue */
	DCrequeue_items(&itemid, &state, &ts.sec, &errcode, 1);

	ipmi_poller_free_request(poller);
	ipmi_manager_process_poller_queue(manager, poller, now);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_serialize_request                                   *
 *                                                                            *
 * Purpose: serializes IPMI poll request (TRX_IPC_IPMI_VALUE_REQUEST)         *
 *                                                                            *
 * Parameters: item      - [IN] the item to poll                              *
 *             command   - [IN] the command to execute                        *
 *             message   - [OUT] the message                                  *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_serialize_request(const DC_ITEM *item, int command, trx_ipc_message_t *message)
{
	trx_uint32_t	size;

	size = trx_ipmi_serialize_request(&message->data, item->itemid, item->interface.addr,
			item->interface.port, item->host.ipmi_authtype, item->host.ipmi_privilege,
			item->host.ipmi_username, item->host.ipmi_password, item->ipmi_sensor, command);

	message->code = TRX_IPC_IPMI_VALUE_REQUEST;
	message->size = size;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_schedule_request                                    *
 *                                                                            *
 * Purpose: schedules request to the host                                     *
 *                                                                            *
 * Parameters: manager  - [IN] the IPMI manager                               *
 *             hostid   - [IN] the target host id                             *
 *             request  - [IN] the request to schedule                        *
 *             now      - [IN] the current timestamp                          *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_schedule_request(trx_ipmi_manager_t *manager, trx_uint64_t hostid,
		trx_ipmi_request_t *request, int now)
{
	trx_ipmi_manager_host_t	*host;

	host = ipmi_manager_cache_host(manager, hostid, now);
	ipmi_poller_schedule_request(host->poller, request);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_schedule_requests                                   *
 *                                                                            *
 * Purpose: either sends or queues IPMI poll requests from configuration      *
 *          cache IPMI poller queue                                           *
 *                                                                            *
 * Parameters: manager   - [IN] the IPMI manager                              *
 *             now       - [IN] current time                                  *
 *             nextcheck - [OUT] time when the next IPMI check is scheduled   *
 *                         in configuration cache IPMI poller queue           *
 *                                                                            *
 * Return value: The number of requests scheduled.                            *
 *                                                                            *
 ******************************************************************************/
static int	ipmi_manager_schedule_requests(trx_ipmi_manager_t *manager, int now, int *nextcheck)
{
	int			i, num;
	DC_ITEM			items[MAX_POLLER_ITEMS];
	trx_ipmi_request_t	*request;
	char			*error = NULL;

	num = DCconfig_get_ipmi_poller_items(now, items, MAX_POLLER_ITEMS, nextcheck);

	for (i = 0; i < num; i++)
	{
		if (FAIL == trx_ipmi_port_expand_macros(items[i].host.hostid, items[i].interface.port_orig,
				&items[i].interface.port, &error))
		{
			trx_timespec_t	ts;
			unsigned char	state = ITEM_STATE_NOTSUPPORTED;
			int		errcode = CONFIG_ERROR;

			trx_timespec(&ts);
			trx_preprocess_item_value(items[i].itemid, items[i].value_type, 0, NULL, &ts, state, error);
			DCrequeue_items(&items[i].itemid, &state, &ts.sec, &errcode, 1);
			trx_free(error);
			continue;
		}

		request = ipmi_request_create(items[i].host.hostid);
		request->itemid = items[i].itemid;
		request->item_state = items[i].state;
		ipmi_manager_serialize_request(&items[i], 0, &request->message);
		ipmi_manager_schedule_request(manager, items[i].host.hostid, request, now);
	}

	trx_preprocessor_flush();
	DCconfig_clean_items(items, NULL, num);

	return num;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_process_script_request                              *
 *                                                                            *
 * Purpose: forwards IPMI script request to the poller managing the specified *
 *          host                                                              *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             client  - [IN] the client asking to execute IPMI script        *
 *             message - [IN] the script request message                      *
 *             now     - [IN] the current time                                *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_process_script_request(trx_ipmi_manager_t *manager, trx_ipc_client_t *client,
		trx_ipc_message_t *message, int now)
{
	trx_ipmi_request_t	*request;
	trx_uint64_t		hostid;

	trx_ipmi_deserialize_request_objectid(message->data, &hostid);

	trx_ipc_client_addref(client);

	request = ipmi_request_create(0);
	request->client = client;
	trx_ipc_message_copy(&request->message, message);
	request->message.code = TRX_IPC_IPMI_COMMAND_REQUEST;

	ipmi_manager_schedule_request(manager, hostid, request, now);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_process_command_result                              *
 *                                                                            *
 * Purpose: forwards command result as script result to the client that       *
 *          requested IPMI script execution                                   *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             client  - [IN] the IPMI poller client                          *
 *             message - [IN] the command result message                      *
 *             now     - [IN] the current time                                *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_process_command_result(trx_ipmi_manager_t *manager, trx_ipc_client_t *client,
		trx_ipc_message_t *message, int now)
{
	trx_ipmi_poller_t	*poller;

	if (NULL == (poller = ipmi_manager_get_poller_by_client(manager, client)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return;
	}

	if (SUCCEED == trx_ipc_client_connected(poller->request->client))
	{
		trx_ipc_client_send(poller->request->client, TRX_IPC_IPMI_SCRIPT_RESULT, message->data, message->size);
		trx_ipc_client_release(poller->request->client);
	}

	ipmi_poller_free_request(poller);
	ipmi_manager_process_poller_queue(manager, poller, now);
}

TRX_THREAD_ENTRY(ipmi_manager_thread, args)
{
	trx_ipc_service_t	ipmi_service;
	char			*error = NULL;
	trx_ipc_client_t	*client;
	trx_ipc_message_t	*message;
	trx_ipmi_manager_t	ipmi_manager;
	trx_ipmi_poller_t	*poller;
	int			ret, nextcheck, timeout, nextcleanup, polled_num = 0, scheduled_num = 0, now;
	double			time_stat, time_idle = 0, time_now, sec;

#define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	trx_setproctitle("%s #%d starting", get_process_type_string(process_type), process_num);

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	if (FAIL == trx_ipc_service_start(&ipmi_service, TRX_IPC_SERVICE_IPMI, &error))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot start IPMI service: %s", error);
		trx_free(error);
		exit(EXIT_FAILURE);
	}

	ipmi_manager_init(&ipmi_manager);

	DBconnect(TRX_DB_CONNECT_NORMAL);

	nextcleanup = time(NULL) + TRX_IPMI_MANAGER_CLEANUP_DELAY;

	time_stat = trx_time();

	trx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);

	update_selfmon_counter(TRX_PROCESS_STATE_BUSY);

	while (TRX_IS_RUNNING())
	{
		time_now = trx_time();
		now = time_now;

		if (STAT_INTERVAL < time_now - time_stat)
		{
			trx_setproctitle("%s #%d [scheduled %d, polled %d values, idle " TRX_FS_DBL " sec during "
					TRX_FS_DBL " sec]", get_process_type_string(process_type), process_num,
					scheduled_num, polled_num, time_idle, time_now - time_stat);

			time_stat = time_now;
			time_idle = 0;
			polled_num = 0;
			scheduled_num = 0;
		}

		scheduled_num += ipmi_manager_schedule_requests(&ipmi_manager, now, &nextcheck);

		if (FAIL != nextcheck)
			timeout = (nextcheck > now ? nextcheck - now : 0);
		else
			timeout = TRX_IPMI_MANAGER_DELAY;

		if (TRX_IPMI_MANAGER_DELAY < timeout)
			timeout = TRX_IPMI_MANAGER_DELAY;

		update_selfmon_counter(TRX_PROCESS_STATE_IDLE);
		ret = trx_ipc_service_recv(&ipmi_service, timeout, &client, &message);
		update_selfmon_counter(TRX_PROCESS_STATE_BUSY);
		sec = trx_time();
		trx_update_env(sec);

		if (TRX_IPC_RECV_IMMEDIATE != ret)
			time_idle += sec - time_now;

		if (NULL != message)
		{
			switch (message->code)
			{
				case TRX_IPC_IPMI_REGISTER:
					if (NULL != (poller = ipmi_manager_register_poller(&ipmi_manager, client,
							message)))
					{
						ipmi_manager_process_poller_queue(&ipmi_manager, poller, now);
					}
					break;
				case TRX_IPC_IPMI_VALUE_RESULT:
					ipmi_manager_process_value_result(&ipmi_manager, client, message, now);
					polled_num++;
					break;
				case TRX_IPC_IPMI_SCRIPT_REQUEST:
					ipmi_manager_process_script_request(&ipmi_manager, client, message, now);
					break;
				case TRX_IPC_IPMI_COMMAND_RESULT:
					ipmi_manager_process_command_result(&ipmi_manager, client, message, now);
			}

			trx_ipc_message_free(message);
		}

		if (NULL != client)
			trx_ipc_client_release(client);

		if (now >= nextcleanup)
		{
			ipmi_manager_host_cleanup(&ipmi_manager, now);
			nextcleanup = now + TRX_IPMI_MANAGER_CLEANUP_DELAY;
		}
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);

	trx_ipc_service_close(&ipmi_service);
	ipmi_manager_destroy(&ipmi_manager);
#undef STAT_INTERVAL
}

#endif
