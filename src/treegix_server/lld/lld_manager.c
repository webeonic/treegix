

#include "common.h"
#include "daemon.h"

#include "trxself.h"
#include "log.h"
#include "trxipcservice.h"
#include "lld_manager.h"
#include "lld_protocol.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

extern int	CONFIG_LLDWORKER_FORKS;

/*
 * The LLD queue is organized as a queue (rule_queue binary heap) of LLD rules,
 * sorted by their oldest value timestamps. The values are stored in linked lists,
 * each rule having its own list of values. Values inside list are not sorted, so
 * in the case a LLD rule received a value with past timestamp, it will be processed
 * in queuing order, not the value chronological order.
 *
 * During processing the rule with oldest value is popped from queue and sent
 * to a free worker. After processing the rule worker sends done response and
 * manager removes the oldest value from rule's value list. If there are no more
 * values in the list the rule is removed from the index (rule_index hashset),
 * otherwise the rule is enqueued back in LLD queue.
 *
 */

typedef struct trx_lld_value
{
	char			*value;
	char			*error;
	trx_timespec_t		ts;

	trx_uint64_t		lastlogsize;
	int			mtime;
	unsigned char		meta;

	struct	trx_lld_value	*next;
}
trx_lld_data_t;

/* queue of values for one LLD rule */
typedef struct
{
	/* the LLD rule id */
	trx_uint64_t	itemid;

	/* the oldest value in queue */
	trx_lld_data_t	*tail;

	/* the newest value in queue */
	trx_lld_data_t	*head;
}
trx_lld_rule_t;

typedef struct
{
	/* workers vector, created during manager initialization */
	trx_vector_ptr_t	workers;

	/* free workers */
	trx_queue_ptr_t		free_workers;

	/* workers indexed by IPC service clients */
	trx_hashset_t		workers_client;

	/* the next worker index to be assigned to new IPC service clients */
	int			next_worker_index;

	/* index of queued LLD rules */
	trx_hashset_t		rule_index;

	/* LLD rule queue, ordered by the oldest values */
	trx_binary_heap_t	rule_queue;

	/* the number of queued LLD rules */
	trx_uint64_t		queued_num;

}
trx_lld_manager_t;

typedef struct
{
	trx_ipc_client_t	*client;
	trx_lld_rule_t		*rule;
}
trx_lld_worker_t;

/* workers_client hashset support */
static trx_hash_t	worker_hash_func(const void *d)
{
	const trx_lld_worker_t	*worker = *(const trx_lld_worker_t **)d;

	trx_hash_t hash =  TRX_DEFAULT_PTR_HASH_FUNC(&worker->client);

	return hash;
}

static int	worker_compare_func(const void *d1, const void *d2)
{
	const trx_lld_worker_t	*p1 = *(const trx_lld_worker_t **)d1;
	const trx_lld_worker_t	*p2 = *(const trx_lld_worker_t **)d2;

	TRX_RETURN_IF_NOT_EQUAL(p1->client, p2->client);
	return 0;
}

