

#include "common.h"

#include "dbcache.h"
#include "daemon.h"
#include "trxself.h"
#include "log.h"
#include "trxserver.h"
#include "sysinfo.h"
#include "trxserialize.h"
#include "trxipcservice.h"
#include "trxlld.h"

#include "preprocessing.h"
#include "preproc_manager.h"
#include "linked_list.h"
#include "preproc_history.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num, CONFIG_PREPROCESSOR_FORKS;

#define TRX_PREPROCESSING_MANAGER_DELAY	1

#define TRX_PREPROC_PRIORITY_NONE	0
#define TRX_PREPROC_PRIORITY_FIRST	1

typedef enum
{
	REQUEST_STATE_QUEUED		= 0,		/* requires preprocessing */
	REQUEST_STATE_PROCESSING	= 1,		/* is being preprocessed  */
	REQUEST_STATE_DONE		= 2,		/* value is set, waiting for flush */
	REQUEST_STATE_PENDING		= 3		/* value requires preprocessing, */
							/* but is waiting on other request to complete */
}
trx_preprocessing_states_t;

/* preprocessing request */
typedef struct preprocessing_request
{
	trx_preprocessing_states_t	state;		/* request state */
	struct preprocessing_request	*pending;	/* the request waiting on this request to complete */
	trx_preproc_item_value_t	value;		/* unpacked item value */
	trx_preproc_op_t		*steps;		/* preprocessing steps */
	int				steps_num;	/* number of preprocessing steps */
	unsigned char			value_type;	/* value type from configuration */
							/* at the beginning of preprocessing queue */
}
trx_preprocessing_request_t;

/* preprocessing worker data */
typedef struct
{
	trx_ipc_client_t	*client;	/* the connected preprocessing worker client */
	void			*task;		/* the current task data */
}
trx_preprocessing_worker_t;

/* item link index */
typedef struct
{
	trx_uint64_t		itemid;		/* item id */
	trx_list_item_t		*queue_item;	/* queued item */
}
trx_item_link_t;

/* direct request to be forwarded to worker, bypassing the preprocessing queue */
typedef struct
{
	trx_ipc_client_t	*client;	/* the IPC client sending forward message to worker */
	trx_ipc_message_t	message;
}
trx_preprocessing_direct_request_t;

/* preprocessing manager data */
typedef struct
{
	trx_preprocessing_worker_t	*workers;	/* preprocessing worker array */
	int				worker_count;	/* preprocessing worker count */
	trx_list_t			queue;		/* queue of item values */
	trx_hashset_t			item_config;	/* item configuration L2 cache */
	trx_hashset_t			history_cache;	/* item value history cache */
	trx_hashset_t			linked_items;	/* linked items placed in queue */
	int				cache_ts;	/* cache timestamp */
	trx_uint64_t			processed_num;	/* processed value counter */
	trx_uint64_t			queued_num;	/* queued value counter */
	trx_uint64_t			preproc_num;	/* queued values with preprocessing steps */
	trx_list_iterator_t		priority_tail;	/* iterator to the last queued priority item */

	trx_list_t			direct_queue;	/* Queue of external requests that have to be */
							/* forwarded to workers for preprocessing.    */
}
trx_preprocessing_manager_t;

static void	preprocessor_enqueue_dependent(trx_preprocessing_manager_t *manager,
		trx_preproc_item_value_t *value, trx_list_item_t *master);

/* cleanup functions */

static void	preproc_item_clear(trx_preproc_item_t *item)
{
	int	i;

	trx_free(item->dep_itemids);

	for (i = 0; i < item->preproc_ops_num; i++)
	{
		trx_free(item->preproc_ops[i].params);
		trx_free(item->preproc_ops[i].error_handler_params);
	}

	trx_free(item->preproc_ops);
}

