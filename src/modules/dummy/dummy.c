

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "module.h"

/* the variable keeps timeout setting for item processing */
static int	item_timeout = 0;

/* module SHOULD define internal functions as static and use a naming pattern different from Treegix internal */
/* symbols (trx_*) and loadable module API functions (trx_module_*) to avoid conflicts                       */
static int	dummy_ping(AGENT_REQUEST *request, AGENT_RESULT *result);
static int	dummy_echo(AGENT_REQUEST *request, AGENT_RESULT *result);
static int	dummy_random(AGENT_REQUEST *request, AGENT_RESULT *result);

static TRX_METRIC keys[] =
/*	KEY			FLAG		FUNCTION	TEST PARAMETERS */
{
	{"dummy.ping",		0,		dummy_ping,	NULL},
	{"dummy.echo",		CF_HAVEPARAMS,	dummy_echo,	"a message"},
	{"dummy.random",	CF_HAVEPARAMS,	dummy_random,	"1,1000"},
	{NULL}
};

/******************************************************************************
 *                                                                            *
 * Function: trx_module_api_version                                           *
 *                                                                            *
 * Purpose: returns version number of the module interface                    *
 *                                                                            *
 * Return value: TRX_MODULE_API_VERSION - version of module.h module is       *
 *               compiled with, in order to load module successfully Treegix   *
 *               MUST be compiled with the same version of this header file   *
 *                                                                            *
 ******************************************************************************/
int	trx_module_api_version(void)
{
	return TRX_MODULE_API_VERSION;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_module_item_timeout                                          *
 *                                                                            *
 * Purpose: set timeout value for processing of items                         *
 *                                                                            *
 * Parameters: timeout - timeout in seconds, 0 - no timeout set               *
 *                                                                            *
 ******************************************************************************/
void	trx_module_item_timeout(int timeout)
{
	item_timeout = timeout;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_module_item_list                                             *
 *                                                                            *
 * Purpose: returns list of item keys supported by the module                 *
 *                                                                            *
 * Return value: list of item keys                                            *
 *                                                                            *
 ******************************************************************************/
TRX_METRIC	*trx_module_item_list(void)
{
	return keys;
}

static int	dummy_ping(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	SET_UI64_RESULT(result, 1);

	return SYSINFO_RET_OK;
}

static int	dummy_echo(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*param;

	if (1 != request->nparam)
	{
		/* set optional error message */
		SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
		return SYSINFO_RET_FAIL;
	}

	param = get_rparam(request, 0);

	SET_STR_RESULT(result, strdup(param));

	return SYSINFO_RET_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: dummy_random                                                     *
 *                                                                            *
 * Purpose: a main entry point for processing of an item                      *
 *                                                                            *
 * Parameters: request - structure that contains item key and parameters      *
 *              request->key - item key without parameters                    *
 *              request->nparam - number of parameters                        *
 *              request->timeout - processing should not take longer than     *
 *                                 this number of seconds                     *
 *              request->params[N-1] - pointers to item key parameters        *
 *                                                                            *
 *             result - structure that will contain result                    *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by treegix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 * Comment: get_rparam(request, N-1) can be used to get a pointer to the Nth  *
 *          parameter starting from 0 (first parameter). Make sure it exists  *
 *          by checking value of request->nparam.                             *
 *                                                                            *
 ******************************************************************************/
static int	dummy_random(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*param1, *param2;
	int	from, to;

	if (2 != request->nparam)
	{
		/* set optional error message */
		SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
		return SYSINFO_RET_FAIL;
	}

	param1 = get_rparam(request, 0);
	param2 = get_rparam(request, 1);

	/* there is no strict validation of parameters for simplicity sake */
	from = atoi(param1);
	to = atoi(param2);

	if (from > to)
	{
		SET_MSG_RESULT(result, strdup("Invalid range specified."));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, from + rand() % (to - from + 1));

	return SYSINFO_RET_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_module_init                                                  *
 *                                                                            *
 * Purpose: the function is called on agent startup                           *
 *          It should be used to call any initialization routines             *
 *                                                                            *
 * Return value: TRX_MODULE_OK - success                                      *
 *               TRX_MODULE_FAIL - module initialization failed               *
 *                                                                            *
 * Comment: the module won't be loaded in case of TRX_MODULE_FAIL             *
 *                                                                            *
 ******************************************************************************/
int	trx_module_init(void)
{
	/* initialization for dummy.random */
	srand(time(NULL));

	return TRX_MODULE_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_module_uninit                                                *
 *                                                                            *
 * Purpose: the function is called on agent shutdown                          *
 *          It should be used to cleanup used resources if there are any      *
 *                                                                            *
 * Return value: TRX_MODULE_OK - success                                      *
 *               TRX_MODULE_FAIL - function failed                            *
 *                                                                            *
 ******************************************************************************/
int	trx_module_uninit(void)
{
	return TRX_MODULE_OK;
}

/******************************************************************************
 *                                                                            *
 * Functions: dummy_history_float_cb                                          *
 *            dummy_history_integer_cb                                        *
 *            dummy_history_string_cb                                         *
 *            dummy_history_text_cb                                           *
 *            dummy_history_log_cb                                            *
 *                                                                            *
 * Purpose: callback functions for storing historical data of types float,    *
 *          integer, string, text and log respectively in external storage    *
 *                                                                            *
 * Parameters: history     - array of historical data                         *
 *             history_num - number of elements in history array              *
 *                                                                            *
 ******************************************************************************/
static void	dummy_history_float_cb(const TRX_HISTORY_FLOAT *history, int history_num)
{
	int	i;

	for (i = 0; i < history_num; i++)
	{
		/* do something with history[i].itemid, history[i].clock, history[i].ns, history[i].value, ... */
	}
}

static void	dummy_history_integer_cb(const TRX_HISTORY_INTEGER *history, int history_num)
{
	int	i;

	for (i = 0; i < history_num; i++)
	{
		/* do something with history[i].itemid, history[i].clock, history[i].ns, history[i].value, ... */
	}
}

static void	dummy_history_string_cb(const TRX_HISTORY_STRING *history, int history_num)
{
	int	i;

	for (i = 0; i < history_num; i++)
	{
		/* do something with history[i].itemid, history[i].clock, history[i].ns, history[i].value, ... */
	}
}

static void	dummy_history_text_cb(const TRX_HISTORY_TEXT *history, int history_num)
{
	int	i;

	for (i = 0; i < history_num; i++)
	{
		/* do something with history[i].itemid, history[i].clock, history[i].ns, history[i].value, ... */
	}
}

static void	dummy_history_log_cb(const TRX_HISTORY_LOG *history, int history_num)
{
	int	i;

	for (i = 0; i < history_num; i++)
	{
		/* do something with history[i].itemid, history[i].clock, history[i].ns, history[i].value, ... */
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_module_history_write_cbs                                     *
 *                                                                            *
 * Purpose: returns a set of module functions Treegix will call to export      *
 *          different types of historical data                                *
 *                                                                            *
 * Return value: structure with callback function pointers (can be NULL if    *
 *               module is not interested in data of certain types)           *
 *                                                                            *
 ******************************************************************************/
TRX_HISTORY_WRITE_CBS	trx_module_history_write_cbs(void)
{
	static TRX_HISTORY_WRITE_CBS	dummy_callbacks =
	{
		dummy_history_float_cb,
		dummy_history_integer_cb,
		dummy_history_string_cb,
		dummy_history_text_cb,
		dummy_history_log_cb,
	};

	return dummy_callbacks;
}
