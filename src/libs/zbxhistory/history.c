

#include "common.h"
#include "log.h"
#include "trxalgo.h"
#include "trxhistory.h"
#include "history.h"

#include "../trxalgo/vectorimpl.h"

TRX_VECTOR_IMPL(history_record, trx_history_record_t)

extern char	*CONFIG_HISTORY_STORAGE_URL;
extern char	*CONFIG_HISTORY_STORAGE_OPTS;

trx_history_iface_t	history_ifaces[ITEM_VALUE_TYPE_MAX];

/************************************************************************************
 *                                                                                  *
 * Function: trx_history_init                                                       *
 *                                                                                  *
 * Purpose: initializes history storage                                             *
 *                                                                                  *
 * Comments: History interfaces are created for all values types based on           *
 *           configuration. Every value type can have different history storage     *
 *           backend.                                                               *
 *                                                                                  *
 ************************************************************************************/
int	trx_history_init(char **error)
{
	int		i, ret;

	/* TODO: support per value type specific configuration */

	const char	*opts[] = {"dbl", "str", "log", "uint", "text"};

	for (i = 0; i < ITEM_VALUE_TYPE_MAX; i++)
	{
		if (NULL == CONFIG_HISTORY_STORAGE_URL || NULL == strstr(CONFIG_HISTORY_STORAGE_OPTS, opts[i]))
			ret = trx_history_sql_init(&history_ifaces[i], i, error);
		else
			ret = trx_history_elastic_init(&history_ifaces[i], i, error);

		if (FAIL == ret)
			return FAIL;
	}

	return SUCCEED;
}

/************************************************************************************
 *                                                                                  *
 * Function: trx_history_destroy                                                    *
 *                                                                                  *
 * Purpose: destroys history storage                                                *
 *                                                                                  *
 * Comments: All interfaces created by trx_history_init() function are destroyed    *
 *           here.                                                                  *
 *                                                                                  *
 ************************************************************************************/
void	trx_history_destroy(void)
{
	int	i;

	for (i = 0; i < ITEM_VALUE_TYPE_MAX; i++)
	{
		trx_history_iface_t	*writer = &history_ifaces[i];

		writer->destroy(writer);
	}
}

/************************************************************************************
 *                                                                                  *
 * Function: trx_history_add_values                                                 *
 *                                                                                  *
 * Purpose: Sends values to the history storage                                     *
 *                                                                                  *
 * Parameters: history - [IN] the values to store                                   *
 *                                                                                  *
 * Comments: add history values to the configured storage backends                  *
 *                                                                                  *
 ************************************************************************************/
