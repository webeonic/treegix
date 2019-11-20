

#include "common.h"

#include "db.h"
#include "log.h"
#include "mutexs.h"

#define LOCK_ITSERVICES		trx_mutex_lock(itservices_lock)
#define UNLOCK_ITSERVICES	trx_mutex_unlock(itservices_lock)

static trx_mutex_t	itservices_lock = TRX_MUTEX_NULL;

/* status update queue items */
typedef struct
{
	/* the update source id */
	trx_uint64_t	sourceid;
	/* the new status */
	int		status;
	/* timestamp */
	int		clock;
}
trx_status_update_t;

/* Service node */
typedef struct
{
	/* service id */
	trx_uint64_t		serviceid;
	/* trigger id of leaf nodes */
	trx_uint64_t		triggerid;
	/* the initial service status */
	int			old_status;
	/* the calculated service status */
	int			status;
	/* the service status calculation algorithm, see SERVICE_ALGORITHM_* defines */
	int			algorithm;
	/* the parent nodes */
	trx_vector_ptr_t	parents;
	/* the child nodes */
	trx_vector_ptr_t	children;
}
trx_itservice_t;

/* index of services by triggerid */
typedef struct
{
	trx_uint64_t		triggerid;
	trx_vector_ptr_t	itservices;
}
trx_itservice_index_t;

/* a set of services used during update session                          */
/*                                                                          */
/* All services are stored into hashset accessed by serviceid. The services */
/* also are indexed by triggerid.                                           */
/* The following types of services are loaded during update session:        */
/*  1) services directly linked to the triggers with values changed         */
/*     during update session.                                               */
/*  2) direct or indirect parent services of (1)                            */
/*  3) services required to calculate status of (2) and not already loaded  */
/*     as (1) or (2).                                                       */
/*                                                                          */
/* In this schema:                                                          */
/*   (1) can't have children services                                       */
/*   (2) will have children services                                        */
/*   (1) and (2) will have parent services unless it's the root service     */
/*   (3) will have neither children or parent services                      */
/*                                                                          */
typedef struct
{
	/* loaded services */
	trx_hashset_t	itservices;
	/* service index by triggerid */
	trx_hashset_t	index;
}
trx_itservices_t;

/******************************************************************************
 *                                                                            *
 * Function: its_itservices_init                                              *
 *                                                                            *
 * Purpose: initializes services data set to store services during update     *
 *          session                                                           *
 *                                                                            *
 * Parameters: set   - [IN] the data set to initialize                        *
 *                                                                            *
 ******************************************************************************/
