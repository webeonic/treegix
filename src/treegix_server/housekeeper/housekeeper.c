

#include "common.h"
#include "db.h"
#include "dbcache.h"
#include "log.h"
#include "daemon.h"
#include "trxself.h"
#include "trxalgo.h"
#include "trxserver.h"

#include "trxhistory.h"
#include "housekeeper.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

static int	hk_period;

#define HK_INITIAL_DELETE_QUEUE_SIZE	4096

/* the maximum number of housekeeping periods to be removed per single housekeeping cycle */
#define HK_MAX_DELETE_PERIODS		4

/* global configuration data containing housekeeping configuration */
static trx_config_t	cfg;

/* Housekeeping rule definition.                                */
/* A housekeeping rule describes table from which records older */
/* than history setting must be removed according to optional   */
/* filter.                                                      */
typedef struct
{
	/* target table name */
	const char	*table;

	/* ID field name, required to select IDs of records that must be deleted */
	char	*field_name;

	/* Optional filter, must be empty string if not used. Only the records matching */
	/* filter are subject to housekeeping procedures.                               */
	const char	*filter;

	/* The oldest record in table (with filter in effect). The min_clock value is   */
	/* read from the database when accessed for the first time and then during      */
	/* housekeeping procedures updated to the last 'cutoff' value.                  */
	int		min_clock;

	/* a reference to the settings value specifying number of seconds the records must be kept */
	int		*phistory;
}
trx_hk_rule_t;

/* housekeeper table => configuration data mapping.                       */
/* This structure is used to map table names used in housekeeper table to */
/* configuration data.                                                    */
typedef struct
{
	/* housekeeper table name */
	const char		*name;

	/* a reference to housekeeping configuration enable value for this table */
	unsigned char		*poption_mode;

	/* a reference to the housekeeping configuration overwrite option for this table */
	unsigned char		*poption_global;
}
trx_hk_cleanup_table_t;

static unsigned char poption_mode_regular 	= TRX_HK_MODE_REGULAR;
static unsigned char poption_global_disabled	= TRX_HK_OPTION_DISABLED;

/* Housekeeper table mapping to housekeeping configuration values.    */
/* This mapping is used to exclude disabled tables from housekeeping  */
/* cleanup procedure.                                                 */
static trx_hk_cleanup_table_t	hk_cleanup_tables[] = {
	{"history",		&cfg.hk.history_mode,	&cfg.hk.history_global},
	{"history_log",		&cfg.hk.history_mode,	&cfg.hk.history_global},
	{"history_str",		&cfg.hk.history_mode,	&cfg.hk.history_global},
	{"history_text",	&cfg.hk.history_mode,	&cfg.hk.history_global},
	{"history_uint",	&cfg.hk.history_mode,	&cfg.hk.history_global},
	{"trends",		&cfg.hk.trends_mode,	&cfg.hk.trends_global},
	{"trends_uint",		&cfg.hk.trends_mode,	&cfg.hk.trends_global},
	/* force events housekeeping mode on to perform problem cleanup when events housekeeping is disabled */
	{"events",		&poption_mode_regular,	&poption_global_disabled},
	{NULL}
};

/* trends table offsets in the hk_cleanup_tables[] mapping  */
#define HK_UPDATE_CACHE_OFFSET_TREND_FLOAT	ITEM_VALUE_TYPE_MAX
#define HK_UPDATE_CACHE_OFFSET_TREND_UINT	(HK_UPDATE_CACHE_OFFSET_TREND_FLOAT + 1)
#define HK_UPDATE_CACHE_TREND_COUNT		2

/* the oldest record timestamp cache for items in history tables */
typedef struct
{
	trx_uint64_t	itemid;
	int		min_clock;
}
trx_hk_item_cache_t;

/* Delete queue item definition.                                     */
/* The delete queue item defines an item that should be processed by */
/* housekeeping procedure (records older than min_clock seconds      */
/* must be removed from database).                                   */
typedef struct
{
	trx_uint64_t	itemid;
	int		min_clock;
}
trx_hk_delete_queue_t;

/* this structure is used to remove old records from history (trends) tables */
typedef struct
{
	/* the target table name */
	const char		*table;

	/* history setting field name in items table (history|trends) */
	const char		*history;

	/* a reference to the housekeeping configuration mode (enable) option for this table */
	unsigned char		*poption_mode;

	/* a reference to the housekeeping configuration overwrite option for this table */
	unsigned char		*poption_global;

	/* a reference to the housekeeping configuration history value for this table */
	int			*poption;

	/* type for checking which values are sent to the history storage */
	unsigned char		type;

	/* the oldest item record timestamp cache for target table */
	trx_hashset_t		item_cache;

	/* the item delete queue */
	trx_vector_ptr_t	delete_queue;
}
trx_hk_history_rule_t;

