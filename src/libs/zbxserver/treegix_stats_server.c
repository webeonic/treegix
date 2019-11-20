

#include "common.h"
#include "trxjson.h"
#include "dbcache.h"
#include "valuecache.h"
#include "preproc.h"
#include "trxlld.h"
#include "log.h"

#include "treegix_stats.h"

/******************************************************************************
 *                                                                            *
 * Function: trx_get_treegix_stats_ext                                         *
 *                                                                            *
 * Purpose: get program type (server) specific internal statistics            *
 *                                                                            *
 * Parameters: param1  - [IN/OUT] the json data                               *
 *                                                                            *
 * Comments: This function is used to gather server specific internal         *
 *           statistics.                                                      *
 *                                                                            *
 ******************************************************************************/
void	trx_get_treegix_stats_ext(struct trx_json *json)
{
	trx_vc_stats_t	vc_stats;
	trx_uint64_t	queue_size;
	char		*error = NULL;

	/* treegix[lld_queue] */
	if (SUCCEED == trx_lld_get_queue_size(&queue_size, &error))
	{
		trx_json_adduint64(json, "lld_queue", queue_size);
	}
	else
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot get LLD queue size: %s", error);
		trx_free(error);
	}

	/* treegix[triggers] */
	trx_json_adduint64(json, "triggers", DCget_trigger_count());

	/* treegix[vcache,...] */
	if (SUCCEED == trx_vc_get_statistics(&vc_stats))
	{
		trx_json_addobject(json, "vcache");

		trx_json_addobject(json, "buffer");
		trx_json_adduint64(json, "total", vc_stats.total_size);
		trx_json_adduint64(json, "free", vc_stats.free_size);
		trx_json_addfloat(json, "pfree", (double)vc_stats.free_size / vc_stats.total_size * 100);
		trx_json_adduint64(json, "used", vc_stats.total_size - vc_stats.free_size);
		trx_json_addfloat(json, "pused", (double)(vc_stats.total_size - vc_stats.free_size) /
				vc_stats.total_size * 100);
		trx_json_close(json);

		trx_json_addobject(json, "cache");
		trx_json_adduint64(json, "requests", vc_stats.hits + vc_stats.misses);
		trx_json_adduint64(json, "hits", vc_stats.hits);
		trx_json_adduint64(json, "misses", vc_stats.misses);
		trx_json_adduint64(json, "mode", vc_stats.mode);
		trx_json_close(json);

		trx_json_close(json);
	}
}