static void	request_free_steps(trx_preprocessing_request_t *request)
{
	while (0 < request->steps_num--)
	{
		trx_free(request->steps[request->steps_num].params);
		trx_free(request->steps[request->steps_num].error_handler_params);
	}

	trx_free(request->steps);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_sync_configuration                                  *
 *                                                                            *
 * Purpose: synchronize preprocessing manager with configuration cache data   *
 *                                                                            *
 * Parameters: manager - [IN] the manager to be synchronized                  *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_sync_configuration(trx_preprocessing_manager_t *manager)
{
	trx_hashset_iter_t	iter;
	int			ts;
	trx_preproc_history_t	*vault;
	trx_preproc_item_t	*item;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ts = manager->cache_ts;
	DCconfig_get_preprocessable_items(&manager->item_config, &manager->cache_ts);

	if (ts != manager->cache_ts)
	{
		/* drop items with removed preprocessing steps from preprocessing history cache */
		trx_hashset_iter_reset(&manager->history_cache, &iter);
		while (NULL != (vault = (trx_preproc_history_t *)trx_hashset_iter_next(&iter)))
		{
			if (NULL != trx_hashset_search(&manager->item_config, &vault->itemid))
				continue;

			trx_vector_ptr_clear_ext(&vault->history, (trx_clean_func_t)trx_preproc_op_history_free);
			trx_vector_ptr_destroy(&vault->history);
			trx_hashset_iter_remove(&iter);
		}

		/* reset preprocessing history for an item if its preprocessing step was modified */
		trx_hashset_iter_reset(&manager->item_config, &iter);
		while (NULL != (item = (trx_preproc_item_t *)trx_hashset_iter_next(&iter)))
		{
			if (ts >= item->update_time)
				continue;

			if (NULL == (vault = (trx_preproc_history_t *)trx_hashset_search(&manager->history_cache,
					&item->itemid)))
			{
				continue;
			}

			trx_vector_ptr_clear_ext(&vault->history, (trx_clean_func_t)trx_preproc_op_history_free);
			trx_vector_ptr_destroy(&vault->history);
			trx_hashset_remove_direct(&manager->history_cache, vault);
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() item config size: %d, history cache size: %d", __func__,
			manager->item_config.num_data, manager->history_cache.num_data);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_create_task                                         *
 *                                                                            *
 * Purpose: create preprocessing task for request                             *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *             request - [IN] preprocessing request                           *
 *             task    - [OUT] preprocessing task data                        *
 *                                                                            *
 ******************************************************************************/
static trx_uint32_t	preprocessor_create_task(trx_preprocessing_manager_t *manager,
		trx_preprocessing_request_t *request, unsigned char **task)
{
	trx_variant_t		value;
	trx_preproc_history_t	*vault;
	trx_vector_ptr_t	*phistory;

	if (ISSET_LOG(request->value.result))
		trx_variant_set_str(&value, request->value.result->log->value);
	else if (ISSET_UI64(request->value.result))
		trx_variant_set_ui64(&value, request->value.result->ui64);
	else if (ISSET_DBL(request->value.result))
		trx_variant_set_dbl(&value, request->value.result->dbl);
	else if (ISSET_STR(request->value.result))
		trx_variant_set_str(&value, request->value.result->str);
	else if (ISSET_TEXT(request->value.result))
		trx_variant_set_str(&value, request->value.result->text);
	else
		THIS_SHOULD_NEVER_HAPPEN;

	if (NULL != (vault = (trx_preproc_history_t *)trx_hashset_search(&manager->history_cache,
				&request->value.itemid)))
	{
		phistory = &vault->history;
	}
	else
		phistory = NULL;


	return trx_preprocessor_pack_task(task, request->value.itemid, request->value_type, request->value.ts, &value,
			phistory, request->steps, request->steps_num);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_set_request_state_done                              *
 *                                                                            *
 * Purpose: set request state to done and handle linked items                 *
 *                                                                            *
 * Parameters: manager    - [IN] preprocessing manager                        *
 *             request    - [IN] preprocessing request                        *
 *             queue_item - [IN] queued item                                  *
 *                                                                            *
 ******************************************************************************/
static	void	preprocessor_set_request_state_done(trx_preprocessing_manager_t *manager,
		trx_preprocessing_request_t *request, const trx_list_item_t *queue_item)
{
	trx_item_link_t	*index;

	request->state = REQUEST_STATE_DONE;

	/* value processed - the pending value can now be processed */
	if (NULL != request->pending)
		request->pending->state = REQUEST_STATE_QUEUED;

	if (NULL != (index = (trx_item_link_t *)trx_hashset_search(&manager->linked_items, &request->value.itemid)) &&
			queue_item == index->queue_item)
	{
		trx_hashset_remove_direct(&manager->linked_items, index);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_get_next_task                                       *
 *                                                                            *
 * Purpose: gets next task to be sent to worker                               *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *             message - [OUT] the serialized task to be sent                 *
 *                                                                            *
 * Return value: pointer to the task object                                   *
 *                                                                            *
 ******************************************************************************/
static void	*preprocessor_get_next_task(trx_preprocessing_manager_t *manager, trx_ipc_message_t *message)
{
	trx_list_iterator_t			iterator;
	trx_preprocessing_request_t		*request = NULL;
	void					*task = NULL;
	trx_preprocessing_direct_request_t	*direct_request;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED == trx_list_pop(&manager->direct_queue, (void **)&direct_request))
	{
		*message = direct_request->message;
		trx_ipc_message_init(&direct_request->message);
		task = direct_request;
		goto out;
	}

	trx_list_iterator_init(&manager->queue, &iterator);
	while (SUCCEED == trx_list_iterator_next(&iterator))
	{
		trx_list_iterator_peek(&iterator, (void **)&request);

		if (REQUEST_STATE_QUEUED != request->state)
			continue;

		if (ITEM_STATE_NOTSUPPORTED == request->value.state)
		{
			trx_preproc_history_t	*vault;

			if (NULL != (vault = (trx_preproc_history_t *) trx_hashset_search(&manager->history_cache,
					&request->value.itemid)))
			{
				trx_vector_ptr_clear_ext(&vault->history,
						(trx_clean_func_t) trx_preproc_op_history_free);
				trx_vector_ptr_destroy(&vault->history);
				trx_hashset_remove_direct(&manager->history_cache, vault);
			}

			preprocessor_set_request_state_done(manager, request, iterator.current);
			continue;
		}

		task = iterator.current;
		request->state = REQUEST_STATE_PROCESSING;
		message->code = TRX_IPC_PREPROCESSOR_REQUEST;
		message->size = preprocessor_create_task(manager, request, &message->data);
		request_free_steps(request);
		break;
	}
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return task;
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_get_worker_by_client                                *
 *                                                                            *
 * Purpose: get worker data by IPC client                                     *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *             client  - [IN] IPC client                                      *
 *                                                                            *
 * Return value: pointer to the worker data                                   *
 *                                                                            *
 ******************************************************************************/
static trx_preprocessing_worker_t	*preprocessor_get_worker_by_client(trx_preprocessing_manager_t *manager,
		trx_ipc_client_t *client)
{
	int				i;
	trx_preprocessing_worker_t	*worker = NULL;

	for (i = 0; i < manager->worker_count; i++)
	{
		if (client == manager->workers[i].client)
		{
			worker = &manager->workers[i];
			break;
		}
	}

	if (NULL == worker)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	return worker;
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_get_free_worker                                     *
 *                                                                            *
 * Purpose: get worker without active preprocessing task                      *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *                                                                            *
 * Return value: pointer to the worker data or NULL if none                   *
 *                                                                            *
 ******************************************************************************/
static trx_preprocessing_worker_t	*preprocessor_get_free_worker(trx_preprocessing_manager_t *manager)
{
	int	i;

	for (i = 0; i < manager->worker_count; i++)
	{
		if (NULL == manager->workers[i].task)
			return &manager->workers[i];
	}

	return NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_assign_tasks                                        *
 *                                                                            *
 * Purpose: assign available queued preprocessing tasks to free workers       *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_assign_tasks(trx_preprocessing_manager_t *manager)
{
	trx_preprocessing_worker_t	*worker;
	void				*data;
	trx_ipc_message_t		message;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (NULL != (worker = preprocessor_get_free_worker(manager)) &&
			NULL != (data = preprocessor_get_next_task(manager, &message)))
	{
		if (FAIL == trx_ipc_client_send(worker->client, message.code, message.data, message.size))
		{
			treegix_log(LOG_LEVEL_CRIT, "cannot send data to preprocessing worker");
			exit(EXIT_FAILURE);
		}

		worker->task = data;
		trx_ipc_message_clean(&message);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: preproc_item_value_clear                                         *
 *                                                                            *
 * Purpose: frees resources allocated by preprocessor item value              *
 *                                                                            *
 * Parameters: value - [IN] value to be freed                                 *
 *                                                                            *
 ******************************************************************************/
static void	preproc_item_value_clear(trx_preproc_item_value_t *value)
{
	trx_free(value->error);
	if (NULL != value->result)
	{
		free_result(value->result);
		trx_free(value->result);
	}
	trx_free(value->ts);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_free_request                                        *
 *                                                                            *
 * Purpose: free preprocessing request                                        *
 *                                                                            *
 * Parameters: request - [IN] request data to be freed                        *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_free_request(trx_preprocessing_request_t *request)
{
	preproc_item_value_clear(&request->value);
	request_free_steps(request);
	trx_free(request);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_free_direct_request                                 *
 *                                                                            *
 * Purpose: free preprocessing direct request                                 *
 *                                                                            *
 * Parameters: forward - [IN] forward data to be freed                        *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_free_direct_request(trx_preprocessing_direct_request_t *direct_request)
{
	trx_ipc_client_release(direct_request->client);
	trx_ipc_message_clean(&direct_request->message);
	trx_free(direct_request);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_flush_value                                         *
 *                                                                            *
 * Purpose: add new value to the local history cache or send to LLD manager   *
 *                                                                            *
 * Parameters: value - [IN] value to be added or sent                         *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_flush_value(const trx_preproc_item_value_t *value)
{
	if (0 == (value->item_flags & TRX_FLAG_DISCOVERY_RULE) || 0 == (program_type & TRX_PROGRAM_TYPE_SERVER))
	{
		dc_add_history(value->itemid, value->item_value_type, value->item_flags, value->result, value->ts,
				value->state, value->error);
	}
	else
		trx_lld_process_agent_result(value->itemid, value->result, value->ts, value->error);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessing_flush_queue                                        *
 *                                                                            *
 * Purpose: add all sequential processed values from beginning of the queue   *
 *          to the local history cache                                        *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *                                                                            *
 ******************************************************************************/
static void	preprocessing_flush_queue(trx_preprocessing_manager_t *manager)
{
	trx_preprocessing_request_t	*request;
	trx_list_iterator_t		iterator;

	trx_list_iterator_init(&manager->queue, &iterator);
	while (SUCCEED == trx_list_iterator_next(&iterator))
	{
		trx_list_iterator_peek(&iterator, (void **)&request);

		if (REQUEST_STATE_DONE != request->state)
			break;

		preprocessor_flush_value(&request->value);
		preprocessor_free_request(request);

		if (SUCCEED == trx_list_iterator_equal(&iterator, &manager->priority_tail))
			trx_list_iterator_clear(&manager->priority_tail);

		trx_list_pop(&manager->queue, NULL);

		manager->processed_num++;
		manager->queued_num--;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_link_items                                          *
 *                                                                            *
 * Purpose: create relation between item values within value queue            *
 *                                                                            *
 * Parameters: manager     - [IN] preprocessing manager                       *
 *             enqueued_at - [IN] position in value queue                     *
 *             item        - [IN] item configuration data                     *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_link_items(trx_preprocessing_manager_t *manager, trx_list_item_t *enqueued_at,
		trx_preproc_item_t *item)
{
	int				i;
	trx_preprocessing_request_t	*request, *dep_request;
	trx_item_link_t			*index, index_local;
	trx_preproc_op_t		*op;

	for (i = 0; i < item->preproc_ops_num; i++)
	{
		op = &item->preproc_ops[i];

		if (TRX_PREPROC_DELTA_VALUE == op->type || TRX_PREPROC_DELTA_SPEED == op->type)
			break;

		if (TRX_PREPROC_THROTTLE_VALUE == op->type || TRX_PREPROC_THROTTLE_TIMED_VALUE == op->type)
			break;
	}

	if (i != item->preproc_ops_num)
	{
		/* existing linked item*/
		if (NULL != (index = (trx_item_link_t *)trx_hashset_search(&manager->linked_items, &item->itemid)))
		{
			dep_request = (trx_preprocessing_request_t *)(enqueued_at->data);
			request = (trx_preprocessing_request_t *)(index->queue_item->data);

			if (REQUEST_STATE_DONE != request->state)
			{
				request->pending = dep_request;
				dep_request->state = REQUEST_STATE_PENDING;
			}

			index->queue_item = enqueued_at;
		}
		else
		{
			index_local.itemid = item->itemid;
			index_local.queue_item = enqueued_at;

			trx_hashset_insert(&manager->linked_items, &index_local, sizeof(trx_item_link_t));
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_copy_value                                          *
 *                                                                            *
 * Purpose: create a copy of existing item value                              *
 *                                                                            *
 * Parameters: target  - [OUT] created copy                                   *
 *             source  - [IN]  value to be copied                             *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_copy_value(trx_preproc_item_value_t *target, trx_preproc_item_value_t *source)
{
	memcpy(target, source, sizeof(trx_preproc_item_value_t));

	if (NULL != source->error)
		target->error = trx_strdup(NULL, source->error);

	if (NULL != source->ts)
	{
		target->ts = (trx_timespec_t *)trx_malloc(NULL, sizeof(trx_timespec_t));
		memcpy(target->ts, source->ts, sizeof(trx_timespec_t));
	}

	if (NULL != source->result)
	{
		target->result = (AGENT_RESULT *)trx_malloc(NULL, sizeof(AGENT_RESULT));
		memcpy(target->result, source->result, sizeof(AGENT_RESULT));

		if (NULL != source->result->str)
			target->result->str = trx_strdup(NULL, source->result->str);

		if (NULL != source->result->text)
			target->result->text = trx_strdup(NULL, source->result->text);

		if (NULL != source->result->msg)
			target->result->msg = trx_strdup(NULL, source->result->msg);

		if (NULL != source->result->log)
		{
			target->result->log = (trx_log_t *)trx_malloc(NULL, sizeof(trx_log_t));
			memcpy(target->result->log, source->result->log, sizeof(trx_log_t));

			if (NULL != source->result->log->value)
				target->result->log->value = trx_strdup(NULL, source->result->log->value);

			if (NULL != source->result->log->source)
				target->result->log->source = trx_strdup(NULL, source->result->log->source);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_enqueue                                             *
 *                                                                            *
 * Purpose: enqueue preprocessing request                                     *
 *                                                                            *
 * Parameters: manage   - [IN] preprocessing manager                          *
 *             value    - [IN] item value                                     *
 *             master   - [IN] request should be enqueued after this item     *
 *                             (NULL for the end of the queue)                *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_enqueue(trx_preprocessing_manager_t *manager, trx_preproc_item_value_t *value,
		trx_list_item_t *master)
{
	trx_preprocessing_request_t	*request;
	trx_preproc_item_t		*item, item_local;
	trx_list_item_t			*enqueued_at;
	int				i;
	trx_preprocessing_states_t	state;
	unsigned char			priority = TRX_PREPROC_PRIORITY_NONE;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() itemid: " TRX_FS_UI64, __func__, value->itemid);

	item_local.itemid = value->itemid;
	item = (trx_preproc_item_t *)trx_hashset_search(&manager->item_config, &item_local);

	/* override priority based on item type */
	if (NULL != item && ITEM_TYPE_INTERNAL == item->type)
		priority = TRX_PREPROC_PRIORITY_FIRST;

	if (NULL == item || 0 == item->preproc_ops_num || (ITEM_STATE_NOTSUPPORTED != value->state &&
			(NULL == value->result || 0 == ISSET_VALUE(value->result))))
	{
		state = REQUEST_STATE_DONE;

		if (NULL == manager->queue.head)
		{
			/* queue is empty and item is done, it can be flushed */
			preprocessor_flush_value(value);
			manager->processed_num++;
			preprocessor_enqueue_dependent(manager, value, NULL);
			preproc_item_value_clear(value);

			goto out;
		}
	}
	else
		state = REQUEST_STATE_QUEUED;

	request = (trx_preprocessing_request_t *)trx_malloc(NULL, sizeof(trx_preprocessing_request_t));
	memset(request, 0, sizeof(trx_preprocessing_request_t));
	memcpy(&request->value, value, sizeof(trx_preproc_item_value_t));
	request->state = state;

	if (REQUEST_STATE_QUEUED == state && ITEM_STATE_NOTSUPPORTED != value->state)
	{
		request->value_type = item->value_type;
		request->steps = (trx_preproc_op_t *)trx_malloc(NULL, sizeof(trx_preproc_op_t) * item->preproc_ops_num);
		request->steps_num = item->preproc_ops_num;

		for (i = 0; i < item->preproc_ops_num; i++)
		{
			request->steps[i].type = item->preproc_ops[i].type;
			request->steps[i].params = trx_strdup(NULL, item->preproc_ops[i].params);
			request->steps[i].error_handler = item->preproc_ops[i].error_handler;
			request->steps[i].error_handler_params = trx_strdup(NULL,
					item->preproc_ops[i].error_handler_params);
		}

		manager->preproc_num++;
	}

	/* priority items are enqueued at the beginning of the line */
	if (NULL == master && TRX_PREPROC_PRIORITY_FIRST == priority)
	{
		if (SUCCEED == trx_list_iterator_isset(&manager->priority_tail))
		{
			/* insert after the last internal item */
			trx_list_insert_after(&manager->queue, manager->priority_tail.current, request, &enqueued_at);
			trx_list_iterator_update(&manager->priority_tail);
		}
		else
		{
			/* no internal items in queue, insert at the beginning */
			trx_list_prepend(&manager->queue, request, &enqueued_at);
			trx_list_iterator_init(&manager->queue, &manager->priority_tail);
		}

		trx_list_iterator_next(&manager->priority_tail);
	}
	else
	{
		trx_list_insert_after(&manager->queue, master, request, &enqueued_at);
		trx_list_iterator_update(&manager->priority_tail);

		/* move internal item tail position if we are inserting after last internal item */
		if (NULL != master && master == manager->priority_tail.current)
			trx_list_iterator_next(&manager->priority_tail);
	}

	if (REQUEST_STATE_QUEUED == request->state)
		preprocessor_link_items(manager, enqueued_at, item);

	/* if no preprocessing is needed, dependent items are enqueued */
	if (REQUEST_STATE_DONE == request->state)
		preprocessor_enqueue_dependent(manager, value, enqueued_at);

	manager->queued_num++;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_enqueue_dependent                                   *
 *                                                                            *
 * Purpose: enqueue dependent items (if any)                                  *
 *                                                                            *
 * Parameters: manager      - [IN] preprocessing manager                      *
 *             source_value - [IN] master item value                          *
 *             master       - [IN] dependent item should be enqueued after    *
 *                                 this item                                  *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_enqueue_dependent(trx_preprocessing_manager_t *manager,
		trx_preproc_item_value_t *source_value, trx_list_item_t *master)
{
	int				i;
	trx_preproc_item_t		*item, item_local;
	trx_preproc_item_value_t	value;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() itemid: " TRX_FS_UI64, __func__, source_value->itemid);

	if (NULL != source_value->result && ISSET_VALUE(source_value->result))
	{
		item_local.itemid = source_value->itemid;
		if (NULL != (item = (trx_preproc_item_t *)trx_hashset_search(&manager->item_config, &item_local)) &&
				0 != item->dep_itemids_num)
		{
			for (i = item->dep_itemids_num - 1; i >= 0; i--)
			{
				preprocessor_copy_value(&value, source_value);
				value.itemid = item->dep_itemids[i].first;
				value.item_flags = item->dep_itemids[i].second;
				preprocessor_enqueue(manager, &value, master);
			}

			preprocessor_assign_tasks(manager);
			preprocessing_flush_queue(manager);
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_add_request                                         *
 *                                                                            *
 * Purpose: handle new preprocessing request                                  *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *             message - [IN] packed preprocessing request                    *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_add_request(trx_preprocessing_manager_t *manager, trx_ipc_message_t *message)
{
	trx_uint32_t			offset = 0;
	trx_preproc_item_value_t	value;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	preprocessor_sync_configuration(manager);

	while (offset < message->size)
	{
		offset += trx_preprocessor_unpack_value(&value, message->data + offset);
		preprocessor_enqueue(manager, &value, NULL);
	}

	preprocessor_assign_tasks(manager);
	preprocessing_flush_queue(manager);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_add_test_request                                    *
 *                                                                            *
 * Purpose: handle new preprocessing test request                             *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *             message - [IN] packed preprocessing request                    *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_add_test_request(trx_preprocessing_manager_t *manager, trx_ipc_client_t *client,
		trx_ipc_message_t *message)
{
	trx_preprocessing_direct_request_t	*direct_request;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_ipc_client_addref(client);
	direct_request = trx_malloc(NULL, sizeof(trx_preprocessing_direct_request_t));
	direct_request->client = client;
	trx_ipc_message_copy(&direct_request->message, message);
	trx_list_append(&manager->direct_queue, direct_request, NULL);

	preprocessor_assign_tasks(manager);
	preprocessing_flush_queue(manager);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_set_variant_result                                  *
 *                                                                            *
 * Purpose: get result data from variant and error message                    *
 *                                                                            *
 * Parameters: request - [IN/OUT] preprocessing request                       *
 *             value   - [IN] variant value                                   *
 *             error   - [IN] error message (if any)                          *
 *                                                                            *
 ******************************************************************************/
static int	preprocessor_set_variant_result(trx_preprocessing_request_t *request, trx_variant_t *value, char *error)
{
	int		type, ret = FAIL;
	trx_log_t	*log;

	if (NULL != error)
	{
		/* on error item state is set to ITEM_STATE_NOTSUPPORTED */
		request->value.state = ITEM_STATE_NOTSUPPORTED;
		request->value.error = error;
		ret = FAIL;

		goto out;
	}

	if (TRX_VARIANT_NONE == value->type)
	{
		UNSET_UI64_RESULT(request->value.result);
		UNSET_DBL_RESULT(request->value.result);
		UNSET_STR_RESULT(request->value.result);
		UNSET_TEXT_RESULT(request->value.result);
		UNSET_LOG_RESULT(request->value.result);
		UNSET_MSG_RESULT(request->value.result);
		ret = FAIL;

		goto out;
	}

	switch (request->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			type = TRX_VARIANT_DBL;
			break;
		case ITEM_VALUE_TYPE_UINT64:
			type = TRX_VARIANT_UI64;
			break;
		default:
			/* ITEM_VALUE_TYPE_STR, ITEM_VALUE_TYPE_TEXT, ITEM_VALUE_TYPE_LOG */
			type = TRX_VARIANT_STR;
	}

	if (FAIL != (ret = trx_variant_convert(value, type)))
	{
		switch (request->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				UNSET_RESULT_EXCLUDING(request->value.result, AR_DOUBLE);
				SET_DBL_RESULT(request->value.result, value->data.dbl);
				break;
			case ITEM_VALUE_TYPE_STR:
				UNSET_RESULT_EXCLUDING(request->value.result, AR_STRING);
				UNSET_STR_RESULT(request->value.result);
				SET_STR_RESULT(request->value.result, value->data.str);
				break;
			case ITEM_VALUE_TYPE_LOG:
				UNSET_RESULT_EXCLUDING(request->value.result, AR_LOG);
				if (ISSET_LOG(request->value.result))
				{
					log = GET_LOG_RESULT(request->value.result);
					trx_free(log->value);
				}
				else
				{
					log = (trx_log_t *)trx_malloc(NULL, sizeof(trx_log_t));
					memset(log, 0, sizeof(trx_log_t));
					SET_LOG_RESULT(request->value.result, log);
				}
				log->value = value->data.str;
				break;
			case ITEM_VALUE_TYPE_UINT64:
				UNSET_RESULT_EXCLUDING(request->value.result, AR_UINT64);
				SET_UI64_RESULT(request->value.result, value->data.ui64);
				break;
			case ITEM_VALUE_TYPE_TEXT:
				UNSET_RESULT_EXCLUDING(request->value.result, AR_TEXT);
				UNSET_TEXT_RESULT(request->value.result);
				SET_TEXT_RESULT(request->value.result, value->data.str);
				break;
		}

		trx_variant_set_none(value);
	}
	else
	{
		trx_free(request->value.error);
		request->value.error = trx_dsprintf(NULL, "Value \"%s\" of type \"%s\" is not suitable for"
			" value type \"%s\"", trx_variant_value_desc(value), trx_variant_type_desc(value),
			trx_item_value_type_string((trx_item_value_type_t)request->value_type));

		request->value.state = ITEM_STATE_NOTSUPPORTED;
		ret = FAIL;
	}

out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_add_result                                          *
 *                                                                            *
 * Purpose: handle preprocessing result                                       *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *             client  - [IN] IPC client                                      *
 *             message - [IN] packed preprocessing result                     *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_add_result(trx_preprocessing_manager_t *manager, trx_ipc_client_t *client,
		trx_ipc_message_t *message)
{
	trx_preprocessing_worker_t	*worker;
	trx_preprocessing_request_t	*request;
	trx_variant_t			value;
	char				*error;
	trx_vector_ptr_t		history;
	trx_preproc_history_t		*vault;
	trx_list_item_t			*node;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	worker = preprocessor_get_worker_by_client(manager, client);
	node = (trx_list_item_t *)worker->task;
	request = (trx_preprocessing_request_t *)node->data;

	trx_vector_ptr_create(&history);
	trx_preprocessor_unpack_result(&value, &history, &error, message->data);

	if (NULL != (vault = (trx_preproc_history_t *)trx_hashset_search(&manager->history_cache,
			&request->value.itemid)))
	{
		trx_vector_ptr_clear_ext(&vault->history, (trx_clean_func_t)trx_preproc_op_history_free);
	}

	if (0 != history.values_num)
	{
		if (NULL == vault)
		{
			trx_preproc_history_t	history_local;

			history_local.itemid = request->value.itemid;
			vault = (trx_preproc_history_t *)trx_hashset_insert(&manager->history_cache, &history_local,
					sizeof(history_local));
			trx_vector_ptr_create(&vault->history);
		}

		trx_vector_ptr_append_array(&vault->history, history.values, history.values_num);
		trx_vector_ptr_clear(&history);
	}
	else
	{
		if (NULL != vault)
		{
			trx_vector_ptr_destroy(&vault->history);
			trx_hashset_remove_direct(&manager->history_cache, vault);
		}
	}

	preprocessor_set_request_state_done(manager, request, worker->task);

	if (FAIL != preprocessor_set_variant_result(request, &value, error))
		preprocessor_enqueue_dependent(manager, &request->value, worker->task);

	worker->task = NULL;
	trx_variant_clear(&value);

	manager->preproc_num--;

	preprocessor_assign_tasks(manager);
	preprocessing_flush_queue(manager);

	trx_vector_ptr_destroy(&history);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_flush_test_result                                   *
 *                                                                            *
 * Purpose: handle preprocessing result                                       *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *             client  - [IN] IPC client                                      *
 *             message - [IN] packed preprocessing result                     *
 *                                                                            *
 * Comments: Preprocessing testing results are directly forwarded to source   *
 *           client as they are.                                              *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_flush_test_result(trx_preprocessing_manager_t *manager, trx_ipc_client_t *client,
		trx_ipc_message_t *message)
{
	trx_preprocessing_worker_t		*worker;
	trx_preprocessing_direct_request_t	*direct_request;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	worker = preprocessor_get_worker_by_client(manager, client);
	direct_request = (trx_preprocessing_direct_request_t *)worker->task;

	/* forward the response to the client */
	if (SUCCEED == trx_ipc_client_connected(direct_request->client))
		trx_ipc_client_send(direct_request->client, message->code, message->data, message->size);

	worker->task = NULL;
	preprocessor_free_direct_request(direct_request);

	preprocessor_assign_tasks(manager);
	preprocessing_flush_queue(manager);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_init_manager                                        *
 *                                                                            *
 * Purpose: initializes preprocessing manager                                 *
 *                                                                            *
 * Parameters: manager - [IN] the manager to initialize                       *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_init_manager(trx_preprocessing_manager_t *manager)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s() workers: %d", __func__, CONFIG_PREPROCESSOR_FORKS);

	memset(manager, 0, sizeof(trx_preprocessing_manager_t));

	manager->workers = (trx_preprocessing_worker_t *)trx_calloc(NULL, CONFIG_PREPROCESSOR_FORKS,
			sizeof(trx_preprocessing_worker_t));
	trx_list_create(&manager->queue);
	trx_list_create(&manager->direct_queue);
	trx_hashset_create_ext(&manager->item_config, 0, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC,
			(trx_clean_func_t)preproc_item_clear,
			TRX_DEFAULT_MEM_MALLOC_FUNC, TRX_DEFAULT_MEM_REALLOC_FUNC, TRX_DEFAULT_MEM_FREE_FUNC);
	trx_hashset_create(&manager->linked_items, 0, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_hashset_create(&manager->history_cache, 1000, TRX_DEFAULT_UINT64_HASH_FUNC,
			TRX_DEFAULT_UINT64_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_register_worker                                     *
 *                                                                            *
 * Purpose: registers preprocessing worker                                    *
 *                                                                            *
 * Parameters: manager - [IN] the manager                                     *
 *             client  - [IN] the connected preprocessing worker              *
 *             message - [IN] message received by preprocessing manager       *
 *                                                                            *
 ******************************************************************************/
static void preprocessor_register_worker(trx_preprocessing_manager_t *manager, trx_ipc_client_t *client,
		trx_ipc_message_t *message)
{
	trx_preprocessing_worker_t	*worker = NULL;
	pid_t				ppid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	memcpy(&ppid, message->data, sizeof(ppid));

	if (ppid != getppid())
	{
		trx_ipc_client_close(client);
		treegix_log(LOG_LEVEL_DEBUG, "refusing connection from foreign process");
	}
	else
	{
		if (CONFIG_PREPROCESSOR_FORKS == manager->worker_count)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		worker = (trx_preprocessing_worker_t *)&manager->workers[manager->worker_count++];
		worker->client = client;

		preprocessor_assign_tasks(manager);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_destroy_manager                                     *
 *                                                                            *
 * Purpose: destroy preprocessing manager                                     *
 *                                                                            *
 * Parameters: manager - [IN] the manager to destroy                          *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_destroy_manager(trx_preprocessing_manager_t *manager)
{
	trx_preprocessing_request_t		*request;
	trx_preprocessing_direct_request_t	*direct_request;

	trx_free(manager->workers);

	/* this is the place where values are lost */
	while (SUCCEED == trx_list_pop(&manager->direct_queue, (void **)&direct_request))
		preprocessor_free_direct_request(direct_request);

	trx_list_destroy(&manager->direct_queue);

	while (SUCCEED == trx_list_pop(&manager->queue, (void **)&request))
		preprocessor_free_request(request);

	trx_list_destroy(&manager->queue);

	trx_hashset_destroy(&manager->item_config);
	trx_hashset_destroy(&manager->linked_items);
	trx_hashset_destroy(&manager->history_cache);
}

TRX_THREAD_ENTRY(preprocessing_manager_thread, args)
{
	trx_ipc_service_t		service;
	char				*error = NULL;
	trx_ipc_client_t		*client;
	trx_ipc_message_t		*message;
	trx_preprocessing_manager_t	manager;
	int				ret;
	double				time_stat, time_idle = 0, time_now, time_flush, sec;

#define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	trx_setproctitle("%s #%d starting", get_process_type_string(process_type), process_num);

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	if (FAIL == trx_ipc_service_start(&service, TRX_IPC_SERVICE_PREPROCESSING, &error))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot start preprocessing service: %s", error);
		trx_free(error);
		exit(EXIT_FAILURE);
	}

	preprocessor_init_manager(&manager);

	/* initialize statistics */
	time_stat = trx_time();
	time_flush = time_stat;

	trx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);

	update_selfmon_counter(TRX_PROCESS_STATE_BUSY);

	while (TRX_IS_RUNNING())
	{
		time_now = trx_time();

		if (STAT_INTERVAL < time_now - time_stat)
		{
			trx_setproctitle("%s #%d [queued " TRX_FS_UI64 ", processed " TRX_FS_UI64 " values, idle "
					TRX_FS_DBL " sec during " TRX_FS_DBL " sec]",
					get_process_type_string(process_type), process_num,
					manager.queued_num, manager.processed_num, time_idle, time_now - time_stat);

			time_stat = time_now;
			time_idle = 0;
			manager.processed_num = 0;
		}

		update_selfmon_counter(TRX_PROCESS_STATE_IDLE);
		ret = trx_ipc_service_recv(&service, TRX_PREPROCESSING_MANAGER_DELAY, &client, &message);
		update_selfmon_counter(TRX_PROCESS_STATE_BUSY);
		sec = trx_time();
		trx_update_env(sec);

		if (TRX_IPC_RECV_IMMEDIATE != ret)
			time_idle += sec - time_now;

		if (NULL != message)
		{
			switch (message->code)
			{
				case TRX_IPC_PREPROCESSOR_WORKER:
					preprocessor_register_worker(&manager, client, message);
					break;
				case TRX_IPC_PREPROCESSOR_REQUEST:
					preprocessor_add_request(&manager, message);
					break;
				case TRX_IPC_PREPROCESSOR_RESULT:
					preprocessor_add_result(&manager, client, message);
					break;
				case TRX_IPC_PREPROCESSOR_QUEUE:
					trx_ipc_client_send(client, message->code, (unsigned char *)&manager.queued_num,
							sizeof(trx_uint64_t));
					break;
				case TRX_IPC_PREPROCESSOR_TEST_REQUEST:
					preprocessor_add_test_request(&manager, client, message);
					break;
				case TRX_IPC_PREPROCESSOR_TEST_RESULT:
					preprocessor_flush_test_result(&manager, client, message);
					break;
			}

			trx_ipc_message_free(message);
		}

		if (NULL != client)
			trx_ipc_client_release(client);

		if (0 == manager.preproc_num || 1 < time_now - time_flush)
		{
			dc_flush_history();
			time_flush = time_now;
		}
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);

	trx_ipc_service_close(&service);
	preprocessor_destroy_manager(&manager);
#undef STAT_INTERVAL
}