/* The history item rules, used for housekeeping history and trends tables */
/* The order of the rules must match the order of value types in trx_item_value_type_t. */
static trx_hk_history_rule_t	hk_history_rules[] = {
	{.table = "history",		.history = "history",	.poption_mode = &cfg.hk.history_mode,
			.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
			.type = ITEM_VALUE_TYPE_FLOAT},
	{.table = "history_str",	.history = "history",	.poption_mode = &cfg.hk.history_mode,
			.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
			.type = ITEM_VALUE_TYPE_STR},
	{.table = "history_log",	.history = "history",	.poption_mode = &cfg.hk.history_mode,
			.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
			.type = ITEM_VALUE_TYPE_LOG},
	{.table = "history_uint",	.history = "history",	.poption_mode = &cfg.hk.history_mode,
			.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
			.type = ITEM_VALUE_TYPE_UINT64},
	{.table = "history_text",	.history = "history",	.poption_mode = &cfg.hk.history_mode,
			.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
			.type = ITEM_VALUE_TYPE_TEXT},
	{.table = "trends",		.history = "trends",	.poption_mode = &cfg.hk.trends_mode,
			.poption_global = &cfg.hk.trends_global,	.poption = &cfg.hk.trends,
			.type = ITEM_VALUE_TYPE_FLOAT},
	{.table = "trends_uint",	.history = "trends",	.poption_mode = &cfg.hk.trends_mode,
			.poption_global = &cfg.hk.trends_global,	.poption = &cfg.hk.trends,
			.type = ITEM_VALUE_TYPE_UINT64},
	{NULL}
};

static void	trx_housekeeper_sigusr_handler(int flags)
{
	if (TRX_RTC_HOUSEKEEPER_EXECUTE == TRX_RTC_GET_MSG(flags))
	{
		if (0 < trx_sleep_get_remainder())
		{
			treegix_log(LOG_LEVEL_WARNING, "forced execution of the housekeeper");
			trx_wakeup();
		}
		else
			treegix_log(LOG_LEVEL_WARNING, "housekeeping procedure is already in progress");
	}
}

/******************************************************************************
 *                                                                            *
 * Function: hk_item_update_cache_compare                                     *
 *                                                                            *
 * Purpose: compare two delete queue items by their itemid                    *
 *                                                                            *
 * Parameters: d1 - [IN] the first delete queue item to compare               *
 *             d2 - [IN] the second delete queue item to compare              *
 *                                                                            *
 * Return value: <0 - the first item is less than the second                  *
 *               >0 - the first item is greater than the second               *
 *               =0 - the items are the same                                  *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 * Comments: this function is used to sort delete queue by itemids            *
 *                                                                            *
 ******************************************************************************/
