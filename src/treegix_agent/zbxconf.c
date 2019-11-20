

#include "common.h"
#include "trxconf.h"

#include "cfg.h"
#include "log.h"
#include "alias.h"
#include "sysinfo.h"
#ifdef _WINDOWS
#	include "perfstat.h"
#endif
#include "comms.h"

/******************************************************************************
 *                                                                            *
 * Function: load_aliases                                                     *
 *                                                                            *
 * Purpose: load aliases from configuration                                   *
 *                                                                            *
 * Parameters: lines - aliase entries from configuration file                 *
 *                                                                            *
 * Comments: calls add_alias() for each entry                                 *
 *                                                                            *
 ******************************************************************************/
void	load_aliases(char **lines)
{
	char	**pline;

	for (pline = lines; NULL != *pline; pline++)
	{
		char		*c;
		const char	*r = *pline;

		if (SUCCEED != parse_key(&r) || ':' != *r)
		{
			treegix_log(LOG_LEVEL_CRIT, "cannot add alias \"%s\": invalid character at position %d",
					*pline, (int)((r - *pline) + 1));
			exit(EXIT_FAILURE);
		}

		c = (char *)r++;

		if (SUCCEED != parse_key(&r) || '\0' != *r)
		{
			treegix_log(LOG_LEVEL_CRIT, "cannot add alias \"%s\": invalid character at position %d",
					*pline, (int)((r - *pline) + 1));
			exit(EXIT_FAILURE);
		}

		*c++ = '\0';

		add_alias(*pline, c);

		*--c = ':';
	}
}

/******************************************************************************
 *                                                                            *
 * Function: load_user_parameters                                             *
 *                                                                            *
 * Purpose: load user parameters from configuration                           *
 *                                                                            *
 * Parameters: lines - user parameter entries from configuration file         *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 * Comments: calls add_user_parameter() for each entry                        *
 *                                                                            *
 ******************************************************************************/
void	load_user_parameters(char **lines)
{
	char	*p, **pline, error[MAX_STRING_LEN];

	for (pline = lines; NULL != *pline; pline++)
	{
		if (NULL == (p = strchr(*pline, ',')))
		{
			treegix_log(LOG_LEVEL_CRIT, "cannot add user parameter \"%s\": not comma-separated", *pline);
			exit(EXIT_FAILURE);
		}
		*p = '\0';

		if (FAIL == add_user_parameter(*pline, p + 1, error, sizeof(error)))
		{
			*p = ',';
			treegix_log(LOG_LEVEL_CRIT, "cannot add user parameter \"%s\": %s", *pline, error);
			exit(EXIT_FAILURE);
		}
		*p = ',';
	}
}

#ifdef _WINDOWS
/******************************************************************************
 *                                                                            *
 * Function: load_perf_counters                                               *
 *                                                                            *
 * Purpose: load performance counters from configuration                      *
 *                                                                            *
 * Parameters: def_lines - array of PerfCounter configuration entries         *
 *             eng_lines - array of PerfCounterEn configuration entries       *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
void	load_perf_counters(const char **def_lines, const char **eng_lines)
{
	char		name[MAX_STRING_LEN], counterpath[PDH_MAX_COUNTER_PATH], interval[8];
	const char	**pline, **lines;
	char		*error = NULL;
	LPTSTR		wcounterPath;
	int		period;

	for (lines = def_lines;; lines = eng_lines)
	{
		trx_perf_counter_lang_t lang = (lines == def_lines) ? PERF_COUNTER_LANG_DEFAULT : PERF_COUNTER_LANG_EN;

		for (pline = lines; NULL != *pline; pline++)
		{
			if (3 < num_param(*pline))
			{
				error = trx_strdup(error, "Required parameter missing.");
				goto pc_fail;
			}

			if (0 != get_param(*pline, 1, name, sizeof(name)))
			{
				error = trx_strdup(error, "Cannot parse key.");
				goto pc_fail;
			}

			if (0 != get_param(*pline, 2, counterpath, sizeof(counterpath)))
			{
				error = trx_strdup(error, "Cannot parse counter path.");
				goto pc_fail;
			}

			if (0 != get_param(*pline, 3, interval, sizeof(interval)))
			{
				error = trx_strdup(error, "Cannot parse interval.");
				goto pc_fail;
			}

			wcounterPath = trx_acp_to_unicode(counterpath);
			trx_unicode_to_utf8_static(wcounterPath, counterpath, PDH_MAX_COUNTER_PATH);
			trx_free(wcounterPath);

			if (FAIL == check_counter_path(counterpath, lang == PERF_COUNTER_LANG_DEFAULT))
			{
				error = trx_strdup(error, "Invalid counter path.");
				goto pc_fail;
			}

			period = atoi(interval);

			if (1 > period || MAX_COLLECTOR_PERIOD < period)
			{
				error = trx_strdup(NULL, "Interval out of range.");
				goto pc_fail;
			}

			if (NULL == add_perf_counter(name, counterpath, period, lang, &error))
			{
				if (NULL == error)
					error = trx_strdup(error, "Failed to add new performance counter.");
				goto pc_fail;
			}

			continue;
	pc_fail:
			treegix_log(LOG_LEVEL_CRIT, "cannot add performance counter \"%s\": %s", *pline, error);
			trx_free(error);

			exit(EXIT_FAILURE);
		}

		if (lines == eng_lines)
			break;
	}
}
#endif	/* _WINDOWS */

#ifdef _AIX
void	tl_version(void)
{
#ifdef _AIXVERSION_610
#	define TRX_AIX_TL	"6100 and above"
#elif _AIXVERSION_530
#	ifdef HAVE_AIXOSLEVEL_530006
#		define TRX_AIX_TL	"5300-06 and above"
#	else
#		define TRX_AIX_TL	"5300-00,01,02,03,04,05"
#	endif
#elif _AIXVERSION_520
#	define TRX_AIX_TL	"5200"
#elif _AIXVERSION_510
#	define TRX_AIX_TL	"5100"
#endif
#ifdef TRX_AIX_TL
	printf("Supported technology levels: %s\n", TRX_AIX_TL);
#endif /* TRX_AIX_TL */
#undef TRX_AIX_TL
}
#endif /* _AIX */