int	trx_history_add_values(const trx_vector_ptr_t *history)
{
	int	i, flags = 0, ret = SUCCEED;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < ITEM_VALUE_TYPE_MAX; i++)
	{
		trx_history_iface_t	*writer = &history_ifaces[i];

		if (0 < writer->add_values(writer, history))
			flags |= (1 << i);
	}

	for (i = 0; i < ITEM_VALUE_TYPE_MAX; i++)
	{
		trx_history_iface_t	*writer = &history_ifaces[i];

		if (0 != (flags & (1 << i)))
			ret = writer->flush(writer);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/************************************************************************************
 *                                                                                  *
 * Function: trx_history_get_values                                                 *
 *                                                                                  *
 * Purpose: gets item values from history storage                                   *
 *                                                                                  *
 * Parameters:  itemid     - [IN] the itemid                                        *
 *              value_type - [IN] the item value type                               *
 *              start      - [IN] the period start timestamp                        *
 *              count      - [IN] the number of values to read                      *
 *              end        - [IN] the period end timestamp                          *
 *              values     - [OUT] the item history data values                     *
 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function reads <count> values from ]<start>,<end>] interval or    *
 *           all values from the specified interval if count is zero.               *
 *                                                                                  *
 ************************************************************************************/
int	trx_history_get_values(trx_uint64_t itemid, int value_type, int start, int count, int end,
		trx_vector_history_record_t *values)
{
	int			ret, pos;
	trx_history_iface_t	*writer = &history_ifaces[value_type];

	treegix_log(LOG_LEVEL_DEBUG, "In %s() itemid:" TRX_FS_UI64 " value_type:%d start:%d count:%d end:%d",
			__func__, itemid, value_type, start, count, end);

	pos = values->values_num;
	ret = writer->get_values(writer, itemid, start, count, end, values);

	if (SUCCEED == ret && SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
	{
		int	i;
		char	buffer[MAX_STRING_LEN];

		for (i = pos; i < values->values_num; i++)
		{
			trx_history_record_t	*h = &values->values[i];

			trx_history_value2str(buffer, sizeof(buffer), &h->value, value_type);
			treegix_log(LOG_LEVEL_TRACE, "  %d.%09d %s", h->timestamp.sec, h->timestamp.ns, buffer);
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s values:%d", __func__, trx_result_string(ret),
			values->values_num - pos);

	return ret;
}

/************************************************************************************
 *                                                                                  *
 * Function: trx_history_requires_trends                                            *
 *                                                                                  *
 * Purpose: checks if the value type requires trends data calculations              *
 *                                                                                  *
 * Parameters: value_type - [IN] the value type                                     *
 *                                                                                  *
 * Return value: SUCCEED - trends must be calculated for this value type            *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function is used to check if the trends must be calculated for    *
 *           the specified value type based on the history storage used.            *
 *                                                                                  *
 ************************************************************************************/
int	trx_history_requires_trends(int value_type)
{
	trx_history_iface_t	*writer = &history_ifaces[value_type];

	return 0 != writer->requires_trends ? SUCCEED : FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: history_logfree                                                  *
 *                                                                            *
 * Purpose: frees history log and all resources allocated for it              *
 *                                                                            *
 * Parameters: log   - [IN] the history log to free                           *
 *                                                                            *
 ******************************************************************************/
static void	history_logfree(trx_log_value_t *log)
{
	trx_free(log->source);
	trx_free(log->value);
	trx_free(log);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_history_record_vector_destroy                                *
 *                                                                            *
 * Purpose: destroys value vector and frees resources allocated for it        *
 *                                                                            *
 * Parameters: vector    - [IN] the value vector                              *
 *                                                                            *
 * Comments: Use this function to destroy value vectors created by            *
 *           trx_vc_get_values_by_* functions.                                *
 *                                                                            *
 ******************************************************************************/
void	trx_history_record_vector_destroy(trx_vector_history_record_t *vector, int value_type)
{
	if (NULL != vector->values)
	{
		trx_history_record_vector_clean(vector, value_type);
		trx_vector_history_record_destroy(vector);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_history_record_clear                                         *
 *                                                                            *
 * Purpose: frees resources allocated by a cached value                       *
 *                                                                            *
 * Parameters: value      - [IN] the cached value to clear                    *
 *             value_type - [IN] the history value type                       *
 *                                                                            *
 ******************************************************************************/
void	trx_history_record_clear(trx_history_record_t *value, int value_type)
{
	switch (value_type)
	{
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			trx_free(value->value.str);
			break;
		case ITEM_VALUE_TYPE_LOG:
			history_logfree(value->value.log);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_history_value2str                                            *
 *                                                                            *
 * Purpose: converts history value to string format                           *
 *                                                                            *
 * Parameters: buffer     - [OUT] the output buffer                           *
 *             size       - [IN] the output buffer size                       *
 *             value      - [IN] the value to convert                         *
 *             value_type - [IN] the history value type                       *
 *                                                                            *
 ******************************************************************************/
void	trx_history_value2str(char *buffer, size_t size, const history_value_t *value, int value_type)
{
	switch (value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			trx_snprintf(buffer, size, TRX_FS_DBL, value->dbl);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			trx_snprintf(buffer, size, TRX_FS_UI64, value->ui64);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			trx_strlcpy_utf8(buffer, value->str, size);
			break;
		case ITEM_VALUE_TYPE_LOG:
			trx_strlcpy_utf8(buffer, value->log->value, size);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_history_record_vector_clean                                  *
 *                                                                            *
 * Purpose: releases resources allocated to store history records             *
 *                                                                            *
 * Parameters: vector      - [IN] the history record vector                   *
 *             value_type  - [IN] the type of vector values                   *
 *                                                                            *
 ******************************************************************************/
void	trx_history_record_vector_clean(trx_vector_history_record_t *vector, int value_type)
{
	int	i;

	switch (value_type)
	{
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			for (i = 0; i < vector->values_num; i++)
				trx_free(vector->values[i].value.str);

			break;
		case ITEM_VALUE_TYPE_LOG:
			for (i = 0; i < vector->values_num; i++)
				history_logfree(vector->values[i].value.log);
	}

	trx_vector_history_record_clear(vector);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_history_record_compare_asc_func                              *
 *                                                                            *
 * Purpose: compares two cache values by their timestamps                     *
 *                                                                            *
 * Parameters: d1   - [IN] the first value                                    *
 *             d2   - [IN] the second value                                   *
 *                                                                            *
 * Return value:   <0 - the first value timestamp is less than second         *
 *                 =0 - the first value timestamp is equal to the second      *
 *                 >0 - the first value timestamp is greater than second      *
 *                                                                            *
 * Comments: This function is commonly used to sort value vector in ascending *
 *           order.                                                           *
 *                                                                            *
 ******************************************************************************/
int	trx_history_record_compare_asc_func(const trx_history_record_t *d1, const trx_history_record_t *d2)
{
	if (d1->timestamp.sec == d2->timestamp.sec)
		return d1->timestamp.ns - d2->timestamp.ns;

	return d1->timestamp.sec - d2->timestamp.sec;
}

/******************************************************************************
 *                                                                            *
 * Function: vc_history_record_compare_desc_func                              *
 *                                                                            *
 * Purpose: compares two cache values by their timestamps                     *
 *                                                                            *
 * Parameters: d1   - [IN] the first value                                    *
 *             d2   - [IN] the second value                                   *
 *                                                                            *
 * Return value:   >0 - the first value timestamp is less than second         *
 *                 =0 - the first value timestamp is equal to the second      *
 *                 <0 - the first value timestamp is greater than second      *
 *                                                                            *
 * Comments: This function is commonly used to sort value vector in descending*
 *           order.                                                           *
 *                                                                            *
 ******************************************************************************/
int	trx_history_record_compare_desc_func(const trx_history_record_t *d1, const trx_history_record_t *d2)
{
	if (d1->timestamp.sec == d2->timestamp.sec)
		return d2->timestamp.ns - d1->timestamp.ns;

	return d2->timestamp.sec - d1->timestamp.sec;
}