static int	hk_item_update_cache_compare(const void *d1, const void *d2)
{
	trx_hk_delete_queue_t	*r1 = *(trx_hk_delete_queue_t **)d1;
	trx_hk_delete_queue_t	*r2 = *(trx_hk_delete_queue_t **)d2;

	TRX_RETURN_IF_NOT_EQUAL(r1->itemid, r2->itemid);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: hk_history_delete_queue_append                                   *
 *                                                                            *
 * Purpose: add item to the delete queue if necessary                         *
 *                                                                            *
 * Parameters: rule        - [IN/OUT] the history housekeeping rule           *
 *             now         - [IN] the current timestamp                       *
 *             item_record - [IN/OUT] the record from item cache containing   *
 *                           item to process and its oldest record timestamp  *
 *             history     - [IN] a number of seconds the history data for    *
 *                           item_record must be kept.                        *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 * Comments: If item is added to delete queue, its oldest record timestamp    *
 *           (min_clock) is updated to the calculated 'cutoff' value.         *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_delete_queue_append(trx_hk_history_rule_t *rule, int now,
		trx_hk_item_cache_t *item_record, int history)
{
	int	keep_from;

	if (history > now)
		return;	/* there shouldn't be any records with negative timestamps, nothing to do */

	keep_from = now - history;

	if (keep_from > item_record->min_clock)
	{
		trx_hk_delete_queue_t	*update_record;

		/* update oldest timestamp in item cache */
		item_record->min_clock = MIN(keep_from, item_record->min_clock + HK_MAX_DELETE_PERIODS * hk_period);

		update_record = (trx_hk_delete_queue_t *)trx_malloc(NULL, sizeof(trx_hk_delete_queue_t));
		update_record->itemid = item_record->itemid;
		update_record->min_clock = item_record->min_clock;
		trx_vector_ptr_append(&rule->delete_queue, update_record);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: hk_history_prepare                                               *
 *                                                                            *
 * Purpose: prepares history housekeeping rule                                *
 *                                                                            *
 * Parameters: rule        - [IN/OUT] the history housekeeping rule           *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 * Comments: This function is called to initialize history rule data either   *
 *           at start or when housekeeping is enabled for this rule.          *
 *           It caches item history data and also prepares delete queue to be *
 *           processed during the first run.                                  *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_prepare(trx_hk_history_rule_t *rule)
{
	DB_RESULT	result;
	DB_ROW		row;

	trx_hashset_create(&rule->item_cache, 1024, trx_default_uint64_hash_func, trx_default_uint64_compare_func);

	trx_vector_ptr_create(&rule->delete_queue);
	trx_vector_ptr_reserve(&rule->delete_queue, HK_INITIAL_DELETE_QUEUE_SIZE);

	result = DBselect("select itemid,min(clock) from %s group by itemid", rule->table);

	while (NULL != (row = DBfetch(result)))
	{
		trx_uint64_t		itemid;
		int			min_clock;
		trx_hk_item_cache_t	item_record;

		TRX_STR2UINT64(itemid, row[0]);
		min_clock = atoi(row[1]);

		item_record.itemid = itemid;
		item_record.min_clock = min_clock;

		trx_hashset_insert(&rule->item_cache, &item_record, sizeof(trx_hk_item_cache_t));
	}

	DBfree_result(result);
}

/******************************************************************************
 *                                                                            *
 * Function: hk_history_release                                               *
 *                                                                            *
 * Purpose: releases history housekeeping rule                                *
 *                                                                            *
 * Parameters: rule  - [IN/OUT] the history housekeeping rule                 *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 * Comments: This function is called to release resources allocated by        *
 *           history housekeeping rule after housekeeping was disabled        *
 *           for the table referred by this rule.                             *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_release(trx_hk_history_rule_t *rule)
{
	if (0 == rule->item_cache.num_slots)
		return;

	trx_hashset_destroy(&rule->item_cache);
	trx_vector_ptr_destroy(&rule->delete_queue);
}

/******************************************************************************
 *                                                                            *
 * Function: hk_history_item_update                                           *
 *                                                                            *
 * Purpose: updates history housekeeping rule with item history setting and   *
 *          adds item to the delete queue if necessary                        *
 *                                                                            *
 * Parameters: rule    - [IN/OUT] the history housekeeping rule               *
 *             now     - [IN] the current timestamp                           *
 *             itemid  - [IN] the item to update                              *
 *             history - [IN] the number of seconds the item data             *
 *                       should be kept in history                            *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_item_update(trx_hk_history_rule_t *rules, trx_hk_history_rule_t *rule_add, int count,
		int now, trx_uint64_t itemid, int history)
{
	trx_hk_history_rule_t	*rule;

	/* item can be cached in multiple rules when value type has been changed */
	for (rule = rules; rule - rules < count; rule++)
	{
		trx_hk_item_cache_t	*item_record;

		if (0 == rule->item_cache.num_slots)
			continue;

		if (NULL == (item_record = (trx_hk_item_cache_t *)trx_hashset_search(&rule->item_cache, &itemid)))
		{
			trx_hk_item_cache_t	item_data = {itemid, now};

			if (rule_add != rule)
				continue;

			if (NULL == (item_record = (trx_hk_item_cache_t *)trx_hashset_insert(&rule->item_cache,
					&item_data, sizeof(trx_hk_item_cache_t))))
			{
				continue;
			}
		}

		hk_history_delete_queue_append(rule, now, item_record, history);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: hk_history_update                                                *
 *                                                                            *
 * Purpose: updates history housekeeping rule with the latest item history    *
 *          settings and prepares delete queue                                *
 *                                                                            *
 * Parameters: rule  - [IN/OUT] the history housekeeping rule                 *
 *             now   - [IN] the current timestamp                             *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_update(trx_hk_history_rule_t *rules, int now)
{
	DB_RESULT	result;
	DB_ROW		row;
	char		*tmp = NULL;

	result = DBselect(
			"select i.itemid,i.value_type,i.history,i.trends,h.hostid"
			" from items i,hosts h"
			" where i.flags in (%d,%d)"
				" and i.hostid=h.hostid"
				" and h.status in (%d,%d)",
			TRX_FLAG_DISCOVERY_NORMAL, TRX_FLAG_DISCOVERY_CREATED,
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED);

	while (NULL != (row = DBfetch(result)))
	{
		trx_uint64_t		itemid, hostid;
		int			history, trends, value_type;
		trx_hk_history_rule_t	*rule;

		TRX_STR2UINT64(itemid, row[0]);
		value_type = atoi(row[1]);
		TRX_STR2UINT64(hostid, row[4]);

		if (value_type < ITEM_VALUE_TYPE_MAX &&
				TRX_HK_MODE_REGULAR == *(rule = rules + value_type)->poption_mode)
		{
			tmp = trx_strdup(tmp, row[2]);
			substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL, &tmp,
					MACRO_TYPE_COMMON, NULL, 0);

			if (SUCCEED != is_time_suffix(tmp, &history, TRX_LENGTH_UNLIMITED))
			{
				treegix_log(LOG_LEVEL_WARNING, "invalid history storage period '%s' for itemid '%s'",
						tmp, row[0]);
				continue;
			}

			if (0 != history && (TRX_HK_HISTORY_MIN > history || TRX_HK_PERIOD_MAX < history))
			{
				treegix_log(LOG_LEVEL_WARNING, "invalid history storage period for itemid '%s'", row[0]);
				continue;
			}

			if (0 != history && TRX_HK_OPTION_DISABLED != *rule->poption_global)
				history = *rule->poption;

			hk_history_item_update(rules, rule, ITEM_VALUE_TYPE_MAX, now, itemid, history);
		}

		if (ITEM_VALUE_TYPE_FLOAT == value_type || ITEM_VALUE_TYPE_UINT64 == value_type)
		{
			rule = rules + (value_type == ITEM_VALUE_TYPE_FLOAT ?
					HK_UPDATE_CACHE_OFFSET_TREND_FLOAT : HK_UPDATE_CACHE_OFFSET_TREND_UINT);

			if (TRX_HK_MODE_REGULAR != *rule->poption_mode)
				continue;

			tmp = trx_strdup(tmp, row[3]);
			substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL, &tmp,
					MACRO_TYPE_COMMON, NULL, 0);

			if (SUCCEED != is_time_suffix(tmp, &trends, TRX_LENGTH_UNLIMITED))
			{
				treegix_log(LOG_LEVEL_WARNING, "invalid trends storage period '%s' for itemid '%s'",
						tmp, row[0]);
				continue;
			}
			else if (0 != trends && (TRX_HK_TRENDS_MIN > trends || TRX_HK_PERIOD_MAX < trends))
			{
				treegix_log(LOG_LEVEL_WARNING, "invalid trends storage period for itemid '%s'", row[0]);
				continue;
			}

			if (0 != trends && TRX_HK_OPTION_DISABLED != *rule->poption_global)
				trends = *rule->poption;

			hk_history_item_update(rules + HK_UPDATE_CACHE_OFFSET_TREND_FLOAT, rule,
					HK_UPDATE_CACHE_TREND_COUNT, now, itemid, trends);
		}
	}
	DBfree_result(result);

	trx_free(tmp);
}

/******************************************************************************
 *                                                                            *
 * Function: hk_history_delete_queue_prepare_all                              *
 *                                                                            *
 * Purpose: prepares history housekeeping delete queues for all defined       *
 *          history rules.                                                    *
 *                                                                            *
 * Parameters: rules  - [IN/OUT] the history housekeeping rules               *
 *             now    - [IN] the current timestamp                            *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 * Comments: This function also handles history rule initializing/releasing   *
 *           when the rule just became enabled/disabled.                      *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_delete_queue_prepare_all(trx_hk_history_rule_t *rules, int now)
{
	trx_hk_history_rule_t	*rule;
	unsigned char		items_update = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* prepare history item cache (hashset containing itemid:min_clock values) */
	for (rule = rules; NULL != rule->table; rule++)
	{
		if (TRX_HK_MODE_REGULAR == *rule->poption_mode)
		{
			if (0 == rule->item_cache.num_slots)
				hk_history_prepare(rule);

			items_update = 1;
		}
		else if (0 != rule->item_cache.num_slots)
			hk_history_release(rule);
	}

	/* Since we maintain two separate global period settings - for history and for trends */
	/* we need to scan items table if either of these is off. Thus setting both global periods */
	/* to override is very beneficial for performance. */
	if (0 != items_update)
		hk_history_update(rules, now);	/* scan items and update min_clock using per item settings */

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: hk_history_delete_queue_clear                                    *
 *                                                                            *
 * Purpose: clears the history housekeeping delete queue                      *
 *                                                                            *
 * Parameters: rule   - [IN/OUT] the history housekeeping rule                *
 *             now    - [IN] the current timestamp                            *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_delete_queue_clear(trx_hk_history_rule_t *rule)
{
	trx_vector_ptr_clear_ext(&rule->delete_queue, trx_ptr_free);
}

/******************************************************************************
 *                                                                            *
 * Function: hk_drop_partition_for_rule                                       *
 *                                                                            *
 * Purpose: drop appropriate partitions from the history and trends tables    *
 *                                                                            *
 * Parameters: rules - [IN/OUT] history housekeeping rules                    *
 *             now   - [IN] the current timestamp                             *
 *                                                                            *
 * Return value: the number of tables processed                               *
 *                                                                            *
 ******************************************************************************/
static void	hk_drop_partition_for_rule(trx_hk_history_rule_t *rule, int now)
{
	int		keep_from, history_seconds;
	DB_RESULT	result;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() now:%d", __func__, now);

	history_seconds = *rule->poption;

	if (TRX_HK_HISTORY_MIN > history_seconds || TRX_HK_PERIOD_MAX < history_seconds)
	{
		treegix_log(LOG_LEVEL_WARNING, "invalid history storage period for table '%s'", rule->table);
		return;
	}

	keep_from = now - history_seconds;
	treegix_log(LOG_LEVEL_TRACE, "%s: table=%s keep_from=%d", __func__, rule->table, keep_from);

	result = DBselect("SELECT drop_chunks(%d,'%s')", keep_from, rule->table);

	if (NULL == result)
		treegix_log(LOG_LEVEL_ERR, "cannot drop chunks for %s", rule->table);
	else
		DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return;
}

/******************************************************************************
 *                                                                            *
 * Function: housekeeping_history_and_trends                                  *
 *                                                                            *
 * Purpose: performs housekeeping for history and trends tables               *
 *                                                                            *
 * Parameters: now    - [IN] the current timestamp                            *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
static int	housekeeping_history_and_trends(int now)
{
	int			deleted = 0, i, rc;
	trx_hk_history_rule_t	*rule;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() now:%d", __func__, now);

	/* prepare delete queues for all history housekeeping rules */
	hk_history_delete_queue_prepare_all(hk_history_rules, now);

	/* Loop through the history rules. Each rule is a history table (such as history_log, trends_uint, etc) */
	/* we need to clear records from */
	for (rule = hk_history_rules; NULL != rule->table; rule++)
	{
		if (TRX_HK_MODE_DISABLED == *rule->poption_mode)
			continue;

		/* If partitioning enabled for history and/or trends then drop partitions with expired history.  */
		/* TRX_HK_MODE_PARTITION is set during configuration sync based on the following: */
		/* 1. "Override item history (or trend) period" must be on 2. DB must be PostgreSQL */
		/* 3. config.db_extension must be set to "timescaledb" */
		if (TRX_HK_MODE_PARTITION == *rule->poption_mode)
		{
			hk_drop_partition_for_rule(rule, now);
			continue;
		}

		/* process delete queue for the housekeeping rule */

		trx_vector_ptr_sort(&rule->delete_queue, hk_item_update_cache_compare);

		for (i = 0; i < rule->delete_queue.values_num; i++)
		{
			trx_hk_delete_queue_t	*item_record = (trx_hk_delete_queue_t *)rule->delete_queue.values[i];

			rc = DBexecute("delete from %s where itemid=" TRX_FS_UI64 " and clock<%d",
					rule->table, item_record->itemid, item_record->min_clock);
			if (TRX_DB_OK < rc)
				deleted += rc;
		}

		/* clear history rule delete queue so it's ready for the next housekeeping cycle */
		hk_history_delete_queue_clear(rule);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, deleted);

	return deleted;
}

/******************************************************************************
 *                                                                            *
 * Function: housekeeping_process_rule                                        *
 *                                                                            *
 * Purpose: removes old records from a table according to the specified rule  *
 *                                                                            *
 * Parameters: now  - [IN] the current time in seconds                        *
 *             rule - [IN/OUT] the housekeeping rule specifying table to      *
 *                    clean and the required data (fields, filters, time)     *
 *                                                                            *
 * Return value: the number of deleted records                                *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
static int	housekeeping_process_rule(int now, trx_hk_rule_t *rule)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		keep_from, deleted = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() table:'%s' field_name:'%s' filter:'%s' min_clock:%d now:%d",
			__func__, rule->table, rule->field_name, rule->filter, rule->min_clock, now);

	/* initialize min_clock with the oldest record timestamp from database */
	if (0 == rule->min_clock)
	{
		result = DBselect("select min(clock) from %s%s%s", rule->table,
				('\0' != *rule->filter ? " where " : ""), rule->filter);
		if (NULL != (row = DBfetch(result)) && SUCCEED != DBis_null(row[0]))
			rule->min_clock = atoi(row[0]);
		else
			rule->min_clock = now;

		DBfree_result(result);
	}

	/* Delete the old records from database. Don't remove more than 4 x housekeeping */
	/* periods worth of data to prevent database stalling.                           */
	keep_from = now - *rule->phistory;
	if (keep_from > rule->min_clock)
	{
		char			buffer[MAX_STRING_LEN];
		char			*sql = NULL;
		size_t			sql_alloc = 0, sql_offset = 0;
		trx_vector_uint64_t	ids;
		int			ret;

		trx_vector_uint64_create(&ids);

		rule->min_clock = MIN(keep_from, rule->min_clock + HK_MAX_DELETE_PERIODS * hk_period);

		trx_snprintf(buffer, sizeof(buffer),
			"select %s"
			" from %s"
			" where clock<%d%s%s"
			" order by %s",
			rule->field_name, rule->table, rule->min_clock, '\0' != *rule->filter ? " and " : "",
			rule->filter, rule->field_name);

		while (1)
		{
			/* Select IDs of records that must be deleted, this allows to avoid locking for every   */
			/* record the search encounters when using delete statement, thus eliminates deadlocks. */
			if (0 == CONFIG_MAX_HOUSEKEEPER_DELETE)
				result = DBselect("%s", buffer);
			else
				result = DBselectN(buffer, CONFIG_MAX_HOUSEKEEPER_DELETE);

			while (NULL != (row = DBfetch(result)))
			{
				trx_uint64_t	id;

				TRX_STR2UINT64(id, row[0]);
				trx_vector_uint64_append(&ids, id);
			}
			DBfree_result(result);

			if (0 == ids.values_num)
				break;

			sql_offset = 0;
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from %s where", rule->table);
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, rule->field_name, ids.values,
					ids.values_num);

			if (TRX_DB_OK > (ret = DBexecute("%s", sql)))
				break;

			deleted += ret;
			trx_vector_uint64_clear(&ids);
		}

		trx_free(sql);
		trx_vector_uint64_destroy(&ids);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, deleted);

	return deleted;
}

