

#include "sysinfo.h"
#include "trxalgo.h"
#include "trxjson.h"
#include "cpustat.h"

static const char	*get_cpu_status_string(int status)
{
	switch (status)
	{
		case TRX_CPU_STATUS_ONLINE:
			return "online";
		case TRX_CPU_STATUS_OFFLINE:
			return "offline";
		case TRX_CPU_STATUS_UNKNOWN:
			return "unknown";
	}

	return NULL;
}

int	SYSTEM_CPU_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	trx_vector_uint64_pair_t	cpus;
	struct trx_json			json;
	int				i, ret = SYSINFO_RET_FAIL;

	TRX_UNUSED(request);

	trx_vector_uint64_pair_create(&cpus);

	if (SUCCEED != get_cpus(&cpus))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Collector is not started."));
		goto out;
	}

	trx_json_initarray(&json, TRX_JSON_STAT_BUF_LEN);

	for (i = 0; i < cpus.values_num; i++)
	{
		trx_json_addobject(&json, NULL);

		trx_json_adduint64(&json, "{#CPU.NUMBER}", cpus.values[i].first);
		trx_json_addstring(&json, "{#CPU.STATUS}", get_cpu_status_string((int)cpus.values[i].second),
				TRX_JSON_TYPE_STRING);

		trx_json_close(&json);
	}

	trx_json_close(&json);
	SET_STR_RESULT(result, trx_strdup(result->str, json.buffer));

	trx_json_free(&json);

	ret = SYSINFO_RET_OK;
out:
	trx_vector_uint64_pair_destroy(&cpus);

	return ret;
}