/* rule_queue binary heap support */
static int	rule_elem_compare_func(const void *d1, const void *d2)
{
	const trx_binary_heap_elem_t	*e1 = (const trx_binary_heap_elem_t *)d1;
	const trx_binary_heap_elem_t	*e2 = (const trx_binary_heap_elem_t *)d2;

	const trx_lld_rule_t	*rule1 = (const trx_lld_rule_t *)e1->data;
	const trx_lld_rule_t	*rule2 = (const trx_lld_rule_t *)e2->data;

	/* compare by timestamp of the oldest value */
	return trx_timespec_compare(&rule1->head->ts, &rule2->head->ts);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_data_free                                                    *
 *                                                                            *
 * Purpose: frees LLD data                                                    *
 *                                                                            *
 ******************************************************************************/
static void	lld_data_free(trx_lld_data_t *data)
{
	trx_free(data->value);
	trx_free(data->error);
	trx_free(data);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_rule_clear                                                   *
 *                                                                            *
 * Purpose: clears LLD rule                                                   *
 *                                                                            *
 ******************************************************************************/
static void	lld_rule_clear(trx_lld_rule_t *rule)
{
	trx_lld_data_t	*data;

	while (NULL != rule->head)
	{
		data = rule->head;
		rule->head = data->next;
		lld_data_free(data);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_worker_free                                                  *
 *                                                                            *
 * Purpose: frees LLD worker                                                  *
 *                                                                            *
 ******************************************************************************/
static void	lld_worker_free(trx_lld_worker_t *worker)
{
	trx_free(worker);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_manager_init                                                 *
 *                                                                            *
 * Purpose: initializes LLD manager                                           *
 *                                                                            *
 * Parameters: manager - [IN] the manager to initialize                       *
 *                                                                            *
 ******************************************************************************/
static void	lld_manager_init(trx_lld_manager_t *manager)
{
	int			i;
	trx_lld_worker_t	*worker;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() workers:%d", __func__, CONFIG_LLDWORKER_FORKS);

	trx_vector_ptr_create(&manager->workers);
	trx_queue_ptr_create(&manager->free_workers);
	trx_hashset_create(&manager->workers_client, 0, worker_hash_func, worker_compare_func);

	trx_hashset_create_ext(&manager->rule_index, 0, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC,
			(trx_clean_func_t)lld_rule_clear,
			TRX_DEFAULT_MEM_MALLOC_FUNC, TRX_DEFAULT_MEM_REALLOC_FUNC, TRX_DEFAULT_MEM_FREE_FUNC);

	trx_binary_heap_create(&manager->rule_queue, rule_elem_compare_func, TRX_BINARY_HEAP_OPTION_EMPTY);

	manager->next_worker_index = 0;

	for (i = 0; i < CONFIG_LLDWORKER_FORKS; i++)
	{
		worker = (trx_lld_worker_t *)trx_malloc(NULL, sizeof(trx_lld_worker_t));

		worker->client = NULL;

		trx_vector_ptr_append(&manager->workers, worker);
	}

	manager->queued_num = 0;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_manager_destroy                                              *
 *                                                                            *
 * Purpose: destroys LLD manager                                              *
 *                                                                            *
 * Parameters: manager - [IN] the manager to destroy                          *
 *                                                                            *
 ******************************************************************************/
static void	lld_manager_destroy(trx_lld_manager_t *manager)
{
	trx_binary_heap_destroy(&manager->rule_queue);
	trx_hashset_destroy(&manager->rule_index);
	trx_queue_ptr_destroy(&manager->free_workers);
	trx_hashset_destroy(&manager->workers_client);
	trx_vector_ptr_clear_ext(&manager->workers, (trx_clean_func_t)lld_worker_free);
	trx_vector_ptr_destroy(&manager->workers);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_get_worker_by_client                                         *
 *                                                                            *
 * Purpose: returns worker by connected IPC client data                       *
 *                                                                            *
 * Parameters: manager - [IN] the manager                                     *
 *             client  - [IN] the connected worker                            *
 *                                                                            *
 * Return value: The LLD worker                                               *
 *                                                                            *
 ******************************************************************************/
static trx_lld_worker_t	*lld_get_worker_by_client(trx_lld_manager_t *manager, trx_ipc_client_t *client)
{
	trx_lld_worker_t	**worker, worker_local, *plocal = &worker_local;

	plocal->client = client;
	worker = (trx_lld_worker_t **)trx_hashset_search(&manager->workers_client, &plocal);

	if (NULL == worker)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	return *worker;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_register_worker                                              *
 *                                                                            *
 * Purpose: registers worker                                                  *
 *                                                                            *
 * Parameters: manager - [IN] the manager                                     *
 *             client  - [IN] the connected worker IPC client data            *
 *             message - [IN] the received message                            *
 *                                                                            *
 ******************************************************************************/
static void	lld_register_worker(trx_lld_manager_t *manager, trx_ipc_client_t *client,
		const trx_ipc_message_t *message)
{
	trx_lld_worker_t	*worker;
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
		if (manager->next_worker_index == manager->workers.values_num)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		worker = (trx_lld_worker_t *)manager->workers.values[manager->next_worker_index++];
		worker->client = client;

		trx_hashset_insert(&manager->workers_client, &worker, sizeof(trx_lld_worker_t *));
		trx_queue_ptr_push(&manager->free_workers, worker);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_queue_rule                                                   *
 *                                                                            *
 * Purpose: queues LLD rule                                                   *
 *                                                                            *
 * Parameters: manager - [IN] the LLD manager                                 *
 *             rule    - [IN] the LLD rule                                    *
 *                                                                            *
 ******************************************************************************/
static void	lld_queue_rule(trx_lld_manager_t *manager, trx_lld_rule_t *rule)
{
	trx_binary_heap_elem_t	elem = {rule->itemid, rule};

	trx_binary_heap_insert(&manager->rule_queue, &elem);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_queue_request                                                *
 *                                                                            *
 * Purpose: queues low level discovery request                                *
 *                                                                            *
 * Parameters: manager - [IN] the LLD manager                                 *
 *             message - [IN] the message with LLD request                    *
 *                                                                            *
 ******************************************************************************/
static void	lld_queue_request(trx_lld_manager_t *manager, const trx_ipc_message_t *message)
{
	trx_uint64_t	itemid;
	trx_lld_rule_t	*rule;
	trx_lld_data_t	*data;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	data = (trx_lld_data_t *)trx_malloc(NULL, sizeof(trx_lld_data_t));
	data->next = NULL;
	trx_lld_deserialize_item_value(message->data, &itemid, &data->value, &data->ts, &data->meta, &data->lastlogsize,
			&data->mtime, &data->error);

	treegix_log(LOG_LEVEL_DEBUG, "queuing discovery rule:" TRX_FS_UI64, itemid);

	if (NULL == (rule = trx_hashset_search(&manager->rule_index, &itemid)))
	{
		trx_lld_rule_t	rule_local = {itemid, data, data};

		rule = trx_hashset_insert(&manager->rule_index, &rule_local, sizeof(rule_local));
		lld_queue_rule(manager, rule);
	}
	else
	{
		rule->tail->next = data;
		rule->tail = data;
	}

	manager->queued_num++;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_process_next_request                                         *
 *                                                                            *
 * Purpose: processes next LLD request from queue                             *
 *                                                                            *
 * Parameters: manager - [IN] the LLD manager                                 *
 *             worker  - [IN] the target worker                               *
 *                                                                            *
 ******************************************************************************/
static void	lld_process_next_request(trx_lld_manager_t *manager, trx_lld_worker_t *worker)
{
	trx_binary_heap_elem_t	*elem;
	unsigned char		*buf;
	trx_uint32_t		buf_len;
	trx_lld_data_t		*data;

	elem = trx_binary_heap_find_min(&manager->rule_queue);
	worker->rule = (trx_lld_rule_t *)elem->data;
	trx_binary_heap_remove_min(&manager->rule_queue);

	data = worker->rule->head;
	buf_len = trx_lld_serialize_item_value(&buf, worker->rule->itemid, data->value, &data->ts, data->meta,
			data->lastlogsize, data->mtime, data->error);
	trx_ipc_client_send(worker->client, TRX_IPC_LLD_TASK, buf, buf_len);
	trx_free(buf);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_process_queue                                                *
 *                                                                            *
 * Purpose: sends queued LLD rules to free workers                            *
 *                                                                            *
 * Parameters: manager - [IN] the LLD manager                                 *
 *                                                                            *
 ******************************************************************************/
static void	lld_process_queue(trx_lld_manager_t *manager)
{
	trx_lld_worker_t	*worker;

	while (SUCCEED != trx_binary_heap_empty(&manager->rule_queue))
	{
		if (NULL == (worker = trx_queue_ptr_pop(&manager->free_workers)))
			break;

		lld_process_next_request(manager, worker);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_process_result                                               *
 *                                                                            *
 * Purpose: processes LLD worker 'done' response                              *
 *                                                                            *
 * Parameters: manager - [IN] the LLD manager                                 *
 * Parameters: client  - [IN] the worker's IPC client connection              *
 *                                                                            *
 ******************************************************************************/
static void	lld_process_result(trx_lld_manager_t *manager, trx_ipc_client_t *client)
{
	trx_lld_worker_t	*worker;
	trx_lld_rule_t		*rule;
	trx_lld_data_t		*data;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	worker = lld_get_worker_by_client(manager, client);

	treegix_log(LOG_LEVEL_DEBUG, "discovery rule:" TRX_FS_UI64 " has been processed", worker->rule->itemid);

	rule = worker->rule;
	worker->rule = NULL;

	data = rule->head;
	rule->head = rule->head->next;

	if (NULL == rule->head)
		trx_hashset_remove_direct(&manager->rule_index, rule);
	else
		lld_queue_rule(manager, rule);

	lld_data_free(data);

	if (SUCCEED != trx_binary_heap_empty(&manager->rule_queue))
		lld_process_next_request(manager, worker);
	else
		trx_queue_ptr_push(&manager->free_workers, worker);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_manager_thread                                               *
 *                                                                            *
 * Purpose: main processing loop                                              *
 *                                                                            *
 ******************************************************************************/
TRX_THREAD_ENTRY(lld_manager_thread, args)
{
#define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	trx_ipc_service_t	lld_service;
	char			*error = NULL;
	trx_ipc_client_t	*client;
	trx_ipc_message_t	*message;
	double			time_stat, time_now, sec;
	trx_lld_manager_t	manager;
	trx_uint64_t		processed_num = 0;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	trx_setproctitle("%s #%d starting", get_process_type_string(process_type), process_num);

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	if (FAIL == trx_ipc_service_start(&lld_service, TRX_IPC_SERVICE_LLD, &error))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot start LLD manager service: %s", error);
		trx_free(error);
		exit(EXIT_FAILURE);
	}

	lld_manager_init(&manager);

	/* initialize statistics */
	time_stat = trx_time();

	trx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);

	update_selfmon_counter(TRX_PROCESS_STATE_BUSY);

	while (TRX_IS_RUNNING())
	{
		time_now = trx_time();

		if (STAT_INTERVAL < time_now - time_stat)
		{
			trx_setproctitle("%s #%d [processed " TRX_FS_UI64 " LLD rules during " TRX_FS_DBL " sec]",
					get_process_type_string(process_type), process_num, processed_num,
					time_now - time_stat);

			time_stat = time_now;
			processed_num = 0;
		}

		update_selfmon_counter(TRX_PROCESS_STATE_IDLE);
		trx_ipc_service_recv(&lld_service, 1, &client, &message);
		update_selfmon_counter(TRX_PROCESS_STATE_BUSY);

		sec = trx_time();
		trx_update_env(sec);

		if (NULL != message)
		{
			switch (message->code)
			{
				case TRX_IPC_LLD_REGISTER:
					lld_register_worker(&manager, client, message);
					break;
				case TRX_IPC_LLD_REQUEST:
					lld_queue_request(&manager, message);
					lld_process_queue(&manager);
					break;
				case TRX_IPC_LLD_DONE:
					lld_process_result(&manager, client);
					processed_num++;
					manager.queued_num--;
					break;
				case TRX_IPC_LLD_QUEUE:
					trx_ipc_client_send(client, message->code, (unsigned char *)&manager.queued_num,
							sizeof(trx_uint64_t));
					break;
			}

			trx_ipc_message_free(message);
		}

		if (NULL != client)
			trx_ipc_client_release(client);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);

	trx_ipc_service_close(&lld_service);
	lld_manager_destroy(&manager);
}