/******************************************************************************
 *                                                                            *
 * Function: DBdelete_from_table                                              *
 *                                                                            *
 * Purpose: delete limited count of rows from table                           *
 *                                                                            *
 * Return value: number of deleted rows or less than 0 if an error occurred   *
 *                                                                            *
 ******************************************************************************/
static int	DBdelete_from_table(const char *tablename, const char *filter, int limit)
{
	if (0 == limit)
	{
		return DBexecute(
				"delete from %s"
				" where %s",
				tablename,
				filter);
	}
	else
	{
#if defined(HAVE_IBM_DB2) || defined(HAVE_ORACLE)
		return DBexecute(
				"delete from %s"
				" where %s"
					" and rownum<=%d",
				tablename,
				filter,
				limit);
#elif defined(HAVE_MYSQL)
		return DBexecute(
				"delete from %s"
				" where %s limit %d",
				tablename,
				filter,
				limit);
#elif defined(HAVE_POSTGRESQL)
		return DBexecute(
				"delete from %s"
				" where %s and ctid = any(array(select ctid from %s"
					" where %s limit %d))",
				tablename,
				filter,
				tablename,
				filter,
				limit);
#elif defined(HAVE_SQLITE3)
		return DBexecute(
				"delete from %s"
				" where %s",
				tablename,
				filter);
#endif
	}

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: hk_problem_cleanup                                               *
 *                                                                            *
 * Purpose: perform problem table cleanup                                     *
 *                                                                            *
 * Parameters: table    - [IN] the problem table name                         *
 * Parameters: source   - [IN] the event source                               *
 *             object   - [IN] the event object type                          *
 *             objectid - [IN] the event object identifier                    *
 *             more     - [OUT] 1 if there might be more data to remove,      *
 *                              otherwise the value is not changed            *
 *                                                                            *
 * Return value: number of rows deleted                                       *
 *                                                                            *
 ******************************************************************************/
static int	hk_problem_cleanup(const char *table, int source, int object, trx_uint64_t objectid, int *more)
{
	char	filter[MAX_STRING_LEN];
	int	ret;

	trx_snprintf(filter, sizeof(filter), "source=%d and object=%d and objectid=" TRX_FS_UI64,
			source, object, objectid);

	ret = DBdelete_from_table(table, filter, CONFIG_MAX_HOUSEKEEPER_DELETE);

	if (TRX_DB_OK > ret || (0 != CONFIG_MAX_HOUSEKEEPER_DELETE && ret >= CONFIG_MAX_HOUSEKEEPER_DELETE))
		*more = 1;

	return TRX_DB_OK <= ret ? ret : 0;
}

/******************************************************************************
 *                                                                            *
 * Function: hk_table_cleanup                                                 *
 *                                                                            *
 * Purpose: perform generic table cleanup                                     *
 *                                                                            *
 * Parameters: table    - [IN] the table name                                 *
 *             field    - [IN] the field name                                 *
 *             objectid - [IN] the field value                                *
 *             more     - [OUT] 1 if there might be more data to remove,      *
 *                              otherwise the value is not changed            *
 *                                                                            *
 * Return value: number of rows deleted                                       *
 *                                                                            *
 ******************************************************************************/
static int	hk_table_cleanup(const char *table, const char *field, trx_uint64_t id, int *more)
{
	char	filter[MAX_STRING_LEN];
	int	ret;

	trx_snprintf(filter, sizeof(filter), "%s=" TRX_FS_UI64, field, id);

	ret = DBdelete_from_table(table, filter, CONFIG_MAX_HOUSEKEEPER_DELETE);

	if (TRX_DB_OK > ret || (0 != CONFIG_MAX_HOUSEKEEPER_DELETE && ret >= CONFIG_MAX_HOUSEKEEPER_DELETE))
		*more = 1;

	return TRX_DB_OK <= ret ? ret : 0;
}

/******************************************************************************
 *                                                                            *
 * Function: housekeeping_cleanup                                             *
 *                                                                            *
 * Purpose: remove deleted items/triggers data                                *
 *                                                                            *
 * Return value: number of rows deleted                                       *
 *                                                                            *
 * Author: Alexei Vladishev, Dmitry Borovikov                                 *
 *                                                                            *
 * Comments: sqlite3 does not use CONFIG_MAX_HOUSEKEEPER_DELETE, deletes all  *
 *                                                                            *
 ******************************************************************************/
static int	housekeeping_cleanup(void)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			deleted = 0;
	trx_vector_uint64_t	housekeeperids;
	char			*sql = NULL, *table_name_esc;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_hk_cleanup_table_t *table;
	trx_uint64_t		housekeeperid, objectid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&housekeeperids);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select housekeeperid,tablename,field,value"
			" from housekeeper"
			" where tablename in (");

	/* assemble list of tables included in the housekeeping procedure */
	for (table = hk_cleanup_tables; NULL != table->name; table++)
	{
		if (TRX_HK_MODE_REGULAR != *table->poption_mode || TRX_HK_OPTION_ENABLED == *table->poption_global)
			continue;

		table_name_esc = DBdyn_escape_string(table->name);

		trx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, '\'');
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table_name_esc);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "',");

		trx_free(table_name_esc);
	}
	sql_offset--;

	/* order by tablename to effectively use DB cache */
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ") order by tablename");

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		int	more = 0;

		TRX_STR2UINT64(housekeeperid, row[0]);
		TRX_STR2UINT64(objectid, row[3]);

		if (0 == strcmp(row[1], "events")) /* events name is used for backwards compatibility with frontend */
		{
			const char	*table_name = "problem";

			if (0 == strcmp(row[2], "triggerid"))
			{
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER,
						objectid, &more);
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_TRIGGER,
						objectid, &more);
			}
			else if (0 == strcmp(row[2], "itemid"))
			{
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_ITEM,
						objectid, &more);
			}
			else if (0 == strcmp(row[2], "lldruleid"))
			{
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_LLDRULE,
						objectid, &more);
			}
		}
		else
			deleted += hk_table_cleanup(row[1], row[2], objectid, &more);

		if (0 == more)
			trx_vector_uint64_append(&housekeeperids, housekeeperid);
	}
	DBfree_result(result);

	if (0 != housekeeperids.values_num)
	{
		trx_vector_uint64_sort(&housekeeperids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
		DBexecute_multiple_query("delete from housekeeper where", "housekeeperid", &housekeeperids);
	}

	trx_free(sql);

	trx_vector_uint64_destroy(&housekeeperids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, deleted);

	return deleted;
}

