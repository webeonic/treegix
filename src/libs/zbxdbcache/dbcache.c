

#include "common.h"
#include "log.h"
#include "threads.h"

#include "db.h"
#include "dbcache.h"
#include "ipc.h"
#include "mutexs.h"
#include "trxserver.h"
#include "proxy.h"
#include "events.h"
#include "memalloc.h"
#include "trxalgo.h"
#include "valuecache.h"
#include "trxmodules.h"
#include "module.h"
#include "export.h"
#include "trxjson.h"
#include "trxhistory.h"

static trx_mem_info_t	*hc_index_mem = NULL;
static trx_mem_info_t	*hc_mem = NULL;
static trx_mem_info_t	*trend_mem = NULL;

#define	LOCK_CACHE	trx_mutex_lock(cache_lock)
#define	UNLOCK_CACHE	trx_mutex_unlock(cache_lock)
#define	LOCK_TRENDS	trx_mutex_lock(trends_lock)
#define	UNLOCK_TRENDS	trx_mutex_unlock(trends_lock)
#define	LOCK_CACHE_IDS		trx_mutex_lock(cache_ids_lock)
#define	UNLOCK_CACHE_IDS	trx_mutex_unlock(cache_ids_lock)

static trx_mutex_t	cache_lock = TRX_MUTEX_NULL;
static trx_mutex_t	trends_lock = TRX_MUTEX_NULL;
static trx_mutex_t	cache_ids_lock = TRX_MUTEX_NULL;

static char		*sql = NULL;
static size_t		sql_alloc = 64 * TRX_KIBIBYTE;

extern unsigned char	program_type;

#define TRX_IDS_SIZE	8

#define TRX_HC_ITEMS_INIT_SIZE	1000

#define TRX_TRENDS_CLEANUP_TIME	((SEC_PER_HOUR * 55) / 60)

/* the maximum time spent synchronizing history */
#define TRX_HC_SYNC_TIME_MAX	10

/* the maximum number of items in one synchronization batch */
#define TRX_HC_SYNC_MAX		1000
#define TRX_HC_TIMER_MAX	(TRX_HC_SYNC_MAX / 2)

/* the minimum processed item percentage of item candidates to continue synchronizing */
#define TRX_HC_SYNC_MIN_PCNT	10

/* the maximum number of characters for history cache values */
#define TRX_HISTORY_VALUE_LEN	(1024 * 64)

#define TRX_DC_FLAGS_NOT_FOR_HISTORY	(TRX_DC_FLAG_NOVALUE | TRX_DC_FLAG_UNDEF | TRX_DC_FLAG_NOHISTORY)
#define TRX_DC_FLAGS_NOT_FOR_TRENDS	(TRX_DC_FLAG_NOVALUE | TRX_DC_FLAG_UNDEF | TRX_DC_FLAG_NOTRENDS)
#define TRX_DC_FLAGS_NOT_FOR_MODULES	(TRX_DC_FLAGS_NOT_FOR_HISTORY | TRX_DC_FLAG_LLD)
#define TRX_DC_FLAGS_NOT_FOR_EXPORT	(TRX_DC_FLAG_NOVALUE | TRX_DC_FLAG_UNDEF)

typedef struct
{
	char		table_name[TRX_TABLENAME_LEN_MAX];
	trx_uint64_t	lastid;
}
TRX_DC_ID;

typedef struct
{
	TRX_DC_ID	id[TRX_IDS_SIZE];
}
TRX_DC_IDS;

static TRX_DC_IDS	*ids = NULL;

typedef struct
{
	trx_hashset_t		trends;
	TRX_DC_STATS		stats;

	trx_hashset_t		history_items;
	trx_binary_heap_t	history_queue;

	int			history_num;
	int			trends_num;
	int			trends_last_cleanup_hour;
	int			history_num_total;
	int			history_progress_ts;
}
TRX_DC_CACHE;

static TRX_DC_CACHE	*cache = NULL;

/* local history cache */
#define TRX_MAX_VALUES_LOCAL	256
#define TRX_STRUCT_REALLOC_STEP	8
#define TRX_STRING_REALLOC_STEP	TRX_KIBIBYTE

typedef struct
{
	size_t	pvalue;
	size_t	len;
}
dc_value_str_t;

typedef struct
{
	double		value_dbl;
	trx_uint64_t	value_uint;
	dc_value_str_t	value_str;
}
dc_value_t;

typedef struct
{
	trx_uint64_t	itemid;
	dc_value_t	value;
	trx_timespec_t	ts;
	dc_value_str_t	source;		/* for log items only */
	trx_uint64_t	lastlogsize;
	int		timestamp;	/* for log items only */
	int		severity;	/* for log items only */
	int		logeventid;	/* for log items only */
	int		mtime;
	unsigned char	item_value_type;
	unsigned char	value_type;
	unsigned char	state;
	unsigned char	flags;		/* see TRX_DC_FLAG_* above */
}
dc_item_value_t;

static char		*string_values = NULL;
static size_t		string_values_alloc = 0, string_values_offset = 0;
static dc_item_value_t	*item_values = NULL;
static size_t		item_values_alloc = 0, item_values_num = 0;

static void	hc_add_item_values(dc_item_value_t *values, int values_num);
static void	hc_pop_items(trx_vector_ptr_t *history_items);
static void	hc_get_item_values(TRX_DC_HISTORY *history, trx_vector_ptr_t *history_items);
static void	hc_push_items(trx_vector_ptr_t *history_items);
static void	hc_free_item_values(TRX_DC_HISTORY *history, int history_num);
static void	hc_queue_item(trx_hc_item_t *item);
static int	hc_queue_elem_compare_func(const void *d1, const void *d2);
static int	hc_queue_get_size(void);

/******************************************************************************
 *                                                                            *
 * Function: DCget_stats_all                                                  *
 *                                                                            *
 * Purpose: retrieves all internal metrics of the database cache              *
 *                                                                            *
 * Parameters: stats - [OUT] write cache metrics                              *
 *                                                                            *
 ******************************************************************************/