static void	its_itservices_init(trx_itservices_t *itservices)
{
	trx_hashset_create(&itservices->itservices, 512, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_hashset_create(&itservices->index, 128, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: its_itservices_clean                                             *
 *                                                                            *
 * Purpose: cleans services data set by releasing allocated memory            *
 *                                                                            *
 * Parameters: set   - [IN] the data set to clean                             *
 *                                                                            *
 ******************************************************************************/
static void	its_itservices_clean(trx_itservices_t *itservices)
{
	trx_hashset_iter_t	iter;
	trx_itservice_t		*itservice;
	trx_itservice_index_t	*index;

	trx_hashset_iter_reset(&itservices->index, &iter);

	while (NULL != (index = (trx_itservice_index_t *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_destroy(&index->itservices);

	trx_hashset_destroy(&itservices->index);

	trx_hashset_iter_reset(&itservices->itservices, &iter);

	while (NULL != (itservice = (trx_itservice_t *)trx_hashset_iter_next(&iter)))
	{
		trx_vector_ptr_destroy(&itservice->children);
		trx_vector_ptr_destroy(&itservice->parents);
	}

	trx_hashset_destroy(&itservices->itservices);
}

/******************************************************************************
 *                                                                            *
 * Function: its_itservice_create                                             *
 *                                                                            *
 * Purpose: creates a new service node                                        *
 *                                                                            *
 * Parameters: itservices  - [IN] the services data                           *
 *             serviceid   - [IN] the service id                              *
 *             algorithm   - [IN] the service status calculation mode         *
 *             triggerid   - [IN] the source trigger id for leaf nodes        *
 *             status      - [IN] the initial service status                  *
 *                                                                            *
 * Return value: the created service node                                     *
 *                                                                            *
 ******************************************************************************/
static trx_itservice_t	*its_itservice_create(trx_itservices_t *itservices, trx_uint64_t serviceid,
		trx_uint64_t triggerid, int status, int algorithm)
{
	trx_itservice_t		itservice = {.serviceid = serviceid, .triggerid = triggerid, .old_status = status,
				.status = status, .algorithm = algorithm}, *pitservice;
	trx_itservice_index_t	*pindex;

	trx_vector_ptr_create(&itservice.children);
	trx_vector_ptr_create(&itservice.parents);

	pitservice = (trx_itservice_t *)trx_hashset_insert(&itservices->itservices, &itservice, sizeof(itservice));

	if (0 != triggerid)
	{
		if (NULL == (pindex = (trx_itservice_index_t *)trx_hashset_search(&itservices->index, &triggerid)))
		{
			trx_itservice_index_t	index = {.triggerid = triggerid};

			trx_vector_ptr_create(&index.itservices);

			pindex = (trx_itservice_index_t *)trx_hashset_insert(&itservices->index, &index, sizeof(index));
		}

		trx_vector_ptr_append(&pindex->itservices, pitservice);
	}

	return pitservice;
}

/******************************************************************************
 *                                                                            *
 * Function: its_updates_append                                               *
 *                                                                            *
 * Purpose: adds an update to the queue                                       *
 *                                                                            *
 * Parameters: updates   - [OUT] the update queue                             *
 *             sourceid  - [IN] the update source id                          *
 *             status    - [IN] the update status                             *
 *             clock     - [IN] the update timestamp                          *
 *                                                                            *
 ******************************************************************************/
static void	its_updates_append(trx_vector_ptr_t *updates, trx_uint64_t sourceid, int status, int clock)
{
	trx_status_update_t	*update;

	update = (trx_status_update_t *)trx_malloc(NULL, sizeof(trx_status_update_t));

	update->sourceid = sourceid;
	update->status = status;
	update->clock = clock;

	trx_vector_ptr_append(updates, update);
}

static void	trx_status_update_free(trx_status_update_t *update)
{
	trx_free(update);
}

/******************************************************************************
 *                                                                            *
 * Function: its_itservices_load_children                                     *
 *                                                                            *
 * Purpose: loads all missing children of the specified services              *
 *                                                                            *
 * Parameters: itservices   - [IN] the services data                          *
 *                                                                            *
 ******************************************************************************/
static void	its_itservices_load_children(trx_itservices_t *itservices)
{
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	DB_RESULT		result;
	DB_ROW			row;
	trx_itservice_t		*itservice, *parent;
	trx_uint64_t		serviceid, parentid;
	trx_vector_uint64_t	serviceids;
	trx_hashset_iter_t	iter;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&serviceids);

	trx_hashset_iter_reset(&itservices->itservices, &iter);

	while (NULL != (itservice = (trx_itservice_t *)trx_hashset_iter_next(&iter)))
	{
		if (0 == itservice->triggerid)
			trx_vector_uint64_append(&serviceids, itservice->serviceid);
	}

	/* check for extreme case when there are only leaf nodes */
	if (0 == serviceids.values_num)
		goto out;

	trx_vector_uint64_sort(&serviceids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select s.serviceid,s.status,s.algorithm,sl.serviceupid"
			" from services s,services_links sl"
			" where s.serviceid=sl.servicedownid"
				" and");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "sl.serviceupid", serviceids.values,
			serviceids.values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(serviceid, row[0]);
		TRX_STR2UINT64(parentid, row[3]);

		if (NULL == (parent = (trx_itservice_t *)trx_hashset_search(&itservices->itservices, &parentid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		if (NULL == (itservice = (trx_itservice_t *)trx_hashset_search(&itservices->itservices, &serviceid)))
			itservice = its_itservice_create(itservices, serviceid, 0, atoi(row[1]), atoi(row[2]));

		if (FAIL == trx_vector_ptr_search(&parent->children, itservice, TRX_DEFAULT_PTR_COMPARE_FUNC))
			trx_vector_ptr_append(&parent->children, itservice);
	}
	DBfree_result(result);

	trx_free(sql);

	trx_vector_uint64_destroy(&serviceids);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: its_itservices_load_parents                                      *
 *                                                                            *
 * Purpose: recursively loads parent nodes of the specified service until the *
 *          root node                                                         *
 *                                                                            *
 * Parameters: itservices   - [IN] the services data                          *
 *             serviceids   - [IN] a vector containing ids of services to     *
 *                                 load parents                               *
 *                                                                            *
 ******************************************************************************/
static void	its_itservices_load_parents(trx_itservices_t *itservices, trx_vector_uint64_t *serviceids)
{
	DB_RESULT	result;
	DB_ROW		row;
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;
	trx_itservice_t	*parent, *itservice;
	trx_uint64_t	parentid, serviceid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_sort(serviceids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_uniq(serviceids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select s.serviceid,s.status,s.algorithm,sl.servicedownid"
			" from services s,services_links sl"
			" where s.serviceid=sl.serviceupid"
				" and");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "sl.servicedownid", serviceids->values,
			serviceids->values_num);

	trx_vector_uint64_clear(serviceids);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(parentid, row[0]);
		TRX_STR2UINT64(serviceid, row[3]);

		/* find the service */
		if (NULL == (itservice = (trx_itservice_t *)trx_hashset_search(&itservices->itservices, &serviceid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		/* find/load the parent service */
		if (NULL == (parent = (trx_itservice_t *)trx_hashset_search(&itservices->itservices, &parentid)))
		{
			parent = its_itservice_create(itservices, parentid, 0, atoi(row[1]), atoi(row[2]));
			trx_vector_uint64_append(serviceids, parent->serviceid);
		}

		/* link the service as a parent's child */
		if (FAIL == trx_vector_ptr_search(&itservice->parents, parent, TRX_DEFAULT_PTR_COMPARE_FUNC))
			trx_vector_ptr_append(&itservice->parents, parent);
	}
	DBfree_result(result);

	trx_free(sql);

	if (0 != serviceids->values_num)
		its_itservices_load_parents(itservices, serviceids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: its_load_services_by_triggerids                                  *
 *                                                                            *
 * Purpose: loads services that might be affected by the specified triggerid  *
 *          or are required to calculate status of loaded services            *
 *                                                                            *
 * Parameters: itservices - [IN] the services data                            *
 *             triggerids - [IN] the sorted list of trigger ids               *
 *                                                                            *
 ******************************************************************************/
static void	its_load_services_by_triggerids(trx_itservices_t *itservices, const trx_vector_uint64_t *triggerids)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_uint64_t		serviceid, triggerid;
	trx_itservice_t		*itservice;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_vector_uint64_t	serviceids;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&serviceids);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select serviceid,triggerid,status,algorithm"
			" from services"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid", triggerids->values, triggerids->values_num);

	result = DBselect("%s", sql);

	trx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(serviceid, row[0]);
		TRX_STR2UINT64(triggerid, row[1]);

		itservice = its_itservice_create(itservices, serviceid, triggerid, atoi(row[2]), atoi(row[3]));

		trx_vector_uint64_append(&serviceids, itservice->serviceid);
	}
	DBfree_result(result);

	if (0 != serviceids.values_num)
	{
		its_itservices_load_parents(itservices, &serviceids);
		its_itservices_load_children(itservices);
	}

	trx_vector_uint64_destroy(&serviceids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: its_itservice_update_status                                      *
 *                                                                            *
 * Purpose: updates service and its parents statuses                          *
 *                                                                            *
 * Parameters: service    - [IN] the service to update                        *
 *             clock      - [IN] the update timestamp                         *
 *             alarms     - [OUT] the alarms update queue                     *
 *                                                                            *
 * Comments: This function recalculates service status according to the       *
 *           algorithm and status of the children services. If the status     *
 *           has been changed, an alarm is generated and parent services      *
 *           (up until the root service) are updated too.                     *
 *                                                                            *
 ******************************************************************************/
static void	its_itservice_update_status(trx_itservice_t *itservice, int clock, trx_vector_ptr_t *alarms)
{
	int	status, i;

	switch (itservice->algorithm)
	{
		case SERVICE_ALGORITHM_MIN:
			status = TRIGGER_SEVERITY_COUNT;
			for (i = 0; i < itservice->children.values_num; i++)
			{
				trx_itservice_t	*child = (trx_itservice_t *)itservice->children.values[i];

				if (child->status < status)
					status = child->status;
			}
			break;
		case SERVICE_ALGORITHM_MAX:
			status = 0;
			for (i = 0; i < itservice->children.values_num; i++)
			{
				trx_itservice_t	*child = (trx_itservice_t *)itservice->children.values[i];

				if (child->status > status)
					status = child->status;
			}
			break;
		case SERVICE_ALGORITHM_NONE:
			goto out;
		default:
			treegix_log(LOG_LEVEL_ERR, "unknown calculation algorithm of service status [%d]",
					itservice->algorithm);
			goto out;
	}

	if (itservice->status != status)
	{
		itservice->status = status;

		its_updates_append(alarms, itservice->serviceid, status, clock);

		/* update parent services */
		for (i = 0; i < itservice->parents.values_num; i++)
			its_itservice_update_status((trx_itservice_t *)itservice->parents.values[i], clock, alarms);
	}
out:
	;
}

/******************************************************************************
 *                                                                            *
 * Function: its_updates_compare                                              *
 *                                                                            *
 * Purpose: used to sort service updates by source id                         *
 *                                                                            *
 ******************************************************************************/
static int	its_updates_compare(const trx_status_update_t **update1, const trx_status_update_t **update2)
{
	TRX_RETURN_IF_NOT_EQUAL((*update1)->sourceid, (*update2)->sourceid);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: its_write_status_and_alarms                                      *
 *                                                                            *
 * Purpose: writes service status changes and generated service alarms into   *
 *          database                                                          *
 *                                                                            *
 * Parameters: itservices - [IN] the services data                            *
 *             alarms     - [IN] the service alarms update queue              *
 *                                                                            *
 * Return value: SUCCEED - the data was written successfully                  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	its_write_status_and_alarms(trx_itservices_t *itservices, trx_vector_ptr_t *alarms)
{
	int			i, ret = FAIL;
	trx_vector_ptr_t	updates;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_uint64_t		alarmid;
	trx_hashset_iter_t	iter;
	trx_itservice_t		*itservice;

	/* get a list of service status updates that must be written to database */
	trx_vector_ptr_create(&updates);
	trx_hashset_iter_reset(&itservices->itservices, &iter);

	while (NULL != (itservice = (trx_itservice_t *)trx_hashset_iter_next(&iter)))
	{
		if (itservice->old_status != itservice->status)
			its_updates_append(&updates, itservice->serviceid, itservice->status, 0);
	}

	/* write service status changes into database */
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (0 != updates.values_num)
	{
		trx_vector_ptr_sort(&updates, (trx_compare_func_t)its_updates_compare);
		trx_vector_ptr_uniq(&updates, (trx_compare_func_t)its_updates_compare);

		for (i = 0; i < updates.values_num; i++)
		{
			trx_status_update_t	*update = (trx_status_update_t *)updates.values[i];

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"update services"
					" set status=%d"
					" where serviceid=" TRX_FS_UI64 ";\n",
					update->status, update->sourceid);

			if (SUCCEED != DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
				goto out;
		}
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)
	{
		if (TRX_DB_OK > DBexecute("%s", sql))
			goto out;
	}

	ret = SUCCEED;

	/* write generated service alarms into database */
	if (0 != alarms->values_num)
	{
		trx_db_insert_t	db_insert;

		alarmid = DBget_maxid_num("service_alarms", alarms->values_num);

		trx_db_insert_prepare(&db_insert, "service_alarms", "servicealarmid", "serviceid", "value", "clock",
				NULL);

		for (i = 0; i < alarms->values_num; i++)
		{
			trx_status_update_t	*update = (trx_status_update_t *)alarms->values[i];

			trx_db_insert_add_values(&db_insert, alarmid++, update->sourceid, update->status,
					update->clock);
		}

		ret = trx_db_insert_execute(&db_insert);

		trx_db_insert_clean(&db_insert);
	}
out:
	trx_free(sql);

	trx_vector_ptr_clear_ext(&updates, (trx_clean_func_t)trx_status_update_free);
	trx_vector_ptr_destroy(&updates);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: its_flush_updates                                                *
 *                                                                            *
 * Purpose: processes the service update queue                                *
 *                                                                            *
 * Return value: SUCCEED - the data was written successfully                  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: The following steps are taken to process the queue:              *
 *           1) Load all services either directly referenced (with triggerid) *
 *              by update queue or dependent on those services (directly or   *
 *              indirectly) or required to calculate status of any loaded     *
 *              services.                                                     *
 *           2) Apply updates to the loaded service tree. Queue new service   *
 *              alarms whenever service status changes.                       *
 *           3) Write the final service status changes and the generated      *
 *              service alarm queue into database.                            *
 *                                                                            *
 ******************************************************************************/
static int	its_flush_updates(const trx_vector_ptr_t *updates)
{
	int				i, j, k, ret = FAIL;
	const trx_status_update_t	*update;
	trx_itservices_t		itservices;
	trx_vector_ptr_t		alarms;
	trx_itservice_index_t		*index;
	trx_vector_uint64_t		triggerids;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	its_itservices_init(&itservices);

	trx_vector_uint64_create(&triggerids);

	for (i = 0; i < updates->values_num; i++)
	{
		update = (trx_status_update_t *)updates->values[i];

		trx_vector_uint64_append(&triggerids, update->sourceid);
	}

	trx_vector_uint64_sort(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	/* load all services affected by the trigger status change and      */
	/* the services that are required for resulting status calculations */
	its_load_services_by_triggerids(&itservices, &triggerids);

	trx_vector_uint64_destroy(&triggerids);

	if (0 == itservices.itservices.num_data)
	{
		ret = SUCCEED;
		goto out;
	}

	trx_vector_ptr_create(&alarms);

	/* apply status updates */
	for (i = 0; i < updates->values_num; i++)
	{
		update = (const trx_status_update_t *)updates->values[i];

		if (NULL == (index = (trx_itservice_index_t *)trx_hashset_search(&itservices.index, update)))
			continue;

		/* change the status of services based on the update */
		for (j = 0; j < index->itservices.values_num; j++)
		{
			trx_itservice_t	*itservice = (trx_itservice_t *)index->itservices.values[j];

			if (SERVICE_ALGORITHM_NONE == itservice->algorithm || itservice->status == update->status)
				continue;

			its_updates_append(&alarms, itservice->serviceid, update->status, update->clock);
			itservice->status = update->status;
		}

		/* recalculate status of the parent services */
		for (j = 0; j < index->itservices.values_num; j++)
		{
			trx_itservice_t	*itservice = (trx_itservice_t *)index->itservices.values[j];

			/* update parent services */
			for (k = 0; k < itservice->parents.values_num; k++)
				its_itservice_update_status((trx_itservice_t *)itservice->parents.values[k], update->clock, &alarms);
		}
	}

	ret = its_write_status_and_alarms(&itservices, &alarms);

	trx_vector_ptr_clear_ext(&alarms, (trx_clean_func_t)trx_status_update_free);
	trx_vector_ptr_destroy(&alarms);
out:
	its_itservices_clean(&itservices);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/*
 * Public API
 */

/******************************************************************************
 *                                                                            *
 * Function: DBupdate_itservices                                              *
 *                                                                            *
 * Purpose: updates services by applying event list                           *
 *                                                                            *
 * Return value: SUCCEED - the services were updated successfully             *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	DBupdate_itservices(const trx_vector_ptr_t *trigger_diff)
{
	int				ret = SUCCEED;
	trx_vector_ptr_t		updates;
	int				i;
	const trx_trigger_diff_t	*diff;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&updates);

	for (i = 0; i < trigger_diff->values_num; i++)
	{
		diff = (trx_trigger_diff_t *)trigger_diff->values[i];

		if (0 == (diff->flags & TRX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE))
			continue;

		its_updates_append(&updates, diff->triggerid, TRIGGER_VALUE_PROBLEM == diff->value ?
				diff->priority : 0, diff->lastchange);
	}

	if (0 != updates.values_num)
	{
		LOCK_ITSERVICES;

		do
		{
			DBbegin();

			ret = its_flush_updates(&updates);
		}
		while (TRX_DB_DOWN == DBcommit());

		UNLOCK_ITSERVICES;

		trx_vector_ptr_clear_ext(&updates, trx_ptr_free);
	}

	trx_vector_ptr_destroy(&updates);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBremove_itservice_triggers                                      *
 *                                                                            *
 * Purpose: removes specified trigger ids from dependent services and reset   *
 *          the status of those services to the default value (0)             *
 *                                                                            *
 * Parameters: triggerids     - [IN] an array of trigger ids to remove        *
 *             triggerids_num - [IN] the number of items in triggerids array  *
 *                                                                            *
 * Return value: SUCCEED - the data was written successfully                  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	DBremove_triggers_from_itservices(trx_uint64_t *triggerids, int triggerids_num)
{
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_vector_ptr_t	updates;
	int			i, ret = FAIL, now;

	if (0 == triggerids_num)
		return SUCCEED;

	now = time(NULL);

	trx_vector_ptr_create(&updates);

	for (i = 0; i < triggerids_num; i++)
		its_updates_append(&updates, triggerids[i], 0, now);

	LOCK_ITSERVICES;

	if (FAIL == its_flush_updates(&updates))
		goto out;

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update services set triggerid=null,showsla=0 where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid", triggerids, triggerids_num);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	trx_free(sql);
out:
	UNLOCK_ITSERVICES;

	trx_vector_ptr_clear_ext(&updates, (trx_clean_func_t)trx_status_update_free);
	trx_vector_ptr_destroy(&updates);

	return ret;
}

int	trx_create_itservices_lock(char **error)
{
	return trx_mutex_create(&itservices_lock, TRX_MUTEX_ITSERVICES, error);
}

void	trx_destroy_itservices_lock(void)
{
	trx_mutex_destroy(&itservices_lock);
}