static int	housekeeping_sessions(int now)
{
	int	deleted = 0, rc;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() now:%d", __func__, now);

	if (TRX_HK_OPTION_ENABLED == cfg.hk.sessions_mode)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "lastaccess<%d", now - cfg.hk.sessions);
		rc = DBdelete_from_table("sessions", sql, CONFIG_MAX_HOUSEKEEPER_DELETE);
		trx_free(sql);

		if (TRX_DB_OK <= rc)
			deleted = rc;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, deleted);

	return deleted;
}

static int	housekeeping_services(int now)
{
	static trx_hk_rule_t	rule = {"service_alarms", "servicealarmid", "", 0, &cfg.hk.services};

	if (TRX_HK_OPTION_ENABLED == cfg.hk.services_mode)
		return housekeeping_process_rule(now, &rule);

	return 0;
}

static int	housekeeping_audit(int now)
{
	static trx_hk_rule_t	rule = {"auditlog", "auditid", "", 0, &cfg.hk.audit};

	if (TRX_HK_OPTION_ENABLED == cfg.hk.audit_mode)
		return housekeeping_process_rule(now, &rule);

	return 0;
}

static int	housekeeping_events(int now)
{
#define TRX_HK_EVENT_RULE	" and not exists (select null from problem where events.eventid=problem.eventid)" \
				" and not exists (select null from problem where events.eventid=problem.r_eventid)"

	static trx_hk_rule_t	rules[] = {
		{"events", "eventid", "events.source=" TRX_STR(EVENT_SOURCE_TRIGGERS)
			" and events.object=" TRX_STR(EVENT_OBJECT_TRIGGER)
			TRX_HK_EVENT_RULE, 0, &cfg.hk.events_trigger},
		{"events", "eventid", "events.source=" TRX_STR(EVENT_SOURCE_INTERNAL)
			" and events.object=" TRX_STR(EVENT_OBJECT_TRIGGER)
			TRX_HK_EVENT_RULE, 0, &cfg.hk.events_internal},
		{"events", "eventid", "events.source=" TRX_STR(EVENT_SOURCE_INTERNAL)
			" and events.object=" TRX_STR(EVENT_OBJECT_ITEM)
			TRX_HK_EVENT_RULE, 0, &cfg.hk.events_internal},
		{"events", "eventid", "events.source=" TRX_STR(EVENT_SOURCE_INTERNAL)
			" and events.object=" TRX_STR(EVENT_OBJECT_LLDRULE)
			TRX_HK_EVENT_RULE, 0, &cfg.hk.events_internal},
		{"events", "eventid", "events.source=" TRX_STR(EVENT_SOURCE_DISCOVERY)
			" and events.object=" TRX_STR(EVENT_OBJECT_DHOST), 0, &cfg.hk.events_discovery},
		{"events", "eventid", "events.source=" TRX_STR(EVENT_SOURCE_DISCOVERY)
			" and events.object=" TRX_STR(EVENT_OBJECT_DSERVICE), 0, &cfg.hk.events_discovery},
		{"events", "eventid", "events.source=" TRX_STR(EVENT_SOURCE_AUTO_REGISTRATION)
			" and events.object=" TRX_STR(EVENT_OBJECT_TREEGIX_ACTIVE), 0, &cfg.hk.events_autoreg},
		{NULL}
	};

	int		deleted = 0;
	trx_hk_rule_t	*rule;

	if (TRX_HK_OPTION_ENABLED != cfg.hk.events_mode)
		return 0;

	for (rule = rules; NULL != rule->table; rule++)
		deleted += housekeeping_process_rule(now, rule);

	return deleted;
#undef TRX_HK_EVENT_RULE
}