void	DCget_stats_all(trx_wcache_info_t *wcache_info)
{
	LOCK_CACHE;

	wcache_info->stats = cache->stats;
	wcache_info->history_free = hc_mem->free_size;
	wcache_info->history_total = hc_mem->total_size;
	wcache_info->index_free = hc_index_mem->free_size;
	wcache_info->index_total = hc_index_mem->total_size;

	if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
	{
		wcache_info->trend_free = trend_mem->free_size;
		wcache_info->trend_total = trend_mem->orig_size;
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_stats                                                      *
 *                                                                            *
 * Purpose: get statistics of the database cache                              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
void	*DCget_stats(int request)
{
	static trx_uint64_t	value_uint;
	static double		value_double;
	void			*ret;

	LOCK_CACHE;

	switch (request)
	{
		case TRX_STATS_HISTORY_COUNTER:
			value_uint = cache->stats.history_counter;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_HISTORY_FLOAT_COUNTER:
			value_uint = cache->stats.history_float_counter;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_HISTORY_UINT_COUNTER:
			value_uint = cache->stats.history_uint_counter;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_HISTORY_STR_COUNTER:
			value_uint = cache->stats.history_str_counter;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_HISTORY_LOG_COUNTER:
			value_uint = cache->stats.history_log_counter;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_HISTORY_TEXT_COUNTER:
			value_uint = cache->stats.history_text_counter;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_NOTSUPPORTED_COUNTER:
			value_uint = cache->stats.notsupported_counter;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_HISTORY_TOTAL:
			value_uint = hc_mem->total_size;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_HISTORY_USED:
			value_uint = hc_mem->total_size - hc_mem->free_size;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_HISTORY_FREE:
			value_uint = hc_mem->free_size;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_HISTORY_PUSED:
			value_double = 100 * (double)(hc_mem->total_size - hc_mem->free_size) / hc_mem->total_size;
			ret = (void *)&value_double;
			break;
		case TRX_STATS_HISTORY_PFREE:
			value_double = 100 * (double)hc_mem->free_size / hc_mem->total_size;
			ret = (void *)&value_double;
			break;
		case TRX_STATS_TREND_TOTAL:
			value_uint = trend_mem->orig_size;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_TREND_USED:
			value_uint = trend_mem->orig_size - trend_mem->free_size;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_TREND_FREE:
			value_uint = trend_mem->free_size;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_TREND_PUSED:
			value_double = 100 * (double)(trend_mem->orig_size - trend_mem->free_size) /
					trend_mem->orig_size;
			ret = (void *)&value_double;
			break;
		case TRX_STATS_TREND_PFREE:
			value_double = 100 * (double)trend_mem->free_size / trend_mem->orig_size;
			ret = (void *)&value_double;
			break;
		case TRX_STATS_HISTORY_INDEX_TOTAL:
			value_uint = hc_index_mem->total_size;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_HISTORY_INDEX_USED:
			value_uint = hc_index_mem->total_size - hc_index_mem->free_size;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_HISTORY_INDEX_FREE:
			value_uint = hc_index_mem->free_size;
			ret = (void *)&value_uint;
			break;
		case TRX_STATS_HISTORY_INDEX_PUSED:
			value_double = 100 * (double)(hc_index_mem->total_size - hc_index_mem->free_size) /
					hc_index_mem->total_size;
			ret = (void *)&value_double;
			break;
		case TRX_STATS_HISTORY_INDEX_PFREE:
			value_double = 100 * (double)hc_index_mem->free_size / hc_index_mem->total_size;
			ret = (void *)&value_double;
			break;
		default:
			ret = NULL;
	}

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_trend                                                      *
 *                                                                            *
 * Purpose: find existing or add new structure and return pointer             *
 *                                                                            *
 * Return value: pointer to a trend structure                                 *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
static TRX_DC_TREND	*DCget_trend(trx_uint64_t itemid)
{
	TRX_DC_TREND	*ptr, trend;

	if (NULL != (ptr = (TRX_DC_TREND *)trx_hashset_search(&cache->trends, &itemid)))
		return ptr;

	memset(&trend, 0, sizeof(TRX_DC_TREND));
	trend.itemid = itemid;

	return (TRX_DC_TREND *)trx_hashset_insert(&cache->trends, &trend, sizeof(TRX_DC_TREND));
}

/******************************************************************************
 *                                                                            *
 * Function: DCupdate_trends                                                  *
 *                                                                            *
 * Purpose: apply disable_from changes to cache                               *
 *                                                                            *
 ******************************************************************************/
static void	DCupdate_trends(trx_vector_uint64_pair_t *trends_diff)
{
	int	i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	LOCK_TRENDS;

	for (i = 0; i < trends_diff->values_num; i++)
	{
		TRX_DC_TREND	*trend;

		if (NULL != (trend = (TRX_DC_TREND *)trx_hashset_search(&cache->trends, &trends_diff->values[i].first)))
			trend->disable_from = trends_diff->values[i].second;
	}

	UNLOCK_TRENDS;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_insert_trends_in_db                                           *
 *                                                                            *
 * Purpose: helper function for DCflush trends                                *
 *                                                                            *
 ******************************************************************************/
static void	dc_insert_trends_in_db(TRX_DC_TREND *trends, int trends_num, unsigned char value_type,
		const char *table_name, int clock)
{
	TRX_DC_TREND	*trend;
	int		i;
	trx_db_insert_t	db_insert;

	trx_db_insert_prepare(&db_insert, table_name, "itemid", "clock", "num", "value_min", "value_avg",
			"value_max", NULL);

	for (i = 0; i < trends_num; i++)
	{
		trend = &trends[i];

		if (0 == trend->itemid)
			continue;

		if (clock != trend->clock || value_type != trend->value_type)
			continue;

		if (ITEM_VALUE_TYPE_FLOAT == value_type)
		{
			trx_db_insert_add_values(&db_insert, trend->itemid, trend->clock, trend->num,
					trend->value_min.dbl, trend->value_avg.dbl, trend->value_max.dbl);
		}
		else
		{
			trx_uint128_t	avg;

			/* calculate the trend average value */
			udiv128_64(&avg, &trend->value_avg.ui64, trend->num);

			trx_db_insert_add_values(&db_insert, trend->itemid, trend->clock, trend->num,
					trend->value_min.ui64, avg.lo, trend->value_max.ui64);
		}

		trend->itemid = 0;
	}

	trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_remove_updated_trends                                         *
 *                                                                            *
 * Purpose: helper function for DCflush trends                                *
 *                                                                            *
 ******************************************************************************/
static void	dc_remove_updated_trends(TRX_DC_TREND *trends, int trends_num, const char *table_name,
		int value_type, trx_uint64_t *itemids, int *itemids_num, int clock)
{
	int		i;
	TRX_DC_TREND	*trend;
	trx_uint64_t	itemid;
	size_t		sql_offset;
	DB_RESULT	result;
	DB_ROW		row;

	sql_offset = 0;
	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct itemid"
			" from %s"
			" where clock>=%d and",
			table_name, clock);

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids, *itemids_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(itemid, row[0]);
		uint64_array_remove(itemids, itemids_num, &itemid, 1);
	}
	DBfree_result(result);

	while (0 != *itemids_num)
	{
		itemid = itemids[--*itemids_num];

		for (i = 0; i < trends_num; i++)
		{
			trend = &trends[i];

			if (itemid != trend->itemid)
				continue;

			if (clock != trend->clock || value_type != trend->value_type)
				continue;

			trend->disable_from = clock;
			break;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_trends_update_float                                           *
 *                                                                            *
 * Purpose: helper function for DCflush trends                                *
 *                                                                            *
 ******************************************************************************/
static void	dc_trends_update_float(TRX_DC_TREND *trend, DB_ROW row, int num, size_t *sql_offset)
{
	history_value_t	value_min, value_avg, value_max;

	value_min.dbl = atof(row[2]);
	value_avg.dbl = atof(row[3]);
	value_max.dbl = atof(row[4]);

	if (value_min.dbl < trend->value_min.dbl)
		trend->value_min.dbl = value_min.dbl;
	if (value_max.dbl > trend->value_max.dbl)
		trend->value_max.dbl = value_max.dbl;
	trend->value_avg.dbl = (trend->num * trend->value_avg.dbl
			+ num * value_avg.dbl) / (trend->num + num);
	trend->num += num;

	trx_snprintf_alloc(&sql, &sql_alloc, sql_offset,
			"update trends set num=%d,value_min=" TRX_FS_DBL ",value_avg="
			TRX_FS_DBL ",value_max=" TRX_FS_DBL " where itemid=" TRX_FS_UI64
			" and clock=%d;\n",
			trend->num,
			trend->value_min.dbl,
			trend->value_avg.dbl,
			trend->value_max.dbl,
			trend->itemid,
			trend->clock);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_trends_update_uint                                            *
 *                                                                            *
 * Purpose: helper function for DCflush trends                                *
 *                                                                            *
 ******************************************************************************/
static void	dc_trends_update_uint(TRX_DC_TREND *trend, DB_ROW row, int num, size_t *sql_offset)
{
	history_value_t	value_min, value_avg, value_max;
	trx_uint128_t	avg;

	TRX_STR2UINT64(value_min.ui64, row[2]);
	TRX_STR2UINT64(value_avg.ui64, row[3]);
	TRX_STR2UINT64(value_max.ui64, row[4]);

	if (value_min.ui64 < trend->value_min.ui64)
		trend->value_min.ui64 = value_min.ui64;
	if (value_max.ui64 > trend->value_max.ui64)
		trend->value_max.ui64 = value_max.ui64;

	/* calculate the trend average value */
	umul64_64(&avg, num, value_avg.ui64);
	uinc128_128(&trend->value_avg.ui64, &avg);
	udiv128_64(&avg, &trend->value_avg.ui64, trend->num + num);

	trend->num += num;

	trx_snprintf_alloc(&sql, &sql_alloc, sql_offset,
			"update trends_uint set num=%d,value_min=" TRX_FS_UI64 ",value_avg="
			TRX_FS_UI64 ",value_max=" TRX_FS_UI64 " where itemid=" TRX_FS_UI64
			" and clock=%d;\n",
			trend->num,
			trend->value_min.ui64,
			avg.lo,
			trend->value_max.ui64,
			trend->itemid,
			trend->clock);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_trends_fetch_and_update                                       *
 *                                                                            *
 * Purpose: helper function for DCflush trends                                *
 *                                                                            *
 ******************************************************************************/
static void	dc_trends_fetch_and_update(TRX_DC_TREND *trends, int trends_num, trx_uint64_t *itemids,
		int itemids_num, int *inserts_num, unsigned char value_type,
		const char *table_name, int clock)
{

	int		i, num;
	DB_RESULT	result;
	DB_ROW		row;
	trx_uint64_t	itemid;
	TRX_DC_TREND	*trend;
	size_t		sql_offset;

	sql_offset = 0;
	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select itemid,num,value_min,value_avg,value_max"
			" from %s"
			" where clock=%d and",
			table_name, clock);

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids, itemids_num);

	result = DBselect("%s", sql);

	sql_offset = 0;
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(itemid, row[0]);

		for (i = 0; i < trends_num; i++)
		{
			trend = &trends[i];

			if (itemid != trend->itemid)
				continue;

			if (clock != trend->clock || value_type != trend->value_type)
				continue;

			break;
		}

		if (i == trends_num)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		num = atoi(row[1]);

		if (value_type == ITEM_VALUE_TYPE_FLOAT)
			dc_trends_update_float(trend, row, num, &sql_offset);
		else
			dc_trends_update_uint(trend, row, num, &sql_offset);

		trend->itemid = 0;

		--*inserts_num;

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	DBfree_result(result);

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (sql_offset > 16)	/* In ORACLE always present begin..end; */
		DBexecute("%s", sql);
}

/******************************************************************************
 *                                                                            *
 * Function: DBflush_trends                                                   *
 *                                                                            *
 * Purpose: flush trend to the database                                       *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
static void	DBflush_trends(TRX_DC_TREND *trends, int *trends_num, trx_vector_uint64_pair_t *trends_diff)
{
	int		num, i, clock, inserts_num = 0, itemids_alloc, itemids_num = 0, trends_to = *trends_num;
	unsigned char	value_type;
	trx_uint64_t	*itemids = NULL;
	TRX_DC_TREND	*trend = NULL;
	const char	*table_name;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() trends_num:%d", __func__, *trends_num);

	clock = trends[0].clock;
	value_type = trends[0].value_type;

	switch (value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			table_name = "trends";
			break;
		case ITEM_VALUE_TYPE_UINT64:
			table_name = "trends_uint";
			break;
		default:
			assert(0);
	}

	itemids_alloc = MIN(TRX_HC_SYNC_MAX, *trends_num);
	itemids = (trx_uint64_t *)trx_malloc(itemids, itemids_alloc * sizeof(trx_uint64_t));

	for (i = 0; i < *trends_num; i++)
	{
		trend = &trends[i];

		if (clock != trend->clock || value_type != trend->value_type)
			continue;

		inserts_num++;

		if (0 != trend->disable_from)
			continue;

		uint64_array_add(&itemids, &itemids_alloc, &itemids_num, trend->itemid, 64);

		if (TRX_HC_SYNC_MAX == itemids_num)
		{
			trends_to = i + 1;
			break;
		}
	}

	if (0 != itemids_num)
	{
		dc_remove_updated_trends(trends, trends_to, table_name, value_type, itemids,
				&itemids_num, clock);
	}

	for (i = 0; i < trends_to; i++)
	{
		trend = &trends[i];

		if (clock != trend->clock || value_type != trend->value_type)
			continue;

		if (0 != trend->disable_from && clock >= trend->disable_from)
			continue;

		uint64_array_add(&itemids, &itemids_alloc, &itemids_num, trend->itemid, 64);
	}

	if (0 != itemids_num)
	{
		dc_trends_fetch_and_update(trends, trends_to, itemids, itemids_num,
				&inserts_num, value_type, table_name, clock);
	}

	trx_free(itemids);

	/* if 'trends' is not a primary trends buffer */
	if (NULL != trends_diff)
	{
		/* we update it too */
		for (i = 0; i < trends_to; i++)
		{
			trx_uint64_pair_t	pair;

			if (0 == trends[i].itemid)
				continue;

			if (clock != trends[i].clock || value_type != trends[i].value_type)
				continue;

			if (0 == trends[i].disable_from || trends[i].disable_from > clock)
				continue;

			pair.first = trends[i].itemid;
			pair.second = clock + SEC_PER_HOUR;
			trx_vector_uint64_pair_append(trends_diff, pair);
		}
	}

	if (0 != inserts_num)
		dc_insert_trends_in_db(trends, trends_to, value_type, table_name, clock);

	/* clean trends */
	for (i = 0, num = 0; i < *trends_num; i++)
	{
		if (0 == trends[i].itemid)
			continue;

		memcpy(&trends[num++], &trends[i], sizeof(TRX_DC_TREND));
	}
	*trends_num = num;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCflush_trend                                                    *
 *                                                                            *
 * Purpose: move trend to the array of trends for flushing to DB              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
static void	DCflush_trend(TRX_DC_TREND *trend, TRX_DC_TREND **trends, int *trends_alloc, int *trends_num)
{
	if (*trends_num == *trends_alloc)
	{
		*trends_alloc += 256;
		*trends = (TRX_DC_TREND *)trx_realloc(*trends, *trends_alloc * sizeof(TRX_DC_TREND));
	}

	memcpy(&(*trends)[*trends_num], trend, sizeof(TRX_DC_TREND));
	(*trends_num)++;

	trend->clock = 0;
	trend->num = 0;
	memset(&trend->value_min, 0, sizeof(history_value_t));
	memset(&trend->value_avg, 0, sizeof(value_avg_t));
	memset(&trend->value_max, 0, sizeof(history_value_t));
}

/******************************************************************************
 *                                                                            *
 * Function: DCadd_trend                                                      *
 *                                                                            *
 * Purpose: add new value to the trends                                       *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
static void	DCadd_trend(const TRX_DC_HISTORY *history, TRX_DC_TREND **trends, int *trends_alloc, int *trends_num)
{
	TRX_DC_TREND	*trend = NULL;
	int		hour;

	hour = history->ts.sec - history->ts.sec % SEC_PER_HOUR;

	trend = DCget_trend(history->itemid);

	if (trend->num > 0 && (trend->clock != hour || trend->value_type != history->value_type) &&
			SUCCEED == trx_history_requires_trends(trend->value_type))
	{
		DCflush_trend(trend, trends, trends_alloc, trends_num);
	}

	trend->value_type = history->value_type;
	trend->clock = hour;

	switch (trend->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			if (trend->num == 0 || history->value.dbl < trend->value_min.dbl)
				trend->value_min.dbl = history->value.dbl;
			if (trend->num == 0 || history->value.dbl > trend->value_max.dbl)
				trend->value_max.dbl = history->value.dbl;
			trend->value_avg.dbl = (trend->num * trend->value_avg.dbl
				+ history->value.dbl) / (trend->num + 1);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			if (trend->num == 0 || history->value.ui64 < trend->value_min.ui64)
				trend->value_min.ui64 = history->value.ui64;
			if (trend->num == 0 || history->value.ui64 > trend->value_max.ui64)
				trend->value_max.ui64 = history->value.ui64;
			uinc128_64(&trend->value_avg.ui64, history->value.ui64);
			break;
	}
	trend->num++;
}

/******************************************************************************
 *                                                                            *
 * Function: DCmass_update_trends                                             *
 *                                                                            *
 * Purpose: update trends cache and get list of trends to flush into database *
 *                                                                            *
 * Parameters: history     - array of history data                            *
 *             history_num - number of history structures                     *
 *             trends      - list of trends to flush into database            *
 *             trends_num  - number of trends                                 *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
static void	DCmass_update_trends(const TRX_DC_HISTORY *history, int history_num, TRX_DC_TREND **trends,
		int *trends_num)
{
	trx_timespec_t	ts;
	int		trends_alloc = 0, i, hour, seconds;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_timespec(&ts);
	seconds = ts.sec % SEC_PER_HOUR;
	hour = ts.sec - seconds;

	LOCK_TRENDS;

	for (i = 0; i < history_num; i++)
	{
		const TRX_DC_HISTORY	*h = &history[i];

		if (0 != (TRX_DC_FLAGS_NOT_FOR_TRENDS & h->flags))
			continue;

		DCadd_trend(h, trends, &trends_alloc, trends_num);
	}

	if (cache->trends_last_cleanup_hour < hour && TRX_TRENDS_CLEANUP_TIME < seconds)
	{
		trx_hashset_iter_t	iter;
		TRX_DC_TREND		*trend;

		trx_hashset_iter_reset(&cache->trends, &iter);

		while (NULL != (trend = (TRX_DC_TREND *)trx_hashset_iter_next(&iter)))
		{
			if (trend->clock == hour)
				continue;

			if (SUCCEED == trx_history_requires_trends(trend->value_type))
				DCflush_trend(trend, trends, &trends_alloc, trends_num);

			trx_hashset_iter_remove(&iter);
		}

		cache->trends_last_cleanup_hour = hour;
	}

	UNLOCK_TRENDS;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DBmass_update_trends                                             *
 *                                                                            *
 * Purpose: prepare history data using items from configuration cache         *
 *                                                                            *
 * Parameters: trends      - [IN] trends from cache to be added to database   *
 *             trends_num  - [IN] number of trends to add to database         *
 *             trends_diff - [OUT] disable_from updates                       *
 *                                                                            *
 ******************************************************************************/
static void	DBmass_update_trends(const TRX_DC_TREND *trends, int trends_num,
		trx_vector_uint64_pair_t *trends_diff)
{
	TRX_DC_TREND	*trends_tmp;

	if (0 != trends_num)
	{
		trends_tmp = (TRX_DC_TREND *)trx_malloc(NULL, trends_num * sizeof(TRX_DC_TREND));
		memcpy(trends_tmp, trends, trends_num * sizeof(TRX_DC_TREND));

		while (0 < trends_num)
			DBflush_trends(trends_tmp, &trends_num, trends_diff);

		trx_free(trends_tmp);
	}
}

typedef struct
{
	trx_uint64_t		hostid;
	trx_vector_ptr_t	groups;
}
trx_host_info_t;

/******************************************************************************
 *                                                                            *
 * Function: trx_host_info_clean                                              *
 *                                                                            *
 * Purpose: frees resources allocated to store host groups names              *
 *                                                                            *
 * Parameters: host_info - [IN] host information                              *
 *                                                                            *
 ******************************************************************************/
static void	trx_host_info_clean(trx_host_info_t *host_info)
{
	trx_vector_ptr_clear_ext(&host_info->groups, trx_ptr_free);
	trx_vector_ptr_destroy(&host_info->groups);
}

/******************************************************************************
 *                                                                            *
 * Function: db_get_hosts_info_by_hostid                                      *
 *                                                                            *
 * Purpose: get hosts groups names                                            *
 *                                                                            *
 * Parameters: hosts_info - [IN/OUT] output names of host groups for a host   *
 *             hostids    - [IN] hosts identifiers                            *
 *                                                                            *
 ******************************************************************************/
static void	db_get_hosts_info_by_hostid(trx_hashset_t *hosts_info, const trx_vector_uint64_t *hostids)
{
	int		i;
	size_t		sql_offset = 0;
	DB_RESULT	result;
	DB_ROW		row;

	for (i = 0; i < hostids->values_num; i++)
	{
		trx_host_info_t	host_info = {.hostid = hostids->values[i]};

		trx_vector_ptr_create(&host_info.groups);
		trx_hashset_insert(hosts_info, &host_info, sizeof(host_info));
	}

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select distinct hg.hostid,g.name"
				" from hstgrp g,hosts_groups hg"
				" where g.groupid=hg.groupid"
					" and");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hg.hostid", hostids->values, hostids->values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		trx_uint64_t	hostid;
		trx_host_info_t	*host_info;

		TRX_DBROW2UINT64(hostid, row[0]);

		if (NULL == (host_info = (trx_host_info_t *)trx_hashset_search(hosts_info, &hostid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		trx_vector_ptr_append(&host_info->groups, trx_strdup(NULL, row[1]));
	}
	DBfree_result(result);
}

typedef struct
{
	trx_uint64_t		itemid;
	char			*name;
	DC_ITEM			*item;
	trx_vector_ptr_t	applications;
}
trx_item_info_t;

/******************************************************************************
 *                                                                            *
 * Function: db_get_items_info_by_itemid                                      *
 *                                                                            *
 * Purpose: get items name and applications                                   *
 *                                                                            *
 * Parameters: items_info - [IN/OUT] output item name and applications        *
 *             itemids    - [IN] the item identifiers                         *
 *                                                                            *
 ******************************************************************************/
static void	db_get_items_info_by_itemid(trx_hashset_t *items_info, const trx_vector_uint64_t *itemids)
{
	size_t		sql_offset = 0;
	DB_RESULT	result;
	DB_ROW		row;

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select itemid,name from items where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids->values, itemids->values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		trx_uint64_t	itemid;
		trx_item_info_t	*item_info;

		TRX_DBROW2UINT64(itemid, row[0]);

		if (NULL == (item_info = (trx_item_info_t *)trx_hashset_search(items_info, &itemid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		trx_substitute_item_name_macros(item_info->item, row[1], &item_info->name);
	}
	DBfree_result(result);

	sql_offset = 0;
	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select i.itemid,a.name"
			" from applications a,items_applications i"
			" where a.applicationid=i.applicationid"
				" and");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.itemid", itemids->values, itemids->values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		trx_uint64_t	itemid;
		trx_item_info_t	*item_info;

		TRX_DBROW2UINT64(itemid, row[0]);

		if (NULL == (item_info = (trx_item_info_t *)trx_hashset_search(items_info, &itemid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		trx_vector_ptr_append(&item_info->applications, trx_strdup(NULL, row[1]));
	}
	DBfree_result(result);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_item_info_clean                                              *
 *                                                                            *
 * Purpose: frees resources allocated to store item applications and name     *
 *                                                                            *
 * Parameters: item_info - [IN] item information                              *
 *                                                                            *
 ******************************************************************************/
static void	trx_item_info_clean(trx_item_info_t *item_info)
{
	trx_vector_ptr_clear_ext(&item_info->applications, trx_ptr_free);
	trx_vector_ptr_destroy(&item_info->applications);
	trx_free(item_info->name);
}

/******************************************************************************
 *                                                                            *
 * Function: DCexport_trends                                                  *
 *                                                                            *
 * Purpose: export trends                                                     *
 *                                                                            *
 * Parameters: trends     - [IN] trends from cache                            *
 *             trends_num - [IN] number of trends                             *
 *             hosts_info - [IN] hosts groups names                           *
 *             items_info - [IN] item names and applications                  *
 *                                                                            *
 ******************************************************************************/
static void	DCexport_trends(const TRX_DC_TREND *trends, int trends_num, trx_hashset_t *hosts_info,
		trx_hashset_t *items_info)
{
	struct trx_json		json;
	const TRX_DC_TREND	*trend = NULL;
	int			i, j;
	const DC_ITEM		*item;
	trx_host_info_t		*host_info;
	trx_item_info_t		*item_info;
	trx_uint128_t		avg;	/* calculate the trend average value */

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);

	for (i = 0; i < trends_num; i++)
	{
		trend = &trends[i];

		if (NULL == (item_info = (trx_item_info_t *)trx_hashset_search(items_info, &trend->itemid)))
			continue;

		item = item_info->item;

		if (NULL == (host_info = (trx_host_info_t *)trx_hashset_search(hosts_info, &item->host.hostid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		trx_json_clean(&json);

		trx_json_addobject(&json,TRX_PROTO_TAG_HOST);
		trx_json_addstring(&json, TRX_PROTO_TAG_HOST, item->host.host, TRX_JSON_TYPE_STRING);
		trx_json_addstring(&json, TRX_PROTO_TAG_NAME, item->host.name, TRX_JSON_TYPE_STRING);
		trx_json_close(&json);

		trx_json_addarray(&json, TRX_PROTO_TAG_GROUPS);

		for (j = 0; j < host_info->groups.values_num; j++)
			trx_json_addstring(&json, NULL, host_info->groups.values[j], TRX_JSON_TYPE_STRING);

		trx_json_close(&json);

		trx_json_addarray(&json, TRX_PROTO_TAG_APPLICATIONS);

		for (j = 0; j < item_info->applications.values_num; j++)
			trx_json_addstring(&json, NULL, item_info->applications.values[j], TRX_JSON_TYPE_STRING);

		trx_json_close(&json);
		trx_json_adduint64(&json, TRX_PROTO_TAG_ITEMID, item->itemid);

		if (NULL != item_info->name)
			trx_json_addstring(&json, TRX_PROTO_TAG_NAME, item_info->name, TRX_JSON_TYPE_STRING);

		trx_json_addint64(&json, TRX_PROTO_TAG_CLOCK, trend->clock);
		trx_json_addint64(&json, TRX_PROTO_TAG_COUNT, trend->num);

		switch (trend->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				trx_json_addfloat(&json, TRX_PROTO_TAG_MIN, trend->value_min.dbl);
				trx_json_addfloat(&json, TRX_PROTO_TAG_AVG, trend->value_avg.dbl);
				trx_json_addfloat(&json, TRX_PROTO_TAG_MAX, trend->value_max.dbl);
				break;
			case ITEM_VALUE_TYPE_UINT64:
				trx_json_adduint64(&json, TRX_PROTO_TAG_MIN, trend->value_min.ui64);
				udiv128_64(&avg, &trend->value_avg.ui64, trend->num);
				trx_json_adduint64(&json, TRX_PROTO_TAG_AVG, avg.lo);
				trx_json_adduint64(&json, TRX_PROTO_TAG_MAX, trend->value_max.ui64);
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}

		trx_json_adduint64(&json, TRX_PROTO_TAG_TYPE, trend->value_type);
		trx_trends_export_write(json.buffer, json.buffer_size);
	}

	trx_trends_export_flush();
	trx_json_free(&json);
}

/******************************************************************************
 *                                                                            *
 * Function: DCexport_history                                                 *
 *                                                                            *
 * Purpose: export history                                                    *
 *                                                                            *
 * Parameters: history     - [IN/OUT] array of history data                   *
 *             history_num - [IN] number of history structures                *
 *             hosts_info  - [IN] hosts groups names                          *
 *             items_info  - [IN] item names and applications                 *
 *                                                                            *
 ******************************************************************************/
static void	DCexport_history(const TRX_DC_HISTORY *history, int history_num, trx_hashset_t *hosts_info,
		trx_hashset_t *items_info)
{
	const TRX_DC_HISTORY	*h;
	const DC_ITEM		*item;
	int			i, j;
	trx_host_info_t		*host_info;
	trx_item_info_t		*item_info;
	struct trx_json		json;

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);

	for (i = 0; i < history_num; i++)
	{
		h = &history[i];

		if (0 != (TRX_DC_FLAGS_NOT_FOR_MODULES & h->flags))
			continue;

		if (NULL == (item_info = (trx_item_info_t *)trx_hashset_search(items_info, &h->itemid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item = item_info->item;

		if (NULL == (host_info = (trx_host_info_t *)trx_hashset_search(hosts_info, &item->host.hostid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		trx_json_clean(&json);

		trx_json_addobject(&json,TRX_PROTO_TAG_HOST);
		trx_json_addstring(&json, TRX_PROTO_TAG_HOST, item->host.host, TRX_JSON_TYPE_STRING);
		trx_json_addstring(&json, TRX_PROTO_TAG_NAME, item->host.name, TRX_JSON_TYPE_STRING);
		trx_json_close(&json);

		trx_json_addarray(&json, TRX_PROTO_TAG_GROUPS);

		for (j = 0; j < host_info->groups.values_num; j++)
			trx_json_addstring(&json, NULL, host_info->groups.values[j], TRX_JSON_TYPE_STRING);

		trx_json_close(&json);

		trx_json_addarray(&json, TRX_PROTO_TAG_APPLICATIONS);

		for (j = 0; j < item_info->applications.values_num; j++)
			trx_json_addstring(&json, NULL, item_info->applications.values[j], TRX_JSON_TYPE_STRING);

		trx_json_close(&json);
		trx_json_adduint64(&json, TRX_PROTO_TAG_ITEMID, item->itemid);

		if (NULL != item_info->name)
			trx_json_addstring(&json, TRX_PROTO_TAG_NAME, item_info->name, TRX_JSON_TYPE_STRING);

		trx_json_addint64(&json, TRX_PROTO_TAG_CLOCK, h->ts.sec);
		trx_json_addint64(&json, TRX_PROTO_TAG_NS, h->ts.ns);

		switch (h->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				trx_json_addfloat(&json, TRX_PROTO_TAG_VALUE, h->value.dbl);
				break;
			case ITEM_VALUE_TYPE_UINT64:
				trx_json_adduint64(&json, TRX_PROTO_TAG_VALUE, h->value.ui64);
				break;
			case ITEM_VALUE_TYPE_STR:
				trx_json_addstring(&json, TRX_PROTO_TAG_VALUE, h->value.str, TRX_JSON_TYPE_STRING);
				break;
			case ITEM_VALUE_TYPE_TEXT:
				trx_json_addstring(&json, TRX_PROTO_TAG_VALUE, h->value.str, TRX_JSON_TYPE_STRING);
				break;
			case ITEM_VALUE_TYPE_LOG:
				trx_json_addint64(&json, TRX_PROTO_TAG_LOGTIMESTAMP, h->value.log->timestamp);
				trx_json_addstring(&json, TRX_PROTO_TAG_LOGSOURCE,
						TRX_NULL2EMPTY_STR(h->value.log->source), TRX_JSON_TYPE_STRING);
				trx_json_addint64(&json, TRX_PROTO_TAG_LOGSEVERITY, h->value.log->severity);
				trx_json_addint64(&json, TRX_PROTO_TAG_LOGEVENTID, h->value.log->logeventid);
				trx_json_addstring(&json, TRX_PROTO_TAG_VALUE, h->value.log->value,
						TRX_JSON_TYPE_STRING);
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}

		trx_json_adduint64(&json, TRX_PROTO_TAG_TYPE, h->value_type);
		trx_history_export_write(json.buffer, json.buffer_size);
	}

	trx_history_export_flush();
	trx_json_free(&json);
}

/******************************************************************************
 *                                                                            *
 * Function: DCexport_history_and_trends                                      *
 *                                                                            *
 * Purpose: export history and trends                                         *
 *                                                                            *
 * Parameters: history     - [IN/OUT] array of history data                   *
 *             history_num - [IN] number of history structures                *
 *             itemids     - [IN] the item identifiers                        *
 *                                (used for item lookup)                      *
 *             items       - [IN] the items                                   *
 *             errcodes    - [IN] item error codes                            *
 *             trends      - [IN] trends from cache                           *
 *             trends_num  - [IN] number of trends                            *
 *                                                                            *
 ******************************************************************************/
static void	DCexport_history_and_trends(const TRX_DC_HISTORY *history, int history_num,
		const trx_vector_uint64_t *itemids, DC_ITEM *items, const int *errcodes, const TRX_DC_TREND *trends,
		int trends_num)
{
	int			i, index;
	trx_vector_uint64_t	hostids, item_info_ids;
	trx_hashset_t		hosts_info, items_info;
	DC_ITEM			*item;
	trx_item_info_t		item_info;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() history_num:%d trends_num:%d", __func__, history_num, trends_num);

	trx_vector_uint64_create(&hostids);
	trx_vector_uint64_create(&item_info_ids);
	trx_hashset_create_ext(&items_info, itemids->values_num, TRX_DEFAULT_UINT64_HASH_FUNC,
			TRX_DEFAULT_UINT64_COMPARE_FUNC, (trx_clean_func_t)trx_item_info_clean,
			TRX_DEFAULT_MEM_MALLOC_FUNC, TRX_DEFAULT_MEM_REALLOC_FUNC, TRX_DEFAULT_MEM_FREE_FUNC);

	for (i = 0; i < history_num; i++)
	{
		const TRX_DC_HISTORY	*h = &history[i];

		if (0 != (TRX_DC_FLAGS_NOT_FOR_EXPORT & h->flags))
			continue;

		if (FAIL == (index = trx_vector_uint64_bsearch(itemids, h->itemid, TRX_DEFAULT_UINT64_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		if (SUCCEED != errcodes[index])
			continue;

		item = &items[index];

		trx_vector_uint64_append(&hostids, item->host.hostid);
		trx_vector_uint64_append(&item_info_ids, item->itemid);

		item_info.itemid = item->itemid;
		item_info.name = NULL;
		item_info.item = item;
		trx_vector_ptr_create(&item_info.applications);
		trx_hashset_insert(&items_info, &item_info, sizeof(item_info));
	}

	if (0 == history_num)
	{
		for (i = 0; i < trends_num; i++)
		{
			const TRX_DC_TREND	*trend = &trends[i];

			if (FAIL == (index = trx_vector_uint64_bsearch(itemids, trend->itemid,
					TRX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			if (SUCCEED != errcodes[index])
				continue;

			item = &items[index];

			trx_vector_uint64_append(&hostids, item->host.hostid);
			trx_vector_uint64_append(&item_info_ids, item->itemid);

			item_info.itemid = item->itemid;
			item_info.name = NULL;
			item_info.item = item;
			trx_vector_ptr_create(&item_info.applications);
			trx_hashset_insert(&items_info, &item_info, sizeof(item_info));
		}
	}

	if (0 == item_info_ids.values_num)
		goto clean;

	trx_vector_uint64_sort(&item_info_ids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_sort(&hostids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_uniq(&hostids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	trx_hashset_create_ext(&hosts_info, hostids.values_num, TRX_DEFAULT_UINT64_HASH_FUNC,
			TRX_DEFAULT_UINT64_COMPARE_FUNC, (trx_clean_func_t)trx_host_info_clean,
			TRX_DEFAULT_MEM_MALLOC_FUNC, TRX_DEFAULT_MEM_REALLOC_FUNC, TRX_DEFAULT_MEM_FREE_FUNC);

	db_get_hosts_info_by_hostid(&hosts_info, &hostids);

	db_get_items_info_by_itemid(&items_info, &item_info_ids);

	if (0 != history_num)
		DCexport_history(history, history_num, &hosts_info, &items_info);

	if (0 != trends_num)
		DCexport_trends(trends, trends_num, &hosts_info, &items_info);

	trx_hashset_destroy(&hosts_info);
clean:
	trx_hashset_destroy(&items_info);
	trx_vector_uint64_destroy(&item_info_ids);
	trx_vector_uint64_destroy(&hostids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCexport_all_trends                                              *
 *                                                                            *
 * Purpose: export all trends                                                 *
 *                                                                            *
 * Parameters: trends     - [IN] trends from cache                            *
 *             trends_num - [IN] number of trends                             *
 *                                                                            *
 ******************************************************************************/
static void	DCexport_all_trends(const TRX_DC_TREND *trends, int trends_num)
{
	DC_ITEM			*items;
	trx_vector_uint64_t	itemids;
	int			*errcodes, i, num;

	treegix_log(LOG_LEVEL_WARNING, "exporting trend data...");

	while (0 < trends_num)
	{
		num = MIN(TRX_HC_SYNC_MAX, trends_num);

		items = (DC_ITEM *)trx_malloc(NULL, sizeof(DC_ITEM) * (size_t)num);
		errcodes = (int *)trx_malloc(NULL, sizeof(int) * (size_t)num);

		trx_vector_uint64_create(&itemids);
		trx_vector_uint64_reserve(&itemids, num);

		for (i = 0; i < num; i++)
			trx_vector_uint64_append(&itemids, trends[i].itemid);

		trx_vector_uint64_sort(&itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		DCconfig_get_items_by_itemids(items, itemids.values, errcodes, num);

		DCexport_history_and_trends(NULL, 0, &itemids, items, errcodes, trends, num);

		DCconfig_clean_items(items, errcodes, num);
		trx_vector_uint64_destroy(&itemids);
		trx_free(items);
		trx_free(errcodes);

		trends += num;
		trends_num -= num;
	}

	treegix_log(LOG_LEVEL_WARNING, "exporting trend data done");
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_trends                                                    *
 *                                                                            *
 * Purpose: flush all trends to the database                                  *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_trends(void)
{
	trx_hashset_iter_t	iter;
	TRX_DC_TREND		*trends = NULL, *trend;
	int			trends_alloc = 0, trends_num = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() trends_num:%d", __func__, cache->trends_num);

	treegix_log(LOG_LEVEL_WARNING, "syncing trend data...");

	LOCK_TRENDS;

	trx_hashset_iter_reset(&cache->trends, &iter);

	while (NULL != (trend = (TRX_DC_TREND *)trx_hashset_iter_next(&iter)))
	{
		if (SUCCEED == trx_history_requires_trends(trend->value_type))
			DCflush_trend(trend, &trends, &trends_alloc, &trends_num);
	}

	UNLOCK_TRENDS;

	if (SUCCEED == trx_is_export_enabled() && 0 != trends_num)
		DCexport_all_trends(trends, trends_num);

	DBbegin();

	while (trends_num > 0)
		DBflush_trends(trends, &trends_num, NULL);

	DBcommit();

	trx_free(trends);

	treegix_log(LOG_LEVEL_WARNING, "syncing trend data done");

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: recalculate_triggers                                             *
 *                                                                            *
 * Purpose: re-calculate and update values of triggers related to the items   *
 *                                                                            *
 * Parameters: history           - [IN] array of history data                 *
 *             history_num       - [IN] number of history structures          *
 *             timer_triggerids  - [IN] the timer triggerids to process       *
 *             trigger_diff      - [OUT] trigger updates                      *
 *             timers_num        - [OUT] processed timer triggers             *
 *                                                                            *
 ******************************************************************************/
static void	recalculate_triggers(const TRX_DC_HISTORY *history, int history_num,
		const trx_vector_uint64_t *timer_triggerids, trx_vector_ptr_t *trigger_diff)
{
	int			i, item_num = 0;
	trx_uint64_t		*itemids = NULL;
	trx_timespec_t		*timespecs = NULL;
	trx_hashset_t		trigger_info;
	trx_vector_ptr_t	trigger_order;
	trx_vector_ptr_t	trigger_items;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 != history_num)
	{
		itemids = (trx_uint64_t *)trx_malloc(itemids, sizeof(trx_uint64_t) * (size_t)history_num);
		timespecs = (trx_timespec_t *)trx_malloc(timespecs, sizeof(trx_timespec_t) * (size_t)history_num);

		for (i = 0; i < history_num; i++)
		{
			const TRX_DC_HISTORY	*h = &history[i];

			if (0 != (TRX_DC_FLAG_NOVALUE & h->flags))
				continue;

			itemids[item_num] = h->itemid;
			timespecs[item_num] = h->ts;
			item_num++;
		}
	}

	if (0 == item_num && 0 == timer_triggerids->values_num)
		goto out;

	trx_hashset_create(&trigger_info, MAX(100, 2 * item_num + timer_triggerids->values_num),
			TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	trx_vector_ptr_create(&trigger_order);
	trx_vector_ptr_reserve(&trigger_order, trigger_info.num_slots);

	trx_vector_ptr_create(&trigger_items);

	if (0 != item_num)
	{
		DCconfig_get_triggers_by_itemids(&trigger_info, &trigger_order, itemids, timespecs, item_num);
		trx_determine_items_in_expressions(&trigger_order, itemids, item_num);
	}

	if (0 != timer_triggerids->values_num)
	{
		trx_timespec_t	ts;

		trx_timespec(&ts);
		trx_dc_get_timer_triggers_by_triggerids(&trigger_info, &trigger_order, timer_triggerids, &ts);
	}

	trx_vector_ptr_sort(&trigger_order, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	evaluate_expressions(&trigger_order);
	trx_process_triggers(&trigger_order, trigger_diff);

	DCfree_triggers(&trigger_order);

	trx_vector_ptr_destroy(&trigger_items);

	trx_hashset_destroy(&trigger_info);
	trx_vector_ptr_destroy(&trigger_order);
out:
	trx_free(timespecs);
	trx_free(itemids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCinventory_value_add(trx_vector_ptr_t *inventory_values, const DC_ITEM *item, TRX_DC_HISTORY *h)
{
	char			value[MAX_BUFFER_LEN];
	const char		*inventory_field;
	trx_inventory_value_t	*inventory_value;

	if (ITEM_STATE_NOTSUPPORTED == h->state)
		return;

	if (HOST_INVENTORY_AUTOMATIC != item->host.inventory_mode)
		return;

	if (0 != (TRX_DC_FLAG_UNDEF & h->flags) || 0 != (TRX_DC_FLAG_NOVALUE & h->flags) ||
			NULL == (inventory_field = DBget_inventory_field(item->inventory_link)))
	{
		return;
	}

	switch (h->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			trx_snprintf(value, sizeof(value), TRX_FS_DBL, h->value.dbl);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			trx_snprintf(value, sizeof(value), TRX_FS_UI64, h->value.ui64);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			strscpy(value, h->value.str);
			break;
		default:
			return;
	}

	trx_format_value(value, sizeof(value), item->valuemapid, item->units, h->value_type);

	inventory_value = (trx_inventory_value_t *)trx_malloc(NULL, sizeof(trx_inventory_value_t));

	inventory_value->hostid = item->host.hostid;
	inventory_value->idx = item->inventory_link - 1;
	inventory_value->field_name = inventory_field;
	inventory_value->value = trx_strdup(NULL, value);

	trx_vector_ptr_append(inventory_values, inventory_value);
}

static void	DCadd_update_inventory_sql(size_t *sql_offset, const trx_vector_ptr_t *inventory_values)
{
	char	*value_esc;
	int	i;

	for (i = 0; i < inventory_values->values_num; i++)
	{
		const trx_inventory_value_t	*inventory_value = (trx_inventory_value_t *)inventory_values->values[i];

		value_esc = DBdyn_escape_field("host_inventory", inventory_value->field_name, inventory_value->value);

		trx_snprintf_alloc(&sql, &sql_alloc, sql_offset,
				"update host_inventory set %s='%s' where hostid=" TRX_FS_UI64 ";\n",
				inventory_value->field_name, value_esc, inventory_value->hostid);

		DBexecute_overflowed_sql(&sql, &sql_alloc, sql_offset);

		trx_free(value_esc);
	}
}

static void	DCinventory_value_free(trx_inventory_value_t *inventory_value)
{
	trx_free(inventory_value->value);
	trx_free(inventory_value);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_history_clean_value                                           *
 *                                                                            *
 * Purpose: frees resources allocated to store str/text/log value             *
 *                                                                            *
 * Parameters: history     - [IN] the history data                            *
 *             history_num - [IN] the number of values in history data        *
 *                                                                            *
 ******************************************************************************/
static void	dc_history_clean_value(TRX_DC_HISTORY *history)
{
	if (ITEM_STATE_NOTSUPPORTED == history->state)
	{
		trx_free(history->value.err);
		return;
	}

	if (0 != (TRX_DC_FLAG_NOVALUE & history->flags))
		return;

	switch (history->value_type)
	{
		case ITEM_VALUE_TYPE_LOG:
			trx_free(history->value.log->value);
			trx_free(history->value.log->source);
			trx_free(history->value.log);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			trx_free(history->value.str);
			break;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: hc_free_item_values                                              *
 *                                                                            *
 * Purpose: frees resources allocated to store str/text/log values            *
 *                                                                            *
 * Parameters: history     - [IN] the history data                            *
 *             history_num - [IN] the number of values in history data        *
 *                                                                            *
 ******************************************************************************/
static void	hc_free_item_values(TRX_DC_HISTORY *history, int history_num)
{
	int	i;

	for (i = 0; i < history_num; i++)
		dc_history_clean_value(&history[i]);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_history_set_error                                             *
 *                                                                            *
 * Purpose: sets history data to notsupported                                 *
 *                                                                            *
 * Parameters: history  - [IN] the history data                               *
 *             errmsg   - [IN] the error message                              *
 *                                                                            *
 * Comments: The error message is stored directly and freed with when history *
 *           data is cleaned.                                                 *
 *                                                                            *
 ******************************************************************************/
static void	dc_history_set_error(TRX_DC_HISTORY *hdata, char *errmsg)
{
	dc_history_clean_value(hdata);
	hdata->value.err = errmsg;
	hdata->state = ITEM_STATE_NOTSUPPORTED;
	hdata->flags |= TRX_DC_FLAG_UNDEF;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_history_set_value                                             *
 *                                                                            *
 * Purpose: sets history data value                                           *
 *                                                                            *
 * Parameters: hdata      - [IN/OUT] the history data                         *
 *             value_type - [IN] the item value type                          *
 *             value      - [IN] the value to set                             *
 *                                                                            *
 ******************************************************************************/
static void	dc_history_set_value(TRX_DC_HISTORY *hdata, unsigned char value_type, trx_variant_t *value)
{
	char	*errmsg = NULL;

	if (FAIL == trx_variant_to_value_type(value, value_type, &errmsg))
	{
		dc_history_set_error(hdata, errmsg);
		return;
	}

	switch (value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			dc_history_clean_value(hdata);
			hdata->value.dbl = value->data.dbl;
			break;
		case ITEM_VALUE_TYPE_UINT64:
			dc_history_clean_value(hdata);
			hdata->value.ui64 = value->data.ui64;
			break;
		case ITEM_VALUE_TYPE_STR:
			dc_history_clean_value(hdata);
			hdata->value.str = value->data.str;
			hdata->value.str[trx_db_strlen_n(hdata->value.str, HISTORY_STR_VALUE_LEN)] = '\0';
			break;
		case ITEM_VALUE_TYPE_TEXT:
			dc_history_clean_value(hdata);
			hdata->value.str = value->data.str;
			hdata->value.str[trx_db_strlen_n(hdata->value.str, HISTORY_TEXT_VALUE_LEN)] = '\0';
			break;
		case ITEM_VALUE_TYPE_LOG:
			if (ITEM_VALUE_TYPE_LOG != hdata->value_type)
			{
				dc_history_clean_value(hdata);
				hdata->value.log = (trx_log_value_t *)trx_malloc(NULL, sizeof(trx_log_value_t));
				memset(hdata->value.log, 0, sizeof(trx_log_value_t));
			}
			hdata->value.log->value = value->data.str;
			hdata->value.str[trx_db_strlen_n(hdata->value.str, HISTORY_LOG_VALUE_LEN)] = '\0';
	}

	hdata->value_type = value_type;
	trx_variant_set_none(value);
}

/******************************************************************************
 *                                                                            *
 * Function: normalize_item_value                                             *
 *                                                                            *
 * Purpose: normalize item value by performing truncation of long text        *
 *          values and changes value format according to the item value type  *
 *                                                                            *
 * Parameters: item          - [IN] the item                                  *
 *             hdata         - [IN/OUT] the historical data to process        *
 *                                                                            *
 ******************************************************************************/
static void	normalize_item_value(const DC_ITEM *item, TRX_DC_HISTORY *hdata)
{
	char		*logvalue;
	trx_variant_t	value_var;

	if (0 != (hdata->flags & TRX_DC_FLAG_NOVALUE))
		return;

	if (ITEM_STATE_NOTSUPPORTED == hdata->state)
		return;

	if (0 == (hdata->flags & TRX_DC_FLAG_NOHISTORY))
		hdata->ttl = item->history_sec;

	if (item->value_type == hdata->value_type)
	{
		/* truncate text based values if necessary */
		switch (hdata->value_type)
		{
			case ITEM_VALUE_TYPE_STR:
				hdata->value.str[trx_db_strlen_n(hdata->value.str, HISTORY_STR_VALUE_LEN)] = '\0';
				break;
			case ITEM_VALUE_TYPE_TEXT:
				hdata->value.str[trx_db_strlen_n(hdata->value.str, HISTORY_TEXT_VALUE_LEN)] = '\0';
				break;
			case ITEM_VALUE_TYPE_LOG:
				logvalue = hdata->value.log->value;
				logvalue[trx_db_strlen_n(logvalue, HISTORY_LOG_VALUE_LEN)] = '\0';
				break;
			case ITEM_VALUE_TYPE_FLOAT:
				if (FAIL == trx_validate_value_dbl(hdata->value.dbl))
				{
					dc_history_set_error(hdata, trx_dsprintf(NULL, "Value " TRX_FS_DBL
							" is too small or too large.", hdata->value.dbl));
				}
				break;
		}
		return;
	}

	switch (hdata->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			trx_variant_set_dbl(&value_var, hdata->value.dbl);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			trx_variant_set_ui64(&value_var, hdata->value.ui64);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			trx_variant_set_str(&value_var, hdata->value.str);
			hdata->value.str = NULL;
			break;
		case ITEM_VALUE_TYPE_LOG:
			trx_variant_set_str(&value_var, hdata->value.log->value);
			hdata->value.log->value = NULL;
			break;
	}

	dc_history_set_value(hdata, item->value_type, &value_var);
	trx_variant_clear(&value_var);
}

/******************************************************************************
 *                                                                            *
 * Function: calculate_item_update                                            *
 *                                                                            *
 * Purpose: calculates what item fields must be updated                       *
 *                                                                            *
 * Parameters: item      - [IN] the item                                      *
 *             h         - [IN] the historical data to process                *
 *                                                                            *
 * Return value: The update data. This data must be freed by the caller.      *
 *                                                                            *
 * Comments: Will generate internal events when item state switches.          *
 *                                                                            *
 ******************************************************************************/
static trx_item_diff_t	*calculate_item_update(const DC_ITEM *item, const TRX_DC_HISTORY *h)
{
	trx_uint64_t	flags = TRX_FLAGS_ITEM_DIFF_UPDATE_LASTCLOCK;
	const char	*item_error = NULL;
	trx_item_diff_t	*diff;

	if (0 != (TRX_DC_FLAG_META & h->flags))
	{
		if (item->lastlogsize != h->lastlogsize)
			flags |= TRX_FLAGS_ITEM_DIFF_UPDATE_LASTLOGSIZE;

		if (item->mtime != h->mtime)
			flags |= TRX_FLAGS_ITEM_DIFF_UPDATE_MTIME;
	}

	if (h->state != item->state)
	{
		flags |= TRX_FLAGS_ITEM_DIFF_UPDATE_STATE;

		if (ITEM_STATE_NOTSUPPORTED == h->state)
		{
			treegix_log(LOG_LEVEL_WARNING, "item \"%s:%s\" became not supported: %s",
					item->host.host, item->key_orig, h->value.str);

			trx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_ITEM, item->itemid, &h->ts, h->state, NULL,
					NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL, h->value.err);

			if (0 != strcmp(item->error, h->value.err))
				item_error = h->value.err;
		}
		else
		{
			treegix_log(LOG_LEVEL_WARNING, "item \"%s:%s\" became supported",
					item->host.host, item->key_orig);

			/* we know it's EVENT_OBJECT_ITEM because LLDRULE that becomes */
			/* supported is handled in lld_process_discovery_rule()        */
			trx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_ITEM, item->itemid, &h->ts, h->state,
					NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL, NULL);

			item_error = "";
		}
	}
	else if (ITEM_STATE_NOTSUPPORTED == h->state && 0 != strcmp(item->error, h->value.err))
	{
		treegix_log(LOG_LEVEL_WARNING, "error reason for \"%s:%s\" changed: %s", item->host.host,
				item->key_orig, h->value.err);

		item_error = h->value.err;
	}

	if (NULL != item_error)
		flags |= TRX_FLAGS_ITEM_DIFF_UPDATE_ERROR;

	diff = (trx_item_diff_t *)trx_malloc(NULL, sizeof(trx_item_diff_t));
	diff->itemid = item->itemid;
	diff->lastclock = h->ts.sec;
	diff->flags = flags;

	if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_LASTLOGSIZE & flags))
		diff->lastlogsize = h->lastlogsize;

	if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_MTIME & flags))
		diff->mtime = h->mtime;

	if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_STATE & flags))
		diff->state = h->state;

	if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_ERROR & flags))
		diff->error = item_error;

	return diff;
}

/******************************************************************************
 *                                                                            *
 * Function: DBmass_update_items                                              *
 *                                                                            *
 * Purpose: update item data and inventory in database                        *
 *                                                                            *
 * Parameters: item_diff        - item changes                                *
 *             inventory_values - inventory values                            *
 *                                                                            *
 ******************************************************************************/
static void	DBmass_update_items(const trx_vector_ptr_t *item_diff, const trx_vector_ptr_t *inventory_values)
{
	size_t	sql_offset = 0;
	int	i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < item_diff->values_num; i++)
	{
		trx_item_diff_t	*diff;

		diff = (trx_item_diff_t *)item_diff->values[i];
		if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_DB & diff->flags))
			break;
	}

	if (i != item_diff->values_num || 0 != inventory_values->values_num)
	{
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (i != item_diff->values_num)
			trx_db_save_item_changes(&sql, &sql_alloc, &sql_offset, item_diff);

		if (0 != inventory_values->values_num)
			DCadd_update_inventory_sql(&sql_offset, inventory_values);

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (sql_offset > 16)	/* In ORACLE always present begin..end; */
			DBexecute("%s", sql);

		DCconfig_update_inventory_values(inventory_values);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCmass_proxy_update_items                                        *
 *                                                                            *
 * Purpose: update items info after new value is received                     *
 *                                                                            *
 * Parameters: history     - array of history data                            *
 *             history_num - number of history structures                     *
 *                                                                            *
 * Author: Alexei Vladishev, Eugene Grigorjev, Alexander Vladishev            *
 *                                                                            *
 ******************************************************************************/
static void	DCmass_proxy_update_items(TRX_DC_HISTORY *history, int history_num)
{
	size_t			sql_offset = 0;
	int			i;
	trx_vector_ptr_t	item_diff;
	trx_item_diff_t		*diffs;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&item_diff);
	trx_vector_ptr_reserve(&item_diff, history_num);

	/* preallocate trx_item_diff_t structures for item_diff vector */
	diffs = (trx_item_diff_t *)trx_malloc(NULL, sizeof(trx_item_diff_t) * history_num);

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	for (i = 0; i < history_num; i++)
	{
		trx_item_diff_t	*diff = &diffs[i];

		diff->itemid = history[i].itemid;
		diff->state = history[i].state;
		diff->lastclock = history[i].ts.sec;
		diff->flags = TRX_FLAGS_ITEM_DIFF_UPDATE_STATE | TRX_FLAGS_ITEM_DIFF_UPDATE_LASTCLOCK;

		if (0 != (TRX_DC_FLAG_META & history[i].flags))
		{
			diff->lastlogsize = history[i].lastlogsize;
			diff->mtime = history[i].mtime;
			diff->flags |= TRX_FLAGS_ITEM_DIFF_UPDATE_LASTLOGSIZE | TRX_FLAGS_ITEM_DIFF_UPDATE_MTIME;
		}

		trx_vector_ptr_append(&item_diff, diff);

		if (ITEM_STATE_NOTSUPPORTED == history[i].state)
			continue;

		if (0 == (TRX_DC_FLAG_META & history[i].flags))
			continue;

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"update item_rtdata"
				" set lastlogsize=" TRX_FS_UI64
					",mtime=%d"
				" where itemid=" TRX_FS_UI64 ";\n",
				history[i].lastlogsize, history[i].mtime, history[i].itemid);

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (sql_offset > 16)	/* In ORACLE always present begin..end; */
		DBexecute("%s", sql);

	if (0 != item_diff.values_num)
		DCconfig_items_apply_changes(&item_diff);

	trx_vector_ptr_destroy(&item_diff);
	trx_free(diffs);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DBmass_add_history                                               *
 *                                                                            *
 * Purpose: inserting new history data after new value is received            *
 *                                                                            *
 * Parameters: history     - array of history data                            *
 *             history_num - number of history structures                     *
 *                                                                            *
 ******************************************************************************/
static int	DBmass_add_history(TRX_DC_HISTORY *history, int history_num)
{
	int			i, ret = SUCCEED;
	trx_vector_ptr_t	history_values;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&history_values);
	trx_vector_ptr_reserve(&history_values, history_num);

	for (i = 0; i < history_num; i++)
	{
		TRX_DC_HISTORY	*h = &history[i];

		if (0 != (TRX_DC_FLAGS_NOT_FOR_HISTORY & h->flags))
			continue;

		trx_vector_ptr_append(&history_values, h);
	}

	if (0 != history_values.values_num)
		ret = trx_vc_add_values(&history_values);

	trx_vector_ptr_destroy(&history_values);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_add_proxy_history                                             *
 *                                                                            *
 * Purpose: helper function for DCmass_proxy_add_history()                    *
 *                                                                            *
 * Comment: this function is meant for items with value_type other other than *
 *          ITEM_VALUE_TYPE_LOG not containing meta information in result     *
 *                                                                            *
 ******************************************************************************/
static void	dc_add_proxy_history(TRX_DC_HISTORY *history, int history_num)
{
	int		i;
	unsigned int	flags;
	char		buffer[64], *pvalue;
	trx_db_insert_t	db_insert;

	trx_db_insert_prepare(&db_insert, "proxy_history", "itemid", "clock", "ns", "value", "flags", NULL);

	for (i = 0; i < history_num; i++)
	{
		const TRX_DC_HISTORY	*h = &history[i];

		if (0 != (h->flags & TRX_DC_FLAG_UNDEF))
			continue;

		if (0 != (h->flags & TRX_DC_FLAG_META))
			continue;

		if (ITEM_STATE_NOTSUPPORTED == h->state)
			continue;

		if (0 == (h->flags & TRX_DC_FLAG_NOVALUE))
		{
			switch (h->value_type)
			{
				case ITEM_VALUE_TYPE_FLOAT:
					trx_snprintf(pvalue = buffer, sizeof(buffer), TRX_FS_DBL, h->value.dbl);
					break;
				case ITEM_VALUE_TYPE_UINT64:
					trx_snprintf(pvalue = buffer, sizeof(buffer), TRX_FS_UI64, h->value.ui64);
					break;
				case ITEM_VALUE_TYPE_STR:
				case ITEM_VALUE_TYPE_TEXT:
					pvalue = h->value.str;
					break;
				case ITEM_VALUE_TYPE_LOG:
					continue;
				default:
					THIS_SHOULD_NEVER_HAPPEN;
					continue;
			}
			flags = 0;
		}
		else
		{
			flags = PROXY_HISTORY_FLAG_NOVALUE;
			pvalue = (char *)"";
		}

		trx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, pvalue, flags);
	}

	trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_add_proxy_history_meta                                        *
 *                                                                            *
 * Purpose: helper function for DCmass_proxy_add_history()                    *
 *                                                                            *
 * Comment: this function is meant for items with value_type other other than *
 *          ITEM_VALUE_TYPE_LOG containing meta information in result         *
 *                                                                            *
 ******************************************************************************/
static void	dc_add_proxy_history_meta(TRX_DC_HISTORY *history, int history_num)
{
	int		i;
	char		buffer[64], *pvalue;
	trx_db_insert_t	db_insert;

	trx_db_insert_prepare(&db_insert, "proxy_history", "itemid", "clock", "ns", "value", "lastlogsize", "mtime",
			"flags", NULL);

	for (i = 0; i < history_num; i++)
	{
		unsigned int		flags = PROXY_HISTORY_FLAG_META;
		const TRX_DC_HISTORY	*h = &history[i];

		if (ITEM_STATE_NOTSUPPORTED == h->state)
			continue;

		if (0 != (h->flags & TRX_DC_FLAG_UNDEF))
			continue;

		if (0 == (h->flags & TRX_DC_FLAG_META))
			continue;

		if (ITEM_VALUE_TYPE_LOG == h->value_type)
			continue;

		if (0 == (h->flags & TRX_DC_FLAG_NOVALUE))
		{
			switch (h->value_type)
			{
				case ITEM_VALUE_TYPE_FLOAT:
					trx_snprintf(pvalue = buffer, sizeof(buffer), TRX_FS_DBL, h->value.dbl);
					break;
				case ITEM_VALUE_TYPE_UINT64:
					trx_snprintf(pvalue = buffer, sizeof(buffer), TRX_FS_UI64, h->value.ui64);
					break;
				case ITEM_VALUE_TYPE_STR:
				case ITEM_VALUE_TYPE_TEXT:
					pvalue = h->value.str;
					break;
				default:
					THIS_SHOULD_NEVER_HAPPEN;
					continue;
			}
		}
		else
		{
			flags |= PROXY_HISTORY_FLAG_NOVALUE;
			pvalue = (char *)"";
		}

		trx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, pvalue, h->lastlogsize, h->mtime,
				flags);
	}

	trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_add_proxy_history_log                                         *
 *                                                                            *
 * Purpose: helper function for DCmass_proxy_add_history()                    *
 *                                                                            *
 * Comment: this function is meant for items with value_type                  *
 *          ITEM_VALUE_TYPE_LOG                                               *
 *                                                                            *
 ******************************************************************************/
static void	dc_add_proxy_history_log(TRX_DC_HISTORY *history, int history_num)
{
	int		i;
	trx_db_insert_t	db_insert;

	/* see hc_copy_history_data() for fields that might be uninitialized and need special handling here */
	trx_db_insert_prepare(&db_insert, "proxy_history", "itemid", "clock", "ns", "timestamp", "source", "severity",
			"value", "logeventid", "lastlogsize", "mtime", "flags",  NULL);

	for (i = 0; i < history_num; i++)
	{
		unsigned int		flags;
		trx_uint64_t		lastlogsize;
		int			mtime;
		const TRX_DC_HISTORY	*h = &history[i];

		if (ITEM_STATE_NOTSUPPORTED == h->state)
			continue;

		if (ITEM_VALUE_TYPE_LOG != h->value_type)
			continue;

		if (0 == (h->flags & TRX_DC_FLAG_NOVALUE))
		{
			trx_log_value_t *log = h->value.log;

			if (0 != (h->flags & TRX_DC_FLAG_META))
			{
				flags = PROXY_HISTORY_FLAG_META;
				lastlogsize = h->lastlogsize;
				mtime = h->mtime;
			}
			else
			{
				flags = 0;
				lastlogsize = 0;
				mtime = 0;
			}

			trx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, log->timestamp,
					TRX_NULL2EMPTY_STR(log->source), log->severity, log->value, log->logeventid,
					lastlogsize, mtime, flags);
		}
		else
		{
			/* sent to server only if not 0, see proxy_get_history_data() */
			const int	unset_if_novalue = 0;

			flags = PROXY_HISTORY_FLAG_META | PROXY_HISTORY_FLAG_NOVALUE;

			trx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, unset_if_novalue, "",
					unset_if_novalue, "", unset_if_novalue, h->lastlogsize, h->mtime, flags);
		}
	}

	trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_add_proxy_history_notsupported                                *
 *                                                                            *
 * Purpose: helper function for DCmass_proxy_add_history()                    *
 *                                                                            *
 ******************************************************************************/
static void	dc_add_proxy_history_notsupported(TRX_DC_HISTORY *history, int history_num)
{
	int		i;
	trx_db_insert_t	db_insert;

	trx_db_insert_prepare(&db_insert, "proxy_history", "itemid", "clock", "ns", "value", "state", NULL);

	for (i = 0; i < history_num; i++)
	{
		const TRX_DC_HISTORY	*h = &history[i];

		if (ITEM_STATE_NOTSUPPORTED != h->state)
			continue;

		trx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, TRX_NULL2EMPTY_STR(h->value.err),
				(int)h->state);
	}

	trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);
}

/******************************************************************************
 *                                                                            *
 * Function: DCmass_proxy_add_history                                         *
 *                                                                            *
 * Purpose: inserting new history data after new value is received            *
 *                                                                            *
 * Parameters: history     - array of history data                            *
 *             history_num - number of history structures                     *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
static void	DCmass_proxy_add_history(TRX_DC_HISTORY *history, int history_num)
{
	int	i, h_num = 0, h_meta_num = 0, hlog_num = 0, notsupported_num = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < history_num; i++)
	{
		const TRX_DC_HISTORY	*h = &history[i];

		if (ITEM_STATE_NOTSUPPORTED == h->state)
		{
			notsupported_num++;
			continue;
		}

		switch (h->value_type)
		{
			case ITEM_VALUE_TYPE_LOG:
				hlog_num++;
				break;
			case ITEM_VALUE_TYPE_FLOAT:
			case ITEM_VALUE_TYPE_UINT64:
			case ITEM_VALUE_TYPE_STR:
			case ITEM_VALUE_TYPE_TEXT:
				if (0 != (h->flags & TRX_DC_FLAG_META))
					h_meta_num++;
				else
					h_num++;
				break;
			case ITEM_VALUE_TYPE_NONE:
				h_num++;
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}

	if (0 != h_num)
		dc_add_proxy_history(history, history_num);

	if (0 != h_meta_num)
		dc_add_proxy_history_meta(history, history_num);

	if (0 != hlog_num)
		dc_add_proxy_history_log(history, history_num);

	if (0 != notsupported_num)
		dc_add_proxy_history_notsupported(history, history_num);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCmass_prepare_history                                           *
 *                                                                            *
 * Purpose: prepare history data using items from configuration cache and     *
 *          generate item changes to be applied and host inventory values to  *
 *          be added                                                          *
 *                                                                            *
 * Parameters: history          - [IN/OUT] array of history data              *
 *             itemids          - [IN] the item identifiers                   *
 *                                     (used for item lookup)                 *
 *             items            - [IN] the items                              *
 *             errcodes         - [IN] item error codes                       *
 *             history_num      - [IN] number of history structures           *
 *             item_diff        - [OUT] the changes in item data              *
 *             inventory_values - [OUT] the inventory values to add           *
 *                                                                            *
 ******************************************************************************/
static void	DCmass_prepare_history(TRX_DC_HISTORY *history, const trx_vector_uint64_t *itemids,
		const DC_ITEM *items, const int *errcodes, int history_num, trx_vector_ptr_t *item_diff,
		trx_vector_ptr_t *inventory_values)
{
	int	i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() history_num:%d", __func__, history_num);

	for (i = 0; i < history_num; i++)
	{
		TRX_DC_HISTORY	*h = &history[i];
		const DC_ITEM	*item;
		trx_item_diff_t	*diff;
		int		index;

		if (FAIL == (index = trx_vector_uint64_bsearch(itemids, h->itemid, TRX_DEFAULT_UINT64_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			h->flags |= TRX_DC_FLAG_UNDEF;
			continue;
		}

		if (SUCCEED != errcodes[index])
		{
			h->flags |= TRX_DC_FLAG_UNDEF;
			continue;
		}

		item = &items[index];

		if (ITEM_STATUS_ACTIVE != item->status || HOST_STATUS_MONITORED != item->host.status)
		{
			h->flags |= TRX_DC_FLAG_UNDEF;
			continue;
		}

		if (0 == item->history)
			h->flags |= TRX_DC_FLAG_NOHISTORY;

		if ((ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type) ||
				0 == item->trends)
		{
			h->flags |= TRX_DC_FLAG_NOTRENDS;
		}

		normalize_item_value(item, h);

		diff = calculate_item_update(item, h);
		trx_vector_ptr_append(item_diff, diff);
		DCinventory_value_add(inventory_values, item, h);
	}

	trx_vector_ptr_sort(inventory_values, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCmodule_prepare_history                                         *
 *                                                                            *
 * Purpose: prepare history data to share them with loadable modules, sort    *
 *          data by type skipping low-level discovery data, meta information  *
 *          updates and notsupported items                                    *
 *                                                                            *
 * Parameters: history            - [IN] array of history data                *
 *             history_num        - [IN] number of history structures         *
 *             history_<type>     - [OUT] array of historical data of a       *
 *                                  specific data type                        *
 *             history_<type>_num - [OUT] number of values of a specific      *
 *                                  data type                                 *
 *                                                                            *
 ******************************************************************************/
static void	DCmodule_prepare_history(TRX_DC_HISTORY *history, int history_num, TRX_HISTORY_FLOAT *history_float,
		int *history_float_num, TRX_HISTORY_INTEGER *history_integer, int *history_integer_num,
		TRX_HISTORY_STRING *history_string, int *history_string_num, TRX_HISTORY_TEXT *history_text,
		int *history_text_num, TRX_HISTORY_LOG *history_log, int *history_log_num)
{
	TRX_DC_HISTORY		*h;
	TRX_HISTORY_FLOAT	*h_float;
	TRX_HISTORY_INTEGER	*h_integer;
	TRX_HISTORY_STRING	*h_string;
	TRX_HISTORY_TEXT	*h_text;
	TRX_HISTORY_LOG		*h_log;
	int			i;
	const trx_log_value_t	*log;

	*history_float_num = 0;
	*history_integer_num = 0;
	*history_string_num = 0;
	*history_text_num = 0;
	*history_log_num = 0;

	for (i = 0; i < history_num; i++)
	{
		h = &history[i];

		if (0 != (TRX_DC_FLAGS_NOT_FOR_MODULES & h->flags))
			continue;

		switch (h->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				if (NULL == history_float_cbs)
					continue;

				h_float = &history_float[(*history_float_num)++];
				h_float->itemid = h->itemid;
				h_float->clock = h->ts.sec;
				h_float->ns = h->ts.ns;
				h_float->value = h->value.dbl;
				break;
			case ITEM_VALUE_TYPE_UINT64:
				if (NULL == history_integer_cbs)
					continue;

				h_integer = &history_integer[(*history_integer_num)++];
				h_integer->itemid = h->itemid;
				h_integer->clock = h->ts.sec;
				h_integer->ns = h->ts.ns;
				h_integer->value = h->value.ui64;
				break;
			case ITEM_VALUE_TYPE_STR:
				if (NULL == history_string_cbs)
					continue;

				h_string = &history_string[(*history_string_num)++];
				h_string->itemid = h->itemid;
				h_string->clock = h->ts.sec;
				h_string->ns = h->ts.ns;
				h_string->value = h->value.str;
				break;
			case ITEM_VALUE_TYPE_TEXT:
				if (NULL == history_text_cbs)
					continue;

				h_text = &history_text[(*history_text_num)++];
				h_text->itemid = h->itemid;
				h_text->clock = h->ts.sec;
				h_text->ns = h->ts.ns;
				h_text->value = h->value.str;
				break;
			case ITEM_VALUE_TYPE_LOG:
				if (NULL == history_log_cbs)
					continue;

				log = h->value.log;
				h_log = &history_log[(*history_log_num)++];
				h_log->itemid = h->itemid;
				h_log->clock = h->ts.sec;
				h_log->ns = h->ts.ns;
				h_log->value = log->value;
				h_log->source = TRX_NULL2EMPTY_STR(log->source);
				h_log->timestamp = log->timestamp;
				h_log->logeventid = log->logeventid;
				h_log->severity = log->severity;
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}
}

static void	DCmodule_sync_history(int history_float_num, int history_integer_num, int history_string_num,
		int history_text_num, int history_log_num, TRX_HISTORY_FLOAT *history_float,
		TRX_HISTORY_INTEGER *history_integer, TRX_HISTORY_STRING *history_string,
		TRX_HISTORY_TEXT *history_text, TRX_HISTORY_LOG *history_log)
{
	if (0 != history_float_num)
	{
		int	i;

		treegix_log(LOG_LEVEL_DEBUG, "syncing float history data with modules...");

		for (i = 0; NULL != history_float_cbs[i].module; i++)
		{
			treegix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_float_cbs[i].module->name);
			history_float_cbs[i].history_float_cb(history_float, history_float_num);
		}

		treegix_log(LOG_LEVEL_DEBUG, "synced %d float values with modules", history_float_num);
	}

	if (0 != history_integer_num)
	{
		int	i;

		treegix_log(LOG_LEVEL_DEBUG, "syncing integer history data with modules...");

		for (i = 0; NULL != history_integer_cbs[i].module; i++)
		{
			treegix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_integer_cbs[i].module->name);
			history_integer_cbs[i].history_integer_cb(history_integer, history_integer_num);
		}

		treegix_log(LOG_LEVEL_DEBUG, "synced %d integer values with modules", history_integer_num);
	}

	if (0 != history_string_num)
	{
		int	i;

		treegix_log(LOG_LEVEL_DEBUG, "syncing string history data with modules...");

		for (i = 0; NULL != history_string_cbs[i].module; i++)
		{
			treegix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_string_cbs[i].module->name);
			history_string_cbs[i].history_string_cb(history_string, history_string_num);
		}

		treegix_log(LOG_LEVEL_DEBUG, "synced %d string values with modules", history_string_num);
	}

	if (0 != history_text_num)
	{
		int	i;

		treegix_log(LOG_LEVEL_DEBUG, "syncing text history data with modules...");

		for (i = 0; NULL != history_text_cbs[i].module; i++)
		{
			treegix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_text_cbs[i].module->name);
			history_text_cbs[i].history_text_cb(history_text, history_text_num);
		}

		treegix_log(LOG_LEVEL_DEBUG, "synced %d text values with modules", history_text_num);
	}

	if (0 != history_log_num)
	{
		int	i;

		treegix_log(LOG_LEVEL_DEBUG, "syncing log history data with modules...");

		for (i = 0; NULL != history_log_cbs[i].module; i++)
		{
			treegix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_log_cbs[i].module->name);
			history_log_cbs[i].history_log_cb(history_log, history_log_num);
		}

		treegix_log(LOG_LEVEL_DEBUG, "synced %d log values with modules", history_log_num);
	}
}

static void	sync_proxy_history(int *total_num, int *more)
{
	int			history_num;
	time_t			sync_start;
	trx_vector_ptr_t	history_items;
	TRX_DC_HISTORY		history[TRX_HC_SYNC_MAX];

	trx_vector_ptr_create(&history_items);
	trx_vector_ptr_reserve(&history_items, TRX_HC_SYNC_MAX);

	sync_start = time(NULL);

	do
	{
		*more = TRX_SYNC_DONE;

		LOCK_CACHE;

		hc_pop_items(&history_items);		/* select and take items out of history cache */
		history_num = history_items.values_num;

		UNLOCK_CACHE;

		if (0 == history_num)
			break;

		hc_get_item_values(history, &history_items);	/* copy item data from history cache */

		do
		{
			DBbegin();

			DCmass_proxy_add_history(history, history_num);
			DCmass_proxy_update_items(history, history_num);
		}
		while (TRX_DB_DOWN == DBcommit());

		LOCK_CACHE;

		hc_push_items(&history_items);	/* return items to history cache */
		cache->history_num -= history_num;

		if (0 != hc_queue_get_size())
			*more = TRX_SYNC_MORE;

		UNLOCK_CACHE;

		*total_num += history_num;

		trx_vector_ptr_clear(&history_items);
		hc_free_item_values(history, history_num);

		/* Exit from sync loop if we have spent too much time here */
		/* unless we are doing full sync. This is done to allow    */
		/* syncer process to update their statistics.              */
	}
	while (TRX_SYNC_MORE == *more && TRX_HC_SYNC_TIME_MAX >= time(NULL) - sync_start);

	trx_vector_ptr_destroy(&history_items);
}

/******************************************************************************
 *                                                                            *
 * Function: sync_server_history                                              *
 *                                                                            *
 * Purpose: flush history cache to database, process triggers of flushed      *
 *          and timer triggers from timer queue                               *
 *                                                                            *
 * Parameters: sync_timeout - [IN] the timeout in seconds                     *
 *             values_num   - [IN/OUT] the number of synced values            *
 *             triggers_num - [IN/OUT] the number of processed timers         *
 *             more         - [OUT] a flag indicating the cache emptiness:    *
 *                               TRX_SYNC_DONE - nothing to sync, go idle     *
 *                               TRX_SYNC_MORE - more data to sync            *
 *                                                                            *
 * Comments: This function loops syncing history values by 1k batches and     *
 *           processing timer triggers by batches of 500 triggers.            *
 *           Unless full sync is being done the loop is aborted if either     *
 *           timeout has passed or there are no more data to process.         *
 *           The last is assumed when the following is true:                  *
 *            a) history cache is empty or less than 10% of batch values were *
 *               processed (the other items were locked by triggers)          *
 *            b) less than 500 (full batch) timer triggers were processed     *
 *                                                                            *
 ******************************************************************************/
static void	sync_server_history(int *values_num, int *triggers_num, int *more)
{
	static TRX_HISTORY_FLOAT	*history_float;
	static TRX_HISTORY_INTEGER	*history_integer;
	static TRX_HISTORY_STRING	*history_string;
	static TRX_HISTORY_TEXT		*history_text;
	static TRX_HISTORY_LOG		*history_log;
	int				i, history_num, history_float_num, history_integer_num, history_string_num,
					history_text_num, history_log_num, txn_error;
	time_t				sync_start;
	trx_vector_uint64_t		triggerids, timer_triggerids;
	trx_vector_ptr_t		history_items, trigger_diff, item_diff, inventory_values;
	trx_vector_uint64_pair_t	trends_diff;
	TRX_DC_HISTORY			history[TRX_HC_SYNC_MAX];

	if (NULL == history_float && NULL != history_float_cbs)
	{
		history_float = (TRX_HISTORY_FLOAT *)trx_malloc(history_float,
				TRX_HC_SYNC_MAX * sizeof(TRX_HISTORY_FLOAT));
	}

	if (NULL == history_integer && NULL != history_integer_cbs)
	{
		history_integer = (TRX_HISTORY_INTEGER *)trx_malloc(history_integer,
				TRX_HC_SYNC_MAX * sizeof(TRX_HISTORY_INTEGER));
	}

	if (NULL == history_string && NULL != history_string_cbs)
	{
		history_string = (TRX_HISTORY_STRING *)trx_malloc(history_string,
				TRX_HC_SYNC_MAX * sizeof(TRX_HISTORY_STRING));
	}

	if (NULL == history_text && NULL != history_text_cbs)
	{
		history_text = (TRX_HISTORY_TEXT *)trx_malloc(history_text,
				TRX_HC_SYNC_MAX * sizeof(TRX_HISTORY_TEXT));
	}

	if (NULL == history_log && NULL != history_log_cbs)
	{
		history_log = (TRX_HISTORY_LOG *)trx_malloc(history_log,
				TRX_HC_SYNC_MAX * sizeof(TRX_HISTORY_LOG));
	}

	trx_vector_ptr_create(&inventory_values);
	trx_vector_ptr_create(&item_diff);
	trx_vector_ptr_create(&trigger_diff);
	trx_vector_uint64_pair_create(&trends_diff);

	trx_vector_uint64_create(&triggerids);
	trx_vector_uint64_reserve(&triggerids, TRX_HC_SYNC_MAX);

	trx_vector_uint64_create(&timer_triggerids);
	trx_vector_uint64_reserve(&timer_triggerids, TRX_HC_TIMER_MAX);

	trx_vector_ptr_create(&history_items);
	trx_vector_ptr_reserve(&history_items, TRX_HC_SYNC_MAX);

	sync_start = time(NULL);

	do
	{
		DC_ITEM			*items;
		int			*errcodes, trends_num = 0, timers_num = 0, ret = SUCCEED;
		trx_vector_uint64_t	itemids;
		TRX_DC_TREND		*trends = NULL;

		*more = TRX_SYNC_DONE;

		LOCK_CACHE;
		hc_pop_items(&history_items);		/* select and take items out of history cache */
		UNLOCK_CACHE;

		if (0 != history_items.values_num)
		{
			if (0 == (history_num = DCconfig_lock_triggers_by_history_items(&history_items, &triggerids)))
			{
				LOCK_CACHE;
				hc_push_items(&history_items);
				UNLOCK_CACHE;
				trx_vector_ptr_clear(&history_items);
			}
		}
		else
			history_num = 0;

		if (0 != history_num)
		{
			hc_get_item_values(history, &history_items);	/* copy item data from history cache */

			items = (DC_ITEM *)trx_malloc(NULL, sizeof(DC_ITEM) * (size_t)history_num);
			errcodes = (int *)trx_malloc(NULL, sizeof(int) * (size_t)history_num);

			trx_vector_uint64_create(&itemids);
			trx_vector_uint64_reserve(&itemids, history_num);

			for (i = 0; i < history_num; i++)
				trx_vector_uint64_append(&itemids, history[i].itemid);

			trx_vector_uint64_sort(&itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

			DCconfig_get_items_by_itemids(items, itemids.values, errcodes, history_num);

			DCmass_prepare_history(history, &itemids, items, errcodes, history_num, &item_diff,
					&inventory_values);

			if (FAIL != (ret = DBmass_add_history(history, history_num)))
			{
				DCconfig_items_apply_changes(&item_diff);
				DCmass_update_trends(history, history_num, &trends, &trends_num);

				do
				{
					DBbegin();

					DBmass_update_items(&item_diff, &inventory_values);
					DBmass_update_trends(trends, trends_num, &trends_diff);

					/* process internal events generated by DCmass_prepare_history() */
					trx_process_events(NULL, NULL);

					if (TRX_DB_OK == (txn_error = DBcommit()))
						DCupdate_trends(&trends_diff);
					else
						trx_reset_event_recovery();

					trx_vector_uint64_pair_clear(&trends_diff);
				}
				while (TRX_DB_DOWN == txn_error);
			}

			trx_clean_events();

			trx_vector_ptr_clear_ext(&inventory_values, (trx_clean_func_t)DCinventory_value_free);
			trx_vector_ptr_clear_ext(&item_diff, (trx_clean_func_t)trx_ptr_free);
		}

		if (FAIL != ret)
		{
			trx_dc_get_timer_triggerids(&timer_triggerids, time(NULL), TRX_HC_TIMER_MAX);
			timers_num = timer_triggerids.values_num;

			if (TRX_HC_TIMER_MAX == timers_num)
				*more = TRX_SYNC_MORE;

			if (0 != history_num || 0 != timers_num)
			{
				/* timer triggers do not intersect with item triggers because item triggers */
				/* where already locked and skipped when retrieving timer triggers          */
				trx_vector_uint64_append_array(&triggerids, timer_triggerids.values,
						timer_triggerids.values_num);
				do
				{
					DBbegin();

					recalculate_triggers(history, history_num, &timer_triggerids, &trigger_diff);

					/* process trigger events generated by recalculate_triggers() */
					if (0 != trx_process_events(&trigger_diff, &triggerids))
						trx_db_save_trigger_changes(&trigger_diff);

					if (TRX_DB_OK == (txn_error = DBcommit()))
					{
						DCconfig_triggers_apply_changes(&trigger_diff);
						DBupdate_itservices(&trigger_diff);
					}
					else
						trx_clean_events();

					trx_vector_ptr_clear_ext(&trigger_diff, (trx_clean_func_t)trx_trigger_diff_free);
				}
				while (TRX_DB_DOWN == txn_error);
			}

			trx_vector_uint64_clear(&timer_triggerids);
		}

		if (0 != triggerids.values_num)
		{
			*triggers_num += triggerids.values_num;
			DCconfig_unlock_triggers(&triggerids);
			trx_vector_uint64_clear(&triggerids);
		}

		if (0 != history_num)
		{
			LOCK_CACHE;
			hc_push_items(&history_items);	/* return items to history cache */
			cache->history_num -= history_num;

			if (0 != hc_queue_get_size())
			{
				/* Continue sync if enough of sync candidates were processed       */
				/* (meaning most of sync candidates are not locked by triggers).   */
				/* Otherwise better to wait a bit for other syncers to unlock      */
				/* items rather than trying and failing to sync locked items over  */
				/* and over again.                                                 */
				if (TRX_HC_SYNC_MIN_PCNT <= history_num * 100 / history_items.values_num)
					*more = TRX_SYNC_MORE;
			}

			UNLOCK_CACHE;

			*values_num += history_num;
		}

		if (FAIL != ret)
		{
			if (0 != history_num)
			{
				DCmodule_prepare_history(history, history_num, history_float, &history_float_num,
						history_integer, &history_integer_num, history_string,
						&history_string_num, history_text, &history_text_num, history_log,
						&history_log_num);

				DCmodule_sync_history(history_float_num, history_integer_num, history_string_num,
						history_text_num, history_log_num, history_float, history_integer,
						history_string, history_text, history_log);
			}

			if (SUCCEED == trx_is_export_enabled())
			{
				if (0 != history_num)
				{
					DCexport_history_and_trends(history, history_num, &itemids, items, errcodes,
							trends, trends_num);
				}

				trx_export_events();
			}
		}

		if (0 != history_num || 0 != timers_num)
			trx_clean_events();

		if (0 != history_num)
		{
			trx_free(trends);
			trx_vector_uint64_destroy(&itemids);
			DCconfig_clean_items(items, errcodes, history_num);
			trx_free(errcodes);
			trx_free(items);

			trx_vector_ptr_clear(&history_items);
			hc_free_item_values(history, history_num);
		}

		/* Exit from sync loop if we have spent too much time here.       */
		/* This is done to allow syncer process to update its statistics. */
	}
	while (TRX_SYNC_MORE == *more && TRX_HC_SYNC_TIME_MAX >= time(NULL) - sync_start);

	trx_vector_ptr_destroy(&history_items);
	trx_vector_ptr_destroy(&inventory_values);
	trx_vector_ptr_destroy(&item_diff);
	trx_vector_ptr_destroy(&trigger_diff);
	trx_vector_uint64_pair_destroy(&trends_diff);

	trx_vector_uint64_destroy(&timer_triggerids);
	trx_vector_uint64_destroy(&triggerids);
}

/******************************************************************************
 *                                                                            *
 * Function: sync_history_cache_full                                          *
 *                                                                            *
 * Purpose: writes updates and new data from history cache to database        *
 *                                                                            *
 * Comments: This function is used to flush history cache at server/proxy     *
 *           exit.                                                            *
 *           Other processes are already terminated, so cache locking is      *
 *           unnecessary.                                                     *
 *                                                                            *
 ******************************************************************************/
static void	sync_history_cache_full(void)
{
	int			values_num = 0, triggers_num = 0, more;
	trx_hashset_iter_t	iter;
	trx_hc_item_t		*item;
	trx_binary_heap_t	tmp_history_queue;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() history_num:%d", __func__, cache->history_num);

	/* History index cache might be full without any space left for queueing items from history index to  */
	/* history queue. The solution: replace the shared-memory history queue with heap-allocated one. Add  */
	/* all items from history index to the new history queue.                                             */
	/*                                                                                                    */
	/* Assertions that must be true.                                                                      */
	/*   * This is the main server or proxy process,                                                      */
	/*   * There are no other users of history index cache stored in shared memory. Other processes       */
	/*     should have quit by this point.                                                                */
	/*   * other parts of the program do not hold pointers to the elements of history queue that is       */
	/*     stored in the shared memory.                                                                   */

	if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
	{
		/* unlock all triggers before full sync so no items are locked by triggers */
		DCconfig_unlock_all_triggers();

		/* clear timer trigger queue to avoid processing time triggers at exit */
		trx_dc_clear_timer_queue();
	}

	tmp_history_queue = cache->history_queue;

	trx_binary_heap_create(&cache->history_queue, hc_queue_elem_compare_func, TRX_BINARY_HEAP_OPTION_EMPTY);
	trx_hashset_iter_reset(&cache->history_items, &iter);

	/* add all items from history index to the new history queue */
	while (NULL != (item = (trx_hc_item_t *)trx_hashset_iter_next(&iter)))
	{
		if (NULL != item->tail)
		{
			item->status = TRX_HC_ITEM_STATUS_NORMAL;
			hc_queue_item(item);
		}
	}

	if (0 != hc_queue_get_size())
	{
		treegix_log(LOG_LEVEL_WARNING, "syncing history data...");

		do
		{
			if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
				sync_server_history(&values_num, &triggers_num, &more);
			else
				sync_proxy_history(&values_num, &more);

			treegix_log(LOG_LEVEL_WARNING, "syncing history data... " TRX_FS_DBL "%%",
					(double)values_num / (cache->history_num + values_num) * 100);
		}
		while (0 != hc_queue_get_size());

		treegix_log(LOG_LEVEL_WARNING, "syncing history data done");
	}

	trx_binary_heap_destroy(&cache->history_queue);
	cache->history_queue = tmp_history_queue;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_log_sync_history_cache_progress                              *
 *                                                                            *
 * Purpose: log progress of syncing history data                              *
 *                                                                            *
 ******************************************************************************/
void	trx_log_sync_history_cache_progress(void)
{
	double		pcnt = -1.0;
	int		ts_last, ts_next, sec;

	LOCK_CACHE;

	if (INT_MAX == cache->history_progress_ts)
	{
		UNLOCK_CACHE;
		return;
	}

	ts_last = cache->history_progress_ts;
	sec = time(NULL);

	if (0 == cache->history_progress_ts)
	{
		cache->history_num_total = cache->history_num;
		cache->history_progress_ts = sec;
	}

	if (TRX_HC_SYNC_TIME_MAX <= sec - cache->history_progress_ts || 0 == cache->history_num)
	{
		if (0 != cache->history_num_total)
			pcnt = 100 * (double)(cache->history_num_total - cache->history_num) / cache->history_num_total;

		cache->history_progress_ts = (0 == cache->history_num ? INT_MAX : sec);
	}

	ts_next = cache->history_progress_ts;

	UNLOCK_CACHE;

	if (0 == ts_last)
		treegix_log(LOG_LEVEL_WARNING, "syncing history data in progress... ");

	if (-1.0 != pcnt)
		treegix_log(LOG_LEVEL_WARNING, "syncing history data... " TRX_FS_DBL "%%", pcnt);

	if (INT_MAX == ts_next)
		treegix_log(LOG_LEVEL_WARNING, "syncing history data done");
}

/******************************************************************************
 *                                                                            *
 * Function: trx_sync_history_cache                                           *
 *                                                                            *
 * Purpose: writes updates and new data from history cache to database        *
 *                                                                            *
 * Parameters: values_num - [OUT] the number of synced values                  *
 *             more      - [OUT] a flag indicating the cache emptiness:       *
 *                                TRX_SYNC_DONE - nothing to sync, go idle    *
 *                                TRX_SYNC_MORE - more data to sync           *
 *                                                                            *
 ******************************************************************************/
void	trx_sync_history_cache(int *values_num, int *triggers_num, int *more)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s() history_num:%d", __func__, cache->history_num);

	*values_num = 0;
	*triggers_num = 0;

	if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
		sync_server_history(values_num, triggers_num, more);
	else
		sync_proxy_history(values_num, more);
}

/******************************************************************************
 *                                                                            *
 * local history cache                                                        *
 *                                                                            *
 ******************************************************************************/
static void	dc_string_buffer_realloc(size_t len)
{
	if (string_values_alloc >= string_values_offset + len)
		return;

	do
	{
		string_values_alloc += TRX_STRING_REALLOC_STEP;
	}
	while (string_values_alloc < string_values_offset + len);

	string_values = (char *)trx_realloc(string_values, string_values_alloc);
}

static dc_item_value_t	*dc_local_get_history_slot(void)
{
	if (TRX_MAX_VALUES_LOCAL == item_values_num)
		dc_flush_history();

	if (item_values_alloc == item_values_num)
	{
		item_values_alloc += TRX_STRUCT_REALLOC_STEP;
		item_values = (dc_item_value_t *)trx_realloc(item_values, item_values_alloc * sizeof(dc_item_value_t));
	}

	return &item_values[item_values_num++];
}

static void	dc_local_add_history_dbl(trx_uint64_t itemid, unsigned char item_value_type, const trx_timespec_t *ts,
		double value_orig, trx_uint64_t lastlogsize, int mtime, unsigned char flags)
{
	dc_item_value_t	*item_value;

	item_value = dc_local_get_history_slot();

	item_value->itemid = itemid;
	item_value->ts = *ts;
	item_value->item_value_type = item_value_type;
	item_value->value_type = ITEM_VALUE_TYPE_FLOAT;
	item_value->state = ITEM_STATE_NORMAL;
	item_value->flags = flags;

	if (0 != (item_value->flags & TRX_DC_FLAG_META))
	{
		item_value->lastlogsize = lastlogsize;
		item_value->mtime = mtime;
	}

	if (0 == (item_value->flags & TRX_DC_FLAG_NOVALUE))
		item_value->value.value_dbl = value_orig;
}

static void	dc_local_add_history_uint(trx_uint64_t itemid, unsigned char item_value_type, const trx_timespec_t *ts,
		trx_uint64_t value_orig, trx_uint64_t lastlogsize, int mtime, unsigned char flags)
{
	dc_item_value_t	*item_value;

	item_value = dc_local_get_history_slot();

	item_value->itemid = itemid;
	item_value->ts = *ts;
	item_value->item_value_type = item_value_type;
	item_value->value_type = ITEM_VALUE_TYPE_UINT64;
	item_value->state = ITEM_STATE_NORMAL;
	item_value->flags = flags;

	if (0 != (item_value->flags & TRX_DC_FLAG_META))
	{
		item_value->lastlogsize = lastlogsize;
		item_value->mtime = mtime;
	}

	if (0 == (item_value->flags & TRX_DC_FLAG_NOVALUE))
		item_value->value.value_uint = value_orig;
}

static void	dc_local_add_history_text(trx_uint64_t itemid, unsigned char item_value_type, const trx_timespec_t *ts,
		const char *value_orig, trx_uint64_t lastlogsize, int mtime, unsigned char flags)
{
	dc_item_value_t	*item_value;

	item_value = dc_local_get_history_slot();

	item_value->itemid = itemid;
	item_value->ts = *ts;
	item_value->item_value_type = item_value_type;
	item_value->value_type = ITEM_VALUE_TYPE_TEXT;
	item_value->state = ITEM_STATE_NORMAL;
	item_value->flags = flags;

	if (0 != (item_value->flags & TRX_DC_FLAG_META))
	{
		item_value->lastlogsize = lastlogsize;
		item_value->mtime = mtime;
	}

	if (0 == (item_value->flags & TRX_DC_FLAG_NOVALUE))
	{
		item_value->value.value_str.len = trx_db_strlen_n(value_orig, TRX_HISTORY_VALUE_LEN) + 1;
		dc_string_buffer_realloc(item_value->value.value_str.len);

		item_value->value.value_str.pvalue = string_values_offset;
		memcpy(&string_values[string_values_offset], value_orig, item_value->value.value_str.len);
		string_values_offset += item_value->value.value_str.len;
	}
	else
		item_value->value.value_str.len = 0;
}

static void	dc_local_add_history_log(trx_uint64_t itemid, unsigned char item_value_type, const trx_timespec_t *ts,
		const trx_log_t *log, trx_uint64_t lastlogsize, int mtime, unsigned char flags)
{
	dc_item_value_t	*item_value;

	item_value = dc_local_get_history_slot();

	item_value->itemid = itemid;
	item_value->ts = *ts;
	item_value->item_value_type = item_value_type;
	item_value->value_type = ITEM_VALUE_TYPE_LOG;
	item_value->state = ITEM_STATE_NORMAL;

	item_value->flags = flags;

	if (0 != (item_value->flags & TRX_DC_FLAG_META))
	{
		item_value->lastlogsize = lastlogsize;
		item_value->mtime = mtime;
	}

	if (0 == (item_value->flags & TRX_DC_FLAG_NOVALUE))
	{
		item_value->severity = log->severity;
		item_value->logeventid = log->logeventid;
		item_value->timestamp = log->timestamp;

		item_value->value.value_str.len = trx_db_strlen_n(log->value, TRX_HISTORY_VALUE_LEN) + 1;

		if (NULL != log->source && '\0' != *log->source)
			item_value->source.len = trx_db_strlen_n(log->source, HISTORY_LOG_SOURCE_LEN) + 1;
		else
			item_value->source.len = 0;
	}
	else
	{
		item_value->value.value_str.len = 0;
		item_value->source.len = 0;
	}

	if (0 != item_value->value.value_str.len + item_value->source.len)
	{
		dc_string_buffer_realloc(item_value->value.value_str.len + item_value->source.len);

		if (0 != item_value->value.value_str.len)
		{
			item_value->value.value_str.pvalue = string_values_offset;
			memcpy(&string_values[string_values_offset], log->value, item_value->value.value_str.len);
			string_values_offset += item_value->value.value_str.len;
		}

		if (0 != item_value->source.len)
		{
			item_value->source.pvalue = string_values_offset;
			memcpy(&string_values[string_values_offset], log->source, item_value->source.len);
			string_values_offset += item_value->source.len;
		}
	}
}

static void	dc_local_add_history_notsupported(trx_uint64_t itemid, const trx_timespec_t *ts, const char *error,
		trx_uint64_t lastlogsize, int mtime, unsigned char flags)
{
	dc_item_value_t	*item_value;

	item_value = dc_local_get_history_slot();

	item_value->itemid = itemid;
	item_value->ts = *ts;
	item_value->state = ITEM_STATE_NOTSUPPORTED;
	item_value->flags = flags;

	if (0 != (item_value->flags & TRX_DC_FLAG_META))
	{
		item_value->lastlogsize = lastlogsize;
		item_value->mtime = mtime;
	}

	item_value->value.value_str.len = trx_db_strlen_n(error, ITEM_ERROR_LEN) + 1;
	dc_string_buffer_realloc(item_value->value.value_str.len);
	item_value->value.value_str.pvalue = string_values_offset;
	memcpy(&string_values[string_values_offset], error, item_value->value.value_str.len);
	string_values_offset += item_value->value.value_str.len;
}

static void	dc_local_add_history_lld(trx_uint64_t itemid, const trx_timespec_t *ts, const char *value_orig)
{
	dc_item_value_t	*item_value;

	item_value = dc_local_get_history_slot();

	item_value->itemid = itemid;
	item_value->ts = *ts;
	item_value->state = ITEM_STATE_NORMAL;
	item_value->flags = TRX_DC_FLAG_LLD;
	item_value->value.value_str.len = strlen(value_orig) + 1;

	dc_string_buffer_realloc(item_value->value.value_str.len);
	item_value->value.value_str.pvalue = string_values_offset;
	memcpy(&string_values[string_values_offset], value_orig, item_value->value.value_str.len);
	string_values_offset += item_value->value.value_str.len;
}

static void	dc_local_add_history_empty(trx_uint64_t itemid, unsigned char item_value_type, const trx_timespec_t *ts,
		unsigned char flags)
{
	dc_item_value_t	*item_value;

	item_value = dc_local_get_history_slot();

	item_value->itemid = itemid;
	item_value->ts = *ts;
	item_value->item_value_type = item_value_type;
	item_value->value_type = ITEM_VALUE_TYPE_NONE;
	item_value->state = ITEM_STATE_NORMAL;
	item_value->flags = flags;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_add_history                                                   *
 *                                                                            *
 * Purpose: add new value to the cache                                        *
 *                                                                            *
 * Parameters:  itemid          - [IN] the itemid                             *
 *              item_value_type - [IN] the item value type                    *
 *              item_flags      - [IN] the item flags (e. g. lld rule)        *
 *              result          - [IN] agent result containing the value      *
 *                                to add                                      *
 *              ts              - [IN] the value timestamp                    *
 *              state           - [IN] the item state                         *
 *              error           - [IN] the error message in case item state   *
 *                                is ITEM_STATE_NOTSUPPORTED                  *
 *                                                                            *
 ******************************************************************************/
void	dc_add_history(trx_uint64_t itemid, unsigned char item_value_type, unsigned char item_flags,
		AGENT_RESULT *result, const trx_timespec_t *ts, unsigned char state, const char *error)
{
	unsigned char	value_flags;

	if (ITEM_STATE_NOTSUPPORTED == state)
	{
		trx_uint64_t	lastlogsize;
		int		mtime;

		if (NULL != result && 0 != ISSET_META(result))
		{
			value_flags = TRX_DC_FLAG_META;
			lastlogsize = result->lastlogsize;
			mtime = result->mtime;
		}
		else
		{
			value_flags = 0;
			lastlogsize = 0;
			mtime = 0;
		}
		dc_local_add_history_notsupported(itemid, ts, error, lastlogsize, mtime, value_flags);
		return;
	}

	if (0 != (TRX_FLAG_DISCOVERY_RULE & item_flags))
	{
		if (NULL == GET_TEXT_RESULT(result))
			return;

		/* proxy stores low-level discovery (lld) values in db */
		if (0 == (TRX_PROGRAM_TYPE_SERVER & program_type))
			dc_local_add_history_lld(itemid, ts, result->text);

		return;
	}

	/* allow proxy to send timestamps of empty (throttled etc) values to update nextchecks for queue */
	if (!ISSET_VALUE(result) && !ISSET_META(result) && 0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
		return;

	value_flags = 0;

	if (!ISSET_VALUE(result))
		value_flags |= TRX_DC_FLAG_NOVALUE;

	if (ISSET_META(result))
		value_flags |= TRX_DC_FLAG_META;

	/* Add data to the local history cache if:                                           */
	/*   1) the NOVALUE flag is set (data contains either meta information or timestamp) */
	/*   2) the NOVALUE flag is not set and value conversion succeeded                   */

	if (0 == (value_flags & TRX_DC_FLAG_NOVALUE))
	{
		if (ISSET_LOG(result))
		{
			dc_local_add_history_log(itemid, item_value_type, ts, result->log, result->lastlogsize,
					result->mtime, value_flags);
		}
		else if (ISSET_UI64(result))
		{
			dc_local_add_history_uint(itemid, item_value_type, ts, result->ui64, result->lastlogsize,
					result->mtime, value_flags);
		}
		else if (ISSET_DBL(result))
		{
			dc_local_add_history_dbl(itemid, item_value_type, ts, result->dbl, result->lastlogsize,
					result->mtime, value_flags);
		}
		else if (ISSET_STR(result))
		{
			dc_local_add_history_text(itemid, item_value_type, ts, result->str, result->lastlogsize,
					result->mtime, value_flags);
		}
		else if (ISSET_TEXT(result))
		{
			dc_local_add_history_text(itemid, item_value_type, ts, result->text, result->lastlogsize,
					result->mtime, value_flags);
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
		}
	}
	else
	{
		if (0 != (value_flags & TRX_DC_FLAG_META))
		{
			dc_local_add_history_log(itemid, item_value_type, ts, NULL, result->lastlogsize, result->mtime,
					value_flags);
		}
		else
			dc_local_add_history_empty(itemid, item_value_type, ts, value_flags);
	}
}

void	dc_flush_history(void)
{
	if (0 == item_values_num)
		return;

	LOCK_CACHE;

	hc_add_item_values(item_values, item_values_num);

	cache->history_num += item_values_num;

	UNLOCK_CACHE;

	item_values_num = 0;
	string_values_offset = 0;
}

/******************************************************************************
 *                                                                            *
 * history cache storage                                                      *
 *                                                                            *
 ******************************************************************************/
TRX_MEM_FUNC_IMPL(__hc_index, hc_index_mem)
TRX_MEM_FUNC_IMPL(__hc, hc_mem)

/******************************************************************************
 *                                                                            *
 * Function: hc_queue_elem_compare_func                                       *
 *                                                                            *
 * Purpose: compares history queue elements                                   *
 *                                                                            *
 ******************************************************************************/
static int	hc_queue_elem_compare_func(const void *d1, const void *d2)
{
	const trx_binary_heap_elem_t	*e1 = (const trx_binary_heap_elem_t *)d1;
	const trx_binary_heap_elem_t	*e2 = (const trx_binary_heap_elem_t *)d2;

	const trx_hc_item_t	*item1 = (const trx_hc_item_t *)e1->data;
	const trx_hc_item_t	*item2 = (const trx_hc_item_t *)e2->data;

	/* compare by timestamp of the oldest value */
	return trx_timespec_compare(&item1->tail->ts, &item2->tail->ts);
}

/******************************************************************************
 *                                                                            *
 * Function: hc_free_data                                                     *
 *                                                                            *
 * Purpose: free history item data allocated in history cache                 *
 *                                                                            *
 * Parameters: data - [IN] history item data                                  *
 *                                                                            *
 ******************************************************************************/
static void	hc_free_data(trx_hc_data_t *data)
{
	if (ITEM_STATE_NOTSUPPORTED == data->state)
	{
		__hc_mem_free_func(data->value.str);
	}
	else
	{
		if (0 == (data->flags & TRX_DC_FLAG_NOVALUE))
		{
			switch (data->value_type)
			{
				case ITEM_VALUE_TYPE_STR:
				case ITEM_VALUE_TYPE_TEXT:
					__hc_mem_free_func(data->value.str);
					break;
				case ITEM_VALUE_TYPE_LOG:
					__hc_mem_free_func(data->value.log->value);

					if (NULL != data->value.log->source)
						__hc_mem_free_func(data->value.log->source);

					__hc_mem_free_func(data->value.log);
					break;
			}
		}
	}

	__hc_mem_free_func(data);
}

/******************************************************************************
 *                                                                            *
 * Function: hc_queue_item                                                    *
 *                                                                            *
 * Purpose: put back item into history queue                                  *
 *                                                                            *
 * Parameters: data - [IN] history item data                                  *
 *                                                                            *
 ******************************************************************************/
static void	hc_queue_item(trx_hc_item_t *item)
{
	trx_binary_heap_elem_t	elem = {item->itemid, (const void *)item};

	trx_binary_heap_insert(&cache->history_queue, &elem);
}

/******************************************************************************
 *                                                                            *
 * Function: hc_get_item                                                      *
 *                                                                            *
 * Purpose: returns history item by itemid                                    *
 *                                                                            *
 * Parameters: itemid - [IN] the item id                                      *
 *                                                                            *
 * Return value: the history item or NULL if the requested item is not in     *
 *               history cache                                                *
 *                                                                            *
 ******************************************************************************/
static trx_hc_item_t	*hc_get_item(trx_uint64_t itemid)
{
	return (trx_hc_item_t *)trx_hashset_search(&cache->history_items, &itemid);
}

/******************************************************************************
 *                                                                            *
 * Function: hc_add_item                                                      *
 *                                                                            *
 * Purpose: adds a new item to history cache                                  *
 *                                                                            *
 * Parameters: itemid - [IN] the item id                                      *
 *                      [IN] the item data                                    *
 *                                                                            *
 * Return value: the added history item                                       *
 *                                                                            *
 ******************************************************************************/
static trx_hc_item_t	*hc_add_item(trx_uint64_t itemid, trx_hc_data_t *data)
{
	trx_hc_item_t	item_local = {itemid, TRX_HC_ITEM_STATUS_NORMAL, data, data};

	return (trx_hc_item_t *)trx_hashset_insert(&cache->history_items, &item_local, sizeof(item_local));
}

/******************************************************************************
 *                                                                            *
 * Function: hc_mem_value_str_dup                                             *
 *                                                                            *
 * Purpose: copies string value to history cache                              *
 *                                                                            *
 * Parameters: str - [IN] the string value                                    *
 *                                                                            *
 * Return value: the copied string or NULL if there was not enough memory     *
 *                                                                            *
 ******************************************************************************/
static char	*hc_mem_value_str_dup(const dc_value_str_t *str)
{
	char	*ptr;

	if (NULL == (ptr = (char *)__hc_mem_malloc_func(NULL, str->len)))
		return NULL;

	memcpy(ptr, &string_values[str->pvalue], str->len - 1);
	ptr[str->len - 1] = '\0';

	return ptr;
}

/******************************************************************************
 *                                                                            *
 * Function: hc_clone_history_str_data                                        *
 *                                                                            *
 * Purpose: clones string value into history data memory                      *
 *                                                                            *
 * Parameters: dst - [IN/OUT] a reference to the cloned value                 *
 *             str - [IN] the string value to clone                           *
 *                                                                            *
 * Return value: SUCCESS - either there was no need to clone the string       *
 *                         (it was empty or already cloned) or the string was *
 *                          cloned successfully                               *
 *               FAIL    - not enough memory                                  *
 *                                                                            *
 * Comments: This function can be called in loop with the same dst value      *
 *           until it finishes cloning string value.                          *
 *                                                                            *
 ******************************************************************************/
static int	hc_clone_history_str_data(char **dst, const dc_value_str_t *str)
{
	if (0 == str->len)
		return SUCCEED;

	if (NULL != *dst)
		return SUCCEED;

	if (NULL != (*dst = hc_mem_value_str_dup(str)))
		return SUCCEED;

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: hc_clone_history_log_data                                        *
 *                                                                            *
 * Purpose: clones log value into history data memory                         *
 *                                                                            *
 * Parameters: dst        - [IN/OUT] a reference to the cloned value          *
 *             item_value - [IN] the log value to clone                       *
 *                                                                            *
 * Return value: SUCCESS - the log value was cloned successfully              *
 *               FAIL    - not enough memory                                  *
 *                                                                            *
 * Comments: This function can be called in loop with the same dst value      *
 *           until it finishes cloning log value.                             *
 *                                                                            *
 ******************************************************************************/
static int	hc_clone_history_log_data(trx_log_value_t **dst, const dc_item_value_t *item_value)
{
	if (NULL == *dst)
	{
		/* using realloc instead of malloc just to suppress 'not used' warning for realloc */
		if (NULL == (*dst = (trx_log_value_t *)__hc_mem_realloc_func(NULL, sizeof(trx_log_value_t))))
			return FAIL;

		memset(*dst, 0, sizeof(trx_log_value_t));
	}

	if (SUCCEED != hc_clone_history_str_data(&(*dst)->value, &item_value->value.value_str))
		return FAIL;

	if (SUCCEED != hc_clone_history_str_data(&(*dst)->source, &item_value->source))
		return FAIL;

	(*dst)->logeventid = item_value->logeventid;
	(*dst)->severity = item_value->severity;
	(*dst)->timestamp = item_value->timestamp;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: hc_clone_history_data                                            *
 *                                                                            *
 * Purpose: clones item value from local cache into history cache             *
 *                                                                            *
 * Parameters: data       - [IN/OUT] a reference to the cloned value          *
 *             item_value - [IN] the item value                               *
 *                                                                            *
 * Return value: SUCCESS - the item value was cloned successfully             *
 *               FAIL    - not enough memory                                  *
 *                                                                            *
 * Comments: This function can be called in loop with the same data value     *
 *           until it finishes cloning item value.                            *
 *                                                                            *
 ******************************************************************************/
static int	hc_clone_history_data(trx_hc_data_t **data, const dc_item_value_t *item_value)
{
	if (NULL == *data)
	{
		if (NULL == (*data = (trx_hc_data_t *)__hc_mem_malloc_func(NULL, sizeof(trx_hc_data_t))))
			return FAIL;

		memset(*data, 0, sizeof(trx_hc_data_t));

		(*data)->state = item_value->state;
		(*data)->ts = item_value->ts;
		(*data)->flags = item_value->flags;
	}

	if (0 != (TRX_DC_FLAG_META & item_value->flags))
	{
		(*data)->lastlogsize = item_value->lastlogsize;
		(*data)->mtime = item_value->mtime;
	}

	if (ITEM_STATE_NOTSUPPORTED == item_value->state)
	{
		if (NULL == ((*data)->value.str = hc_mem_value_str_dup(&item_value->value.value_str)))
			return FAIL;

		(*data)->value_type = item_value->value_type;
		cache->stats.notsupported_counter++;

		return SUCCEED;
	}

	if (0 != (TRX_DC_FLAG_LLD & item_value->flags))
	{
		if (NULL == ((*data)->value.str = hc_mem_value_str_dup(&item_value->value.value_str)))
			return FAIL;

		(*data)->value_type = ITEM_VALUE_TYPE_TEXT;

		cache->stats.history_text_counter++;
		cache->stats.history_counter++;

		return SUCCEED;
	}

	if (0 == (TRX_DC_FLAG_NOVALUE & item_value->flags))
	{
		switch (item_value->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				(*data)->value.dbl = item_value->value.value_dbl;
				break;
			case ITEM_VALUE_TYPE_UINT64:
				(*data)->value.ui64 = item_value->value.value_uint;
				break;
			case ITEM_VALUE_TYPE_STR:
				if (SUCCEED != hc_clone_history_str_data(&(*data)->value.str,
						&item_value->value.value_str))
				{
					return FAIL;
				}
				break;
			case ITEM_VALUE_TYPE_TEXT:
				if (SUCCEED != hc_clone_history_str_data(&(*data)->value.str,
						&item_value->value.value_str))
				{
					return FAIL;
				}
				break;
			case ITEM_VALUE_TYPE_LOG:
				if (SUCCEED != hc_clone_history_log_data(&(*data)->value.log, item_value))
					return FAIL;
				break;
		}

		switch (item_value->item_value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				cache->stats.history_float_counter++;
				break;
			case ITEM_VALUE_TYPE_UINT64:
				cache->stats.history_uint_counter++;
				break;
			case ITEM_VALUE_TYPE_STR:
				cache->stats.history_str_counter++;
				break;
			case ITEM_VALUE_TYPE_TEXT:
				cache->stats.history_text_counter++;
				break;
			case ITEM_VALUE_TYPE_LOG:
				cache->stats.history_log_counter++;
				break;
		}

		cache->stats.history_counter++;
	}

	(*data)->value_type = item_value->value_type;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: hc_add_item_values                                               *
 *                                                                            *
 * Purpose: adds item values to the history cache                             *
 *                                                                            *
 * Parameters: values     - [IN] the item values to add                       *
 *             values_num - [IN] the number of item values to add             *
 *                                                                            *
 * Comments: If the history cache is full this function will wait until       *
 *           history syncers processes values freeing enough space to store   *
 *           the new value.                                                   *
 *                                                                            *
 ******************************************************************************/
static void	hc_add_item_values(dc_item_value_t *values, int values_num)
{
	dc_item_value_t	*item_value;
	int		i;
	trx_hc_item_t	*item;

	for (i = 0; i < values_num; i++)
	{
		trx_hc_data_t	*data = NULL;

		item_value = &values[i];

		while (SUCCEED != hc_clone_history_data(&data, item_value))
		{
			UNLOCK_CACHE;

			treegix_log(LOG_LEVEL_DEBUG, "History cache is full. Sleeping for 1 second.");
			sleep(1);

			LOCK_CACHE;
		}

		if (NULL == (item = hc_get_item(item_value->itemid)))
		{
			item = hc_add_item(item_value->itemid, data);
			hc_queue_item(item);
		}
		else
		{
			item->head->next = data;
			item->head = data;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: hc_copy_history_data                                             *
 *                                                                            *
 * Purpose: copies item value from history cache into the specified history   *
 *          value                                                             *
 *                                                                            *
 * Parameters: history - [OUT] the history value                              *
 *             itemid  - [IN] the item identifier                             *
 *             data    - [IN] the history data to copy                        *
 *                                                                            *
 * Comments: handling of uninitialized fields in dc_add_proxy_history_log()   *
 *                                                                            *
 ******************************************************************************/
static void	hc_copy_history_data(TRX_DC_HISTORY *history, trx_uint64_t itemid, trx_hc_data_t *data)
{
	history->itemid = itemid;
	history->ts = data->ts;
	history->state = data->state;
	history->flags = data->flags;
	history->lastlogsize = data->lastlogsize;
	history->mtime = data->mtime;

	if (ITEM_STATE_NOTSUPPORTED == data->state)
	{
		history->value.err = trx_strdup(NULL, data->value.str);
		history->flags |= TRX_DC_FLAG_UNDEF;
		return;
	}

	history->value_type = data->value_type;

	if (0 == (TRX_DC_FLAG_NOVALUE & data->flags))
	{
		switch (data->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				history->value.dbl = data->value.dbl;
				break;
			case ITEM_VALUE_TYPE_UINT64:
				history->value.ui64 = data->value.ui64;
				break;
			case ITEM_VALUE_TYPE_STR:
			case ITEM_VALUE_TYPE_TEXT:
				history->value.str = trx_strdup(NULL, data->value.str);
				break;
			case ITEM_VALUE_TYPE_LOG:
				history->value.log = (trx_log_value_t *)trx_malloc(NULL, sizeof(trx_log_value_t));
				history->value.log->value = trx_strdup(NULL, data->value.log->value);

				if (NULL != data->value.log->source)
					history->value.log->source = trx_strdup(NULL, data->value.log->source);
				else
					history->value.log->source = NULL;

				history->value.log->timestamp = data->value.log->timestamp;
				history->value.log->severity = data->value.log->severity;
				history->value.log->logeventid = data->value.log->logeventid;

				break;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: hc_pop_items                                                     *
 *                                                                            *
 * Purpose: pops the next batch of history items from cache for processing    *
 *                                                                            *
 * Parameters: history_items - [OUT] the locked history items                 *
 *                                                                            *
 * Comments: The history_items must be returned back to history cache with    *
 *           hc_push_items() function after they have been processed.         *
 *                                                                            *
 ******************************************************************************/
static void	hc_pop_items(trx_vector_ptr_t *history_items)
{
	trx_binary_heap_elem_t	*elem;
	trx_hc_item_t		*item;

	while (TRX_HC_SYNC_MAX > history_items->values_num && FAIL == trx_binary_heap_empty(&cache->history_queue))
	{
		elem = trx_binary_heap_find_min(&cache->history_queue);
		item = (trx_hc_item_t *)elem->data;
		trx_vector_ptr_append(history_items, item);

		trx_binary_heap_remove_min(&cache->history_queue);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: hc_get_item_values                                               *
 *                                                                            *
 * Purpose: gets item history values                                          *
 *                                                                            *
 * Parameters: history       - [OUT] the history valeus                       *
 *             history_items - [IN] the history items                         *
 *                                                                            *
 ******************************************************************************/
static void	hc_get_item_values(TRX_DC_HISTORY *history, trx_vector_ptr_t *history_items)
{
	int		i, history_num = 0;
	trx_hc_item_t	*item;

	/* we don't need to lock history cache because no other processes can  */
	/* change item's history data until it is pushed back to history queue */
	for (i = 0; i < history_items->values_num; i++)
	{
		item = (trx_hc_item_t *)history_items->values[i];

		if (TRX_HC_ITEM_STATUS_BUSY == item->status)
			continue;

		hc_copy_history_data(&history[history_num++], item->itemid, item->tail);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: hc_push_processed_items                                          *
 *                                                                            *
 * Purpose: push back the processed history items into history cache          *
 *                                                                            *
 * Parameters: history_items - [IN] the history items containing processed    *
 *                                  (available) and busy items                *
 *                                                                            *
 * Comments: This function removes processed value from history cache.        *
 *           If there is no more data for this item, then the item itself is  *
 *           removed from history index.                                      *
 *                                                                            *
 ******************************************************************************/
void	hc_push_items(trx_vector_ptr_t *history_items)
{
	int		i;
	trx_hc_item_t	*item;
	trx_hc_data_t	*data_free;

	for (i = 0; i < history_items->values_num; i++)
	{
		item = (trx_hc_item_t *)history_items->values[i];

		switch (item->status)
		{
			case TRX_HC_ITEM_STATUS_BUSY:
				/* reset item status before returning it to queue */
				item->status = TRX_HC_ITEM_STATUS_NORMAL;
				hc_queue_item(item);
				break;
			case TRX_HC_ITEM_STATUS_NORMAL:
				data_free = item->tail;
				item->tail = item->tail->next;
				hc_free_data(data_free);
				if (NULL == item->tail)
					trx_hashset_remove(&cache->history_items, item);
				else
					hc_queue_item(item);
				break;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: hc_queue_get_size                                                *
 *                                                                            *
 * Purpose: retrieve the size of history queue                                *
 *                                                                            *
 ******************************************************************************/
int	hc_queue_get_size(void)
{
	return cache->history_queue.elems_num;
}

/******************************************************************************
 *                                                                            *
 * Function: init_trend_cache                                                 *
 *                                                                            *
 * Purpose: Allocate shared memory for trend cache (part of database cache)   *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 * Comments: Is optionally called from init_database_cache()                  *
 *                                                                            *
 ******************************************************************************/

TRX_MEM_FUNC_IMPL(__trend, trend_mem)

static int	init_trend_cache(char **error)
{
	size_t	sz;
	int	ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != (ret = trx_mutex_create(&trends_lock, TRX_MUTEX_TRENDS, error)))
		goto out;

	sz = trx_mem_required_size(1, "trend cache", "TrendCacheSize");
	if (SUCCEED != (ret = trx_mem_create(&trend_mem, CONFIG_TRENDS_CACHE_SIZE, "trend cache", "TrendCacheSize", 0,
			error)))
	{
		goto out;
	}

	CONFIG_TRENDS_CACHE_SIZE -= sz;

	cache->trends_num = 0;
	cache->trends_last_cleanup_hour = 0;

#define INIT_HASHSET_SIZE	100	/* Should be calculated dynamically based on trends size? */
					/* Still does not make sense to have it more than initial */
					/* item hashset size in configuration cache.              */

	trx_hashset_create_ext(&cache->trends, INIT_HASHSET_SIZE,
			TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC, NULL,
			__trend_mem_malloc_func, __trend_mem_realloc_func, __trend_mem_free_func);

#undef INIT_HASHSET_SIZE
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: init_database_cache                                              *
 *                                                                            *
 * Purpose: Allocate shared memory for database cache                         *
 *                                                                            *
 * Author: Alexei Vladishev, Alexander Vladishev                              *
 *                                                                            *
 ******************************************************************************/
int	init_database_cache(char **error)
{
	int	ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != (ret = trx_mutex_create(&cache_lock, TRX_MUTEX_CACHE, error)))
		goto out;

	if (SUCCEED != (ret = trx_mutex_create(&cache_ids_lock, TRX_MUTEX_CACHE_IDS, error)))
		goto out;

	if (SUCCEED != (ret = trx_mem_create(&hc_mem, CONFIG_HISTORY_CACHE_SIZE, "history cache",
			"HistoryCacheSize", 1, error)))
	{
		goto out;
	}

	if (SUCCEED != (ret = trx_mem_create(&hc_index_mem, CONFIG_HISTORY_INDEX_CACHE_SIZE, "history index cache",
			"HistoryIndexCacheSize", 0, error)))
	{
		goto out;
	}

	cache = (TRX_DC_CACHE *)__hc_index_mem_malloc_func(NULL, sizeof(TRX_DC_CACHE));
	memset(cache, 0, sizeof(TRX_DC_CACHE));

	ids = (TRX_DC_IDS *)__hc_index_mem_malloc_func(NULL, sizeof(TRX_DC_IDS));
	memset(ids, 0, sizeof(TRX_DC_IDS));

	trx_hashset_create_ext(&cache->history_items, TRX_HC_ITEMS_INIT_SIZE,
			TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC, NULL,
			__hc_index_mem_malloc_func, __hc_index_mem_realloc_func, __hc_index_mem_free_func);

	trx_binary_heap_create_ext(&cache->history_queue, hc_queue_elem_compare_func, TRX_BINARY_HEAP_OPTION_EMPTY,
			__hc_index_mem_malloc_func, __hc_index_mem_realloc_func, __hc_index_mem_free_func);

	if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
	{
		if (SUCCEED != (ret = init_trend_cache(error)))
			goto out;
	}

	cache->history_num_total = 0;
	cache->history_progress_ts = 0;

	if (NULL == sql)
		sql = (char *)trx_malloc(sql, sql_alloc);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_all                                                       *
 *                                                                            *
 * Purpose: writes updates and new data from pool and cache data to database  *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_all(void)
{
	treegix_log(LOG_LEVEL_DEBUG, "In DCsync_all()");

	sync_history_cache_full();
	if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
		DCsync_trends();

	treegix_log(LOG_LEVEL_DEBUG, "End of DCsync_all()");
}

/******************************************************************************
 *                                                                            *
 * Function: free_database_cache                                              *
 *                                                                            *
 * Purpose: Free memory allocated for database cache                          *
 *                                                                            *
 * Author: Alexei Vladishev, Alexander Vladishev                              *
 *                                                                            *
 ******************************************************************************/
void	free_database_cache(void)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	DCsync_all();

	cache = NULL;

	trx_mutex_destroy(&cache_lock);
	trx_mutex_destroy(&cache_ids_lock);

	if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
		trx_mutex_destroy(&trends_lock);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_nextid                                                     *
 *                                                                            *
 * Purpose: Return next id for requested table                                *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
trx_uint64_t	DCget_nextid(const char *table_name, int num)
{
	int		i;
	DB_RESULT	result;
	DB_ROW		row;
	const TRX_TABLE	*table;
	TRX_DC_ID	*id;
	trx_uint64_t	min = 0, max = TRX_DB_MAX_ID, nextid, lastid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() table:'%s' num:%d", __func__, table_name, num);

	LOCK_CACHE_IDS;

	for (i = 0; i < TRX_IDS_SIZE; i++)
	{
		id = &ids->id[i];
		if ('\0' == *id->table_name)
			break;

		if (0 == strcmp(id->table_name, table_name))
		{
			nextid = id->lastid + 1;
			id->lastid += num;
			lastid = id->lastid;

			UNLOCK_CACHE_IDS;

			treegix_log(LOG_LEVEL_DEBUG, "End of %s() table:'%s' [" TRX_FS_UI64 ":" TRX_FS_UI64 "]",
					__func__, table_name, nextid, lastid);

			return nextid;
		}
	}

	if (i == TRX_IDS_SIZE)
	{
		treegix_log(LOG_LEVEL_ERR, "insufficient shared memory for ids");
		exit(EXIT_FAILURE);
	}

	table = DBget_table(table_name);

	result = DBselect("select max(%s) from %s where %s between " TRX_FS_UI64 " and " TRX_FS_UI64,
			table->recid, table_name, table->recid, min, max);

	if (NULL != result)
	{
		trx_strlcpy(id->table_name, table_name, sizeof(id->table_name));

		if (NULL == (row = DBfetch(result)) || SUCCEED == DBis_null(row[0]))
			id->lastid = min;
		else
			TRX_STR2UINT64(id->lastid, row[0]);

		nextid = id->lastid + 1;
		id->lastid += num;
		lastid = id->lastid;
	}
	else
		nextid = lastid = 0;

	UNLOCK_CACHE_IDS;

	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() table:'%s' [" TRX_FS_UI64 ":" TRX_FS_UI64 "]",
			__func__, table_name, nextid, lastid);

	return nextid;
}

/******************************************************************************
 *                                                                            *
 * Function: DCupdate_hosts_availability                                      *
 *                                                                            *
 * Purpose: performs host availability reset for hosts with availability set  *
 *          on interfaces without enabled items                               *
 *                                                                            *
 ******************************************************************************/
void	DCupdate_hosts_availability(void)
{
	trx_vector_ptr_t	hosts;
	char			*sql_buf = NULL;
	size_t			sql_buf_alloc = 0, sql_buf_offset = 0;
	int			i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&hosts);

	if (SUCCEED != DCreset_hosts_availability(&hosts))
		goto out;

	DBbegin();
	DBbegin_multiple_update(&sql_buf, &sql_buf_alloc, &sql_buf_offset);

	for (i = 0; i < hosts.values_num; i++)
	{
		if (SUCCEED != trx_sql_add_host_availability(&sql_buf, &sql_buf_alloc, &sql_buf_offset,
				(trx_host_availability_t *)hosts.values[i]))
		{
			continue;
		}

		trx_strcpy_alloc(&sql_buf, &sql_buf_alloc, &sql_buf_offset, ";\n");
		DBexecute_overflowed_sql(&sql_buf, &sql_buf_alloc, &sql_buf_offset);
	}

	DBend_multiple_update(&sql_buf, &sql_buf_alloc, &sql_buf_offset);

	if (16 < sql_buf_offset)
		DBexecute("%s", sql_buf);

	DBcommit();

	trx_free(sql_buf);
out:
	trx_vector_ptr_clear_ext(&hosts, (trx_mem_free_func_t)trx_host_availability_free);
	trx_vector_ptr_destroy(&hosts);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
