

#include "common.h"
#include "trxjson.h"
#include "dbcache.h"
#include "trxself.h"
#include "valuecache.h"
#include "../../treegix_server/vmware/vmware.h"
#include "preproc.h"

#include "treegix_stats.h"

extern unsigned char	program_type;

/******************************************************************************
 *                                                                            *
 * Function: trx_send_treegix_stats                                            *
 *                                                                            *
 * Purpose: collects all metrics required for Treegix stats request            *
 *                                                                            *
 * Parameters: json - [OUT] the json data                                     *
 *                                                                            *
 ******************************************************************************/
void	trx_get_treegix_stats(struct trx_json *json)
{
	trx_config_cache_info_t	count_stats;
	trx_vmware_stats_t	vmware_stats;
	trx_wcache_info_t	wcache_info;
	trx_process_info_t	process_stats[TRX_PROCESS_TYPE_COUNT];
	int			proc_type;

	DCget_count_stats_all(&count_stats);

	/* treegix[boottime] */
	trx_json_adduint64(json, "boottime", CONFIG_SERVER_STARTUP_TIME);

	/* treegix[uptime] */
	trx_json_adduint64(json, "uptime", time(NULL) - CONFIG_SERVER_STARTUP_TIME);

	/* treegix[hosts] */
	trx_json_adduint64(json, "hosts", count_stats.hosts);

	/* treegix[items] */
	trx_json_adduint64(json, "items", count_stats.items);

	/* treegix[item_unsupported] */
	trx_json_adduint64(json, "item_unsupported", count_stats.items_unsupported);

	/* treegix[requiredperformance] */
	trx_json_addfloat(json, "requiredperformance", count_stats.requiredperformance);

	/* treegix[preprocessing_queue] */
	trx_json_adduint64(json, "preprocessing_queue", trx_preprocessor_get_queue_size());

	trx_get_treegix_stats_ext(json);

	/* treegix[rcache,<cache>,<mode>] */
	trx_json_addobject(json, "rcache");
	trx_json_adduint64(json, "total", *(trx_uint64_t *)DCconfig_get_stats(TRX_CONFSTATS_BUFFER_TOTAL));
	trx_json_adduint64(json, "free", *(trx_uint64_t *)DCconfig_get_stats(TRX_CONFSTATS_BUFFER_FREE));
	trx_json_addfloat(json, "pfree", *(double *)DCconfig_get_stats(TRX_CONFSTATS_BUFFER_PFREE));
	trx_json_adduint64(json, "used", *(trx_uint64_t *)DCconfig_get_stats(TRX_CONFSTATS_BUFFER_USED));
	trx_json_addfloat(json, "pused", *(double *)DCconfig_get_stats(TRX_CONFSTATS_BUFFER_PUSED));
	trx_json_close(json);

	/* treegix[wcache,<cache>,<mode>] */
	DCget_stats_all(&wcache_info);
	trx_json_addobject(json, "wcache");

	trx_json_addobject(json, "values");
	trx_json_adduint64(json, "all", wcache_info.stats.history_counter);
	trx_json_adduint64(json, "float", wcache_info.stats.history_float_counter);
	trx_json_adduint64(json, "uint", wcache_info.stats.history_uint_counter);
	trx_json_adduint64(json, "str", wcache_info.stats.history_str_counter);
	trx_json_adduint64(json, "log", wcache_info.stats.history_log_counter);
	trx_json_adduint64(json, "text", wcache_info.stats.history_text_counter);
	trx_json_adduint64(json, "not supported", wcache_info.stats.notsupported_counter);
	trx_json_close(json);

	trx_json_addobject(json, "history");
	trx_json_addfloat(json, "pfree", 100 * (double)wcache_info.history_free / wcache_info.history_total);
	trx_json_adduint64(json, "free", wcache_info.history_free);
	trx_json_adduint64(json, "total", wcache_info.history_total);
	trx_json_adduint64(json, "used", wcache_info.history_total - wcache_info.history_free);
	trx_json_addfloat(json, "pused", 100 * (double)(wcache_info.history_total - wcache_info.history_free) /
			wcache_info.history_total);
	trx_json_close(json);

	trx_json_addobject(json, "index");
	trx_json_addfloat(json, "pfree", 100 * (double)wcache_info.index_free / wcache_info.index_total);
	trx_json_adduint64(json, "free", wcache_info.index_free);
	trx_json_adduint64(json, "total", wcache_info.index_total);
	trx_json_adduint64(json, "used", wcache_info.index_total - wcache_info.index_free);
	trx_json_addfloat(json, "pused", 100 * (double)(wcache_info.index_total - wcache_info.index_free) /
			wcache_info.index_total);
	trx_json_close(json);

	if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
	{
		trx_json_addobject(json, "trend");
		trx_json_addfloat(json, "pfree", 100 * (double)wcache_info.trend_free / wcache_info.trend_total);
		trx_json_adduint64(json, "free", wcache_info.trend_free);
		trx_json_adduint64(json, "total", wcache_info.trend_total);
		trx_json_adduint64(json, "used", wcache_info.trend_total - wcache_info.trend_free);
		trx_json_addfloat(json, "pused", 100 * (double)(wcache_info.trend_total - wcache_info.trend_free) /
				wcache_info.trend_total);
		trx_json_close(json);
	}

	trx_json_close(json);

	/* treegix[vmware,buffer,<mode>] */
	if (SUCCEED == trx_vmware_get_statistics(&vmware_stats))
	{
		trx_json_addobject(json, "vmware");
		trx_json_adduint64(json, "total", vmware_stats.memory_total);
		trx_json_adduint64(json, "free", vmware_stats.memory_total - vmware_stats.memory_used);
		trx_json_addfloat(json, "pfree", (double)(vmware_stats.memory_total - vmware_stats.memory_used) /
				vmware_stats.memory_total * 100);
		trx_json_adduint64(json, "used", vmware_stats.memory_used);
		trx_json_addfloat(json, "pused", (double)vmware_stats.memory_used / vmware_stats.memory_total * 100);
		trx_json_close(json);
	}

	/* treegix[process,<type>,<mode>,<state>] */
	trx_json_addobject(json, "process");

	if (SUCCEED == trx_get_all_process_stats(process_stats))
	{
		for (proc_type = 0; proc_type < TRX_PROCESS_TYPE_COUNT; proc_type++)
		{
			if (0 == process_stats[proc_type].count)
				continue;

			trx_json_addobject(json, get_process_type_string(proc_type));
			trx_json_addobject(json, "busy");
			trx_json_addfloat(json, "avg", process_stats[proc_type].busy_avg);
			trx_json_addfloat(json, "max", process_stats[proc_type].busy_max);
			trx_json_addfloat(json, "min", process_stats[proc_type].busy_min);
			trx_json_close(json);
			trx_json_addobject(json, "idle");
			trx_json_addfloat(json, "avg", process_stats[proc_type].idle_avg);
			trx_json_addfloat(json, "max", process_stats[proc_type].idle_max);
			trx_json_addfloat(json, "min", process_stats[proc_type].idle_min);
			trx_json_close(json);
			trx_json_adduint64(json, "count", process_stats[proc_type].count);
			trx_json_close(json);
		}
	}

	trx_json_close(json);
}