static int	housekeeping_problems(int now)
{
	int	deleted = 0, rc;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() now:%d", __func__, now);

	rc = DBexecute("delete from problem where r_clock<>0 and r_clock<%d", now - SEC_PER_DAY);

	if (TRX_DB_OK <= rc)
		deleted = rc;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, deleted);

	return deleted;
}

static int	housekeeping_proxy_dhistory(int now)
{
	int	deleted = 0, rc;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() now:%d", __func__, now);

	rc = DBexecute("delete from proxy_dhistory where clock<%d", now - SEC_PER_DAY);

	if (TRX_DB_OK <= rc)
		deleted = rc;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, deleted);

	return deleted;
}


static int	get_housekeeping_period(double time_slept)
{
	if (SEC_PER_HOUR > time_slept)
		return SEC_PER_HOUR;
	else if (24 * SEC_PER_HOUR < time_slept)
		return 24 * SEC_PER_HOUR;
	else
		return (int)time_slept;
}

TRX_THREAD_ENTRY(housekeeper_thread, args)
{
	int	now, d_history_and_trends, d_cleanup, d_events, d_problems, d_sessions, d_services, d_audit, sleeptime,
		records;
	double	sec, time_slept, time_now;
	char	sleeptext[25];

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	if (0 == CONFIG_HOUSEKEEPING_FREQUENCY)
	{
		trx_setproctitle("%s [waiting for user command]", get_process_type_string(process_type));
		trx_snprintf(sleeptext, sizeof(sleeptext), "waiting for user command");
	}
	else
	{
		sleeptime = HOUSEKEEPER_STARTUP_DELAY * SEC_PER_MIN;
		trx_setproctitle("%s [startup idle for %d minutes]", get_process_type_string(process_type),
				HOUSEKEEPER_STARTUP_DELAY);
		trx_snprintf(sleeptext, sizeof(sleeptext), "idle for %d hour(s)", CONFIG_HOUSEKEEPING_FREQUENCY);
	}

	trx_set_sigusr_handler(trx_housekeeper_sigusr_handler);

	while (TRX_IS_RUNNING())
	{
		sec = trx_time();

		if (0 == CONFIG_HOUSEKEEPING_FREQUENCY)
			trx_sleep_forever();
		else
			trx_sleep_loop(sleeptime);

		if (!TRX_IS_RUNNING())
			break;

		time_now = trx_time();
		time_slept = time_now - sec;
		trx_update_env(time_now);

		hk_period = get_housekeeping_period(time_slept);

		treegix_log(LOG_LEVEL_WARNING, "executing housekeeper");

		now = time(NULL);

		trx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));
		DBconnect(TRX_DB_CONNECT_NORMAL);

		trx_config_get(&cfg, TRX_CONFIG_FLAGS_HOUSEKEEPER | TRX_CONFIG_FLAGS_DB_EXTENSION);

		trx_setproctitle("%s [removing old history and trends]",
				get_process_type_string(process_type));
		sec = trx_time();
		d_history_and_trends = housekeeping_history_and_trends(now);

		trx_setproctitle("%s [removing old problems]", get_process_type_string(process_type));
		d_problems = housekeeping_problems(now);

		trx_setproctitle("%s [removing old events]", get_process_type_string(process_type));
		d_events = housekeeping_events(now);

		trx_setproctitle("%s [removing old sessions]", get_process_type_string(process_type));
		d_sessions = housekeeping_sessions(now);

		trx_setproctitle("%s [removing old service alarms]", get_process_type_string(process_type));
		d_services = housekeeping_services(now);

		trx_setproctitle("%s [removing old audit log items]", get_process_type_string(process_type));
		d_audit = housekeeping_audit(now);

		trx_setproctitle("%s [removing old records]", get_process_type_string(process_type));
		records = housekeeping_proxy_dhistory(now);

		trx_setproctitle("%s [removing deleted items data]", get_process_type_string(process_type));
		d_cleanup = housekeeping_cleanup();
		sec = trx_time() - sec;

		treegix_log(LOG_LEVEL_WARNING, "%s [deleted %d hist/trends, %d items/triggers, %d events, %d problems,"
				" %d sessions, %d alarms, %d audit, %d records in " TRX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), d_history_and_trends, d_cleanup, d_events,
				d_problems, d_sessions, d_services, d_audit, records, sec, sleeptext);

		trx_config_clean(&cfg);

		DBclose();

		trx_dc_cleanup_data_sessions();

		trx_setproctitle("%s [deleted %d hist/trends, %d items/triggers, %d events, %d sessions, %d alarms,"
				" %d audit items, %d records in " TRX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), d_history_and_trends, d_cleanup, d_events,
				d_sessions, d_services, d_audit, records, sec, sleeptext);

		if (0 != CONFIG_HOUSEKEEPING_FREQUENCY)
			sleeptime = CONFIG_HOUSEKEEPING_FREQUENCY * SEC_PER_HOUR;
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
}
