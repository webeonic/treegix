

#include "control.h"

static int	parse_log_level_options(const char *opt, size_t len, unsigned int *scope, unsigned int *data)
{
	unsigned short	num = 0;
	const char	*rtc_options;

	rtc_options = opt + len;

	if ('\0' == *rtc_options)
	{
		*scope = TRX_RTC_LOG_SCOPE_FLAG | TRX_RTC_LOG_SCOPE_PID;
		*data = 0;
	}
	else if ('=' != *rtc_options)
	{
		trx_error("invalid runtime control option: %s", opt);
		return FAIL;
	}
	else if (0 != isdigit(*(++rtc_options)))
	{
		/* convert PID */
		if (FAIL == is_ushort(rtc_options, &num) || 0 == num)
		{
			trx_error("invalid log level control target: invalid or unsupported process identifier");
			return FAIL;
		}

		*scope = TRX_RTC_LOG_SCOPE_FLAG | TRX_RTC_LOG_SCOPE_PID;
		*data = num;
	}
	else
	{
		char	*proc_name = NULL, *proc_num;
		int	proc_type;

		if ('\0' == *rtc_options)
		{
			trx_error("invalid log level control target: unspecified process identifier or type");
			return FAIL;
		}

		proc_name = trx_strdup(proc_name, rtc_options);

		if (NULL != (proc_num = strchr(proc_name, ',')))
			*proc_num++ = '\0';

		if ('\0' == *proc_name)
		{
			trx_error("invalid log level control target: unspecified process type");
			trx_free(proc_name);
			return FAIL;
		}

		if (TRX_PROCESS_TYPE_UNKNOWN == (proc_type = get_process_type_by_name(proc_name)))
		{
			trx_error("invalid log level control target: unknown process type \"%s\"", proc_name);
			trx_free(proc_name);
			return FAIL;
		}

		if (NULL != proc_num)
		{
			if ('\0' == *proc_num)
			{
				trx_error("invalid log level control target: unspecified process number");
				trx_free(proc_name);
				return FAIL;
			}

			/* convert Treegix process number (e.g. "2" in "poller,2") */
			if (FAIL == is_ushort(proc_num, &num) || 0 == num)
			{
				trx_error("invalid log level control target: invalid or unsupported process number"
						" \"%s\"", proc_num);
				trx_free(proc_name);
				return FAIL;
			}
		}

		trx_free(proc_name);

		*scope = TRX_RTC_LOG_SCOPE_PROC | (unsigned int)proc_type;
		*data = num;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: parse_rtc_options                                                *
 *                                                                            *
 * Purpose: parse runtime control options and create a runtime control        *
 *          message                                                           *
 *                                                                            *
 * Parameters: opt          - [IN] the command line argument                  *
 *             program_type - [IN] the program type                           *
 *             message      - [OUT] the message containing options for log    *
 *                                  level change or cache reload              *
 *                                                                            *
 * Return value: SUCCEED - the message was created successfully               *
 *               FAIL    - an error occurred                                  *
 *                                                                            *
 ******************************************************************************/
int	parse_rtc_options(const char *opt, unsigned char program_type, int *message)
{
	unsigned int	scope, data, command;

	if (0 == strncmp(opt, TRX_LOG_LEVEL_INCREASE, TRX_CONST_STRLEN(TRX_LOG_LEVEL_INCREASE)))
	{
		command = TRX_RTC_LOG_LEVEL_INCREASE;

		if (SUCCEED != parse_log_level_options(opt, TRX_CONST_STRLEN(TRX_LOG_LEVEL_INCREASE), &scope, &data))
			return FAIL;
	}
	else if (0 == strncmp(opt, TRX_LOG_LEVEL_DECREASE, TRX_CONST_STRLEN(TRX_LOG_LEVEL_DECREASE)))
	{
		command = TRX_RTC_LOG_LEVEL_DECREASE;

		if (SUCCEED != parse_log_level_options(opt, TRX_CONST_STRLEN(TRX_LOG_LEVEL_DECREASE), &scope, &data))
			return FAIL;
	}
	else if (0 != (program_type & (TRX_PROGRAM_TYPE_SERVER | TRX_PROGRAM_TYPE_PROXY)) &&
			0 == strcmp(opt, TRX_CONFIG_CACHE_RELOAD))
	{
		command = TRX_RTC_CONFIG_CACHE_RELOAD;
		scope = 0;
		data = 0;
	}
	else if (0 != (program_type & (TRX_PROGRAM_TYPE_SERVER | TRX_PROGRAM_TYPE_PROXY)) &&
			0 == strcmp(opt, TRX_HOUSEKEEPER_EXECUTE))
	{
		command = TRX_RTC_HOUSEKEEPER_EXECUTE;
		scope = 0;
		data = 0;
	}
	else
	{
		trx_error("invalid runtime control option: %s", opt);
		return FAIL;
	}

	*message = (int)TRX_RTC_MAKE_MESSAGE(command, scope, data);

	return SUCCEED;
}
