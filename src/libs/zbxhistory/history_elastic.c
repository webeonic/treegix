

#include "common.h"
#include "log.h"
#include "trxjson.h"
#include "trxalgo.h"
#include "dbcache.h"
#include "trxhistory.h"
#include "trxself.h"
#include "history.h"

/* curl_multi_wait() is supported starting with version 7.28.0 (0x071c00) */
#if defined(HAVE_LIBCURL) && LIBCURL_VERSION_NUM >= 0x071c00

#define		TRX_HISTORY_STORAGE_DOWN	10000 /* Timeout in milliseconds */

#define		TRX_IDX_JSON_ALLOCATE		256
#define		TRX_JSON_ALLOCATE		2048


const char	*value_type_str[] = {"dbl", "str", "log", "uint", "text"};

extern char	*CONFIG_HISTORY_STORAGE_URL;
extern int	CONFIG_HISTORY_STORAGE_PIPELINES;

typedef struct
{
	char	*base_url;
	char	*post_url;
	char	*buf;
	CURL	*handle;
}
trx_elastic_data_t;

typedef struct
{
	unsigned char		initialized;
	trx_vector_ptr_t	ifaces;

	CURLM			*handle;
}
trx_elastic_writer_t;

static trx_elastic_writer_t	writer;

typedef struct
{
	char	*data;
	size_t	alloc;
	size_t	offset;
}
trx_httppage_t;

static trx_httppage_t	page_r;

typedef struct
{
	trx_httppage_t	page;
	char		errbuf[CURL_ERROR_SIZE];
}
trx_curlpage_t;

static trx_curlpage_t	page_w[ITEM_VALUE_TYPE_MAX];

static size_t	curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t	r_size = size * nmemb;

	trx_httppage_t	*page = (trx_httppage_t	*)userdata;

	trx_strncpy_alloc(&page->data, &page->alloc, &page->offset, ptr, r_size);

	return r_size;
}

static history_value_t	history_str2value(char *str, unsigned char value_type)
{
	history_value_t	value;

	switch (value_type)
	{
		case ITEM_VALUE_TYPE_LOG:
			value.log = (trx_log_value_t *)trx_malloc(NULL, sizeof(trx_log_value_t));
			memset(value.log, 0, sizeof(trx_log_value_t));
			value.log->value = trx_strdup(NULL, str);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			value.str = trx_strdup(NULL, str);
			break;
		case ITEM_VALUE_TYPE_FLOAT:
			value.dbl = atof(str);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			TRX_STR2UINT64(value.ui64, str);
			break;
	}

	return value;
}

static const char	*history_value2str(const TRX_DC_HISTORY *h)
{
	static char	buffer[MAX_ID_LEN + 1];

	switch (h->value_type)
	{
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			return h->value.str;
		case ITEM_VALUE_TYPE_LOG:
			return h->value.log->value;
		case ITEM_VALUE_TYPE_FLOAT:
			trx_snprintf(buffer, sizeof(buffer), TRX_FS_DBL, h->value.dbl);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			trx_snprintf(buffer, sizeof(buffer), TRX_FS_UI64, h->value.ui64);
			break;
	}

	return buffer;
}

static int	history_parse_value(struct trx_json_parse *jp, unsigned char value_type, trx_history_record_t *hr)
{
	char	*value = NULL;
	size_t	value_alloc = 0;
	int	ret = FAIL;

	if (SUCCEED != trx_json_value_by_name_dyn(jp, "clock", &value, &value_alloc))
		goto out;

	hr->timestamp.sec = atoi(value);

	if (SUCCEED != trx_json_value_by_name_dyn(jp, "ns", &value, &value_alloc))
		goto out;

	hr->timestamp.ns = atoi(value);

	if (SUCCEED != trx_json_value_by_name_dyn(jp, "value", &value, &value_alloc))
		goto out;

	hr->value = history_str2value(value, value_type);

	if (ITEM_VALUE_TYPE_LOG == value_type)
	{

		if (SUCCEED != trx_json_value_by_name_dyn(jp, "timestamp", &value, &value_alloc))
			goto out;

		hr->value.log->timestamp = atoi(value);

		if (SUCCEED != trx_json_value_by_name_dyn(jp, "logeventid", &value, &value_alloc))
			goto out;

		hr->value.log->logeventid = atoi(value);

		if (SUCCEED != trx_json_value_by_name_dyn(jp, "severity", &value, &value_alloc))
			goto out;

		hr->value.log->severity = atoi(value);

		if (SUCCEED != trx_json_value_by_name_dyn(jp, "source", &value, &value_alloc))
			goto out;

		hr->value.log->source = trx_strdup(NULL, value);
	}

	ret = SUCCEED;

out:
	trx_free(value);

	return ret;
}

static void	elastic_log_error(CURL *handle, CURLcode error, const char *errbuf)
{
	char		http_status[MAX_STRING_LEN];
	long int	http_code;
	CURLcode	curl_err;

	if (CURLE_HTTP_RETURNED_ERROR == error)
	{
		if (CURLE_OK == (curl_err = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code)))
			trx_snprintf(http_status, sizeof(http_status), "HTTP status code: %ld", http_code);
		else
			trx_strlcpy(http_status, "unknown HTTP status code", sizeof(http_status));

		if (0 != page_r.offset)
		{
			treegix_log(LOG_LEVEL_ERR, "cannot get values from elasticsearch, %s, message: %s", http_status,
					page_r.data);
		}
		else
			treegix_log(LOG_LEVEL_ERR, "cannot get values from elasticsearch, %s", http_status);
	}
	else
	{
		treegix_log(LOG_LEVEL_ERR, "cannot get values from elasticsearch: %s",
				'\0' != *errbuf ? errbuf : curl_easy_strerror(error));
	}
}

/************************************************************************************
 *                                                                                  *
 * Function: elastic_close                                                          *
 *                                                                                  *
 * Purpose: closes connection and releases allocated resources                      *
 *                                                                                  *
 * Parameters:  hist - [IN] the history storage interface                           *
 *                                                                                  *
 ************************************************************************************/
static void	elastic_close(trx_history_iface_t *hist)
{
	trx_elastic_data_t	*data = (trx_elastic_data_t *)hist->data;

	trx_free(data->buf);
	trx_free(data->post_url);

	if (NULL != data->handle)
	{
		if (NULL != writer.handle)
			curl_multi_remove_handle(writer.handle, data->handle);

		curl_easy_cleanup(data->handle);
		data->handle = NULL;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: elastic_is_error_present                                         *
 *                                                                            *
 * Purpose: check a error from Elastic json response                          *
 *                                                                            *
 * Parameters: page - [IN]  the buffer with json response                     *
 *             err  - [OUT] the parse error message. If the error value is    *
 *                           set it must be freed by caller after it has      *
 *                           been used.                                       *
 *                                                                            *
 * Return value: SUCCEED - the response contains an error                     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	elastic_is_error_present(trx_httppage_t *page, char **err)
{
	struct trx_json_parse	jp, jp_values, jp_index, jp_error, jp_items, jp_item;
	const char		*errors, *p = NULL;
	char			*index = NULL, *status = NULL, *type = NULL, *reason = NULL;
	size_t			index_alloc = 0, status_alloc = 0, type_alloc = 0, reason_alloc = 0;
	int			rc_js = SUCCEED;

	treegix_log(LOG_LEVEL_TRACE, "%s() raw json: %s", __func__, TRX_NULL2EMPTY_STR(page->data));

	if (SUCCEED != trx_json_open(page->data, &jp) || SUCCEED != trx_json_brackets_open(jp.start, &jp_values))
		return FAIL;

	if (NULL == (errors = trx_json_pair_by_name(&jp_values, "errors")) || 0 != strncmp("true", errors, 4))
		return FAIL;

	if (SUCCEED == trx_json_brackets_by_name(&jp, "items", &jp_items))
	{
		while (NULL != (p = trx_json_next(&jp_items, p)))
		{
			if (SUCCEED == trx_json_brackets_open(p, &jp_item) &&
					SUCCEED == trx_json_brackets_by_name(&jp_item, "index", &jp_index) &&
					SUCCEED == trx_json_brackets_by_name(&jp_index, "error", &jp_error))
			{
				if (SUCCEED != trx_json_value_by_name_dyn(&jp_error, "type", &type, &type_alloc))
					rc_js = FAIL;
				if (SUCCEED != trx_json_value_by_name_dyn(&jp_error, "reason", &reason, &reason_alloc))
					rc_js = FAIL;
			}
			else
				continue;

			if (SUCCEED != trx_json_value_by_name_dyn(&jp_index, "status", &status, &status_alloc))
				rc_js = FAIL;
			if (SUCCEED != trx_json_value_by_name_dyn(&jp_index, "_index", &index, &index_alloc))
				rc_js = FAIL;

			break;
		}
	}
	else
		rc_js = FAIL;

	*err = trx_dsprintf(NULL,"index:%s status:%s type:%s reason:%s%s", TRX_NULL2EMPTY_STR(index),
			TRX_NULL2EMPTY_STR(status), TRX_NULL2EMPTY_STR(type), TRX_NULL2EMPTY_STR(reason),
			FAIL == rc_js ? " / elasticsearch version is not fully compatible with treegix server" : "");

	trx_free(status);
	trx_free(type);
	trx_free(reason);
	trx_free(index);

	return SUCCEED;
}

/******************************************************************************************************************
 *                                                                                                                *
 * common sql service support                                                                                     *
 *                                                                                                                *
 ******************************************************************************************************************/



/************************************************************************************
 *                                                                                  *
 * Function: elastic_writer_init                                                    *
 *                                                                                  *
 * Purpose: initializes elastic writer for a new batch of history values            *
 *                                                                                  *
 ************************************************************************************/
static void	elastic_writer_init(void)
{
	if (0 != writer.initialized)
		return;

	trx_vector_ptr_create(&writer.ifaces);

	if (NULL == (writer.handle = curl_multi_init()))
	{
		trx_error("Cannot initialize cURL multi session");
		exit(EXIT_FAILURE);
	}

	writer.initialized = 1;
}

/************************************************************************************
 *                                                                                  *
 * Function: elastic_writer_release                                                 *
 *                                                                                  *
 * Purpose: releases initialized elastic writer by freeing allocated resources and  *
 *          setting its state to uninitialized.                                     *
 *                                                                                  *
 ************************************************************************************/
static void	elastic_writer_release(void)
{
	int	i;

	for (i = 0; i < writer.ifaces.values_num; i++)
		elastic_close((trx_history_iface_t *)writer.ifaces.values[i]);

	curl_multi_cleanup(writer.handle);
	writer.handle = NULL;

	trx_vector_ptr_destroy(&writer.ifaces);

	writer.initialized = 0;
}

/************************************************************************************
 *                                                                                  *
 * Function: elastic_writer_add_iface                                               *
 *                                                                                  *
 * Purpose: adds history storage interface to be flushed later                      *
 *                                                                                  *
 * Parameters: db_insert - [IN] bulk insert data                                    *
 *                                                                                  *
 ************************************************************************************/
static void	elastic_writer_add_iface(trx_history_iface_t *hist)
{
	trx_elastic_data_t	*data = (trx_elastic_data_t *)hist->data;

	elastic_writer_init();

	if (NULL == (data->handle = curl_easy_init()))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");
		return;
	}
	curl_easy_setopt(data->handle, CURLOPT_URL, data->post_url);
	curl_easy_setopt(data->handle, CURLOPT_POST, 1L);
	curl_easy_setopt(data->handle, CURLOPT_POSTFIELDS, data->buf);
	curl_easy_setopt(data->handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(data->handle, CURLOPT_WRITEDATA, &page_w[hist->value_type].page);
	curl_easy_setopt(data->handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(data->handle, CURLOPT_ERRORBUFFER, page_w[hist->value_type].errbuf);
	*page_w[hist->value_type].errbuf = '\0';
	curl_easy_setopt(data->handle, CURLOPT_PRIVATE, &page_w[hist->value_type]);
	page_w[hist->value_type].page.offset = 0;
	if (0 < page_w[hist->value_type].page.alloc)
		*page_w[hist->value_type].page.data = '\0';

	curl_multi_add_handle(writer.handle, data->handle);

	trx_vector_ptr_append(&writer.ifaces, hist);
}

/************************************************************************************
 *                                                                                  *
 * Function: elastic_writer_flush                                                   *
 *                                                                                  *
 * Purpose: posts historical data to elastic storage                                *
 *                                                                                  *
 ************************************************************************************/
static int	elastic_writer_flush(void)
{
	struct curl_slist	*curl_headers = NULL;
	int			i, running, previous, msgnum;
	CURLMsg			*msg;
	trx_vector_ptr_t	retries;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* The writer might be uninitialized only if the history */
	/* was already flushed. In that case, return SUCCEED */
	if (0 == writer.initialized)
		return SUCCEED;

	trx_vector_ptr_create(&retries);

	curl_headers = curl_slist_append(curl_headers, "Content-Type: application/x-ndjson");

	for (i = 0; i < writer.ifaces.values_num; i++)
	{
		trx_history_iface_t	*hist = (trx_history_iface_t *)writer.ifaces.values[i];
		trx_elastic_data_t	*data = (trx_elastic_data_t *)hist->data;

		(void)curl_easy_setopt(data->handle, CURLOPT_HTTPHEADER, curl_headers);

		treegix_log(LOG_LEVEL_DEBUG, "sending %s", data->buf);
	}

try_again:
	previous = 0;

	do
	{
		int		fds;
		CURLMcode	code;
		char 		*error;
		trx_curlpage_t	*curl_page;

		if (CURLM_OK != (code = curl_multi_perform(writer.handle, &running)))
		{
			treegix_log(LOG_LEVEL_ERR, "cannot perform on curl multi handle: %s", curl_multi_strerror(code));
			break;
		}

		if (CURLM_OK != (code = curl_multi_wait(writer.handle, NULL, 0, TRX_HISTORY_STORAGE_DOWN, &fds)))
		{
			treegix_log(LOG_LEVEL_ERR, "cannot wait on curl multi handle: %s", curl_multi_strerror(code));
			break;
		}

		if (previous == running)
			continue;

		while (NULL != (msg = curl_multi_info_read(writer.handle, &msgnum)))
		{
			/* If the error is due to malformed data, there is no sense on re-trying to send. */
			/* That's why we actually check for transport and curl errors separately */
			if (CURLE_HTTP_RETURNED_ERROR == msg->data.result)
			{
				if (CURLE_OK == curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE,
						(char **)&curl_page) && '\0' != *curl_page->errbuf)
				{
					treegix_log(LOG_LEVEL_ERR, "cannot send data to elasticsearch, HTTP error"
							" message: %s", curl_page->errbuf);
				}
				else
				{
					char		http_status[MAX_STRING_LEN];
					long int	err;
					CURLcode	curl_err;

					if (CURLE_OK == (curl_err = curl_easy_getinfo(msg->easy_handle,
							CURLINFO_RESPONSE_CODE, &err)))
					{
						trx_snprintf(http_status, sizeof(http_status), "HTTP status code: %ld",
								err);
					}
					else
					{
						trx_strlcpy(http_status, "unknown HTTP status code",
								sizeof(http_status));
					}

					treegix_log(LOG_LEVEL_ERR, "cannot send data to elasticsearch, %s", http_status);
				}
			}
			else if (CURLE_OK != msg->data.result)
			{
				if (CURLE_OK == curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE,
						(char **)&curl_page) && '\0' != *curl_page->errbuf)
				{
					treegix_log(LOG_LEVEL_WARNING, "cannot send data to elasticsearch: %s",
							curl_page->errbuf);
				}
				else
				{
					treegix_log(LOG_LEVEL_WARNING, "cannot send data to elasticsearch: %s",
							curl_easy_strerror(msg->data.result));
				}

				/* If the error is due to curl internal problems or unrelated */
				/* problems with HTTP, we put the handle in a retry list and */
				/* remove it from the current execution loop */
				trx_vector_ptr_append(&retries, msg->easy_handle);
				curl_multi_remove_handle(writer.handle, msg->easy_handle);
			}
			else if (CURLE_OK == curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (char **)&curl_page)
					&& SUCCEED == elastic_is_error_present(&curl_page->page, &error))
			{
				treegix_log(LOG_LEVEL_WARNING, "%s() cannot send data to elasticsearch: %s",
						__func__, error);
				trx_free(error);

				/* If the error is due to elastic internal problems (for example an index */
				/* became read-only), we put the handle in a retry list and */
				/* remove it from the current execution loop */
				trx_vector_ptr_append(&retries, msg->easy_handle);
				curl_multi_remove_handle(writer.handle, msg->easy_handle);
			}
		}

		previous = running;
	}
	while (running);

	/* We check if we have handles to retry. If yes, we put them back in the multi */
	/* handle and go to the beginning of the do while() for try sending the data again */
	/* after sleeping for TRX_HISTORY_STORAGE_DOWN / 1000 (seconds) */
	if (0 < retries.values_num)
	{
		for (i = 0; i < retries.values_num; i++)
			curl_multi_add_handle(writer.handle, retries.values[i]);

		trx_vector_ptr_clear(&retries);

		sleep(TRX_HISTORY_STORAGE_DOWN / 1000);
		goto try_again;
	}

	curl_slist_free_all(curl_headers);

	trx_vector_ptr_destroy(&retries);

	elastic_writer_release();

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return SUCCEED;
}

/******************************************************************************************************************
 *                                                                                                                *
 * history interface support                                                                                      *
 *                                                                                                                *
 ******************************************************************************************************************/

/************************************************************************************
 *                                                                                  *
 * Function: elastic_destroy                                                        *
 *                                                                                  *
 * Purpose: destroys history storage interface                                      *
 *                                                                                  *
 * Parameters:  hist - [IN] the history storage interface                           *
 *                                                                                  *
 ************************************************************************************/
static void	elastic_destroy(trx_history_iface_t *hist)
{
	trx_elastic_data_t	*data = (trx_elastic_data_t *)hist->data;

	elastic_close(hist);

	trx_free(data->base_url);
	trx_free(data);
}

/************************************************************************************
 *                                                                                  *
 * Function: elastic_get_values                                                     *
 *                                                                                  *
 * Purpose: gets item history data from history storage                             *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *              itemid  - [IN] the itemid                                           *
 *              start   - [IN] the period start timestamp                           *
 *              count   - [IN] the number of values to read                         *
 *              end     - [IN] the period end timestamp                             *
 *              values  - [OUT] the item history data values                        *
 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function reads <count> values from ]<start>,<end>] interval or    *
 *           all values from the specified interval if count is zero.               *
 *                                                                                  *
 ************************************************************************************/
static int	elastic_get_values(trx_history_iface_t *hist, trx_uint64_t itemid, int start, int count, int end,
		trx_vector_history_record_t *values)
{
	trx_elastic_data_t	*data = (trx_elastic_data_t *)hist->data;
	size_t			url_alloc = 0, url_offset = 0, id_alloc = 0, scroll_alloc = 0, scroll_offset = 0;
	int			total, empty, ret;
	CURLcode		err;
	struct trx_json		query;
	struct curl_slist	*curl_headers = NULL;
	char			*scroll_id = NULL, *scroll_query = NULL, errbuf[CURL_ERROR_SIZE];

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = FAIL;

	if (NULL == (data->handle = curl_easy_init()))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");

		return FAIL;
	}

	trx_snprintf_alloc(&data->post_url, &url_alloc, &url_offset, "%s/%s*/values/_search?scroll=10s", data->base_url,
			value_type_str[hist->value_type]);

	/* prepare the json query for elasticsearch, apply ranges if needed */
	trx_json_init(&query, TRX_JSON_ALLOCATE);

	if (0 < count)
	{
		trx_json_adduint64(&query, "size", count);
		trx_json_addarray(&query, "sort");
		trx_json_addobject(&query, NULL);
		trx_json_addobject(&query, "clock");
		trx_json_addstring(&query, "order", "desc", TRX_JSON_TYPE_STRING);
		trx_json_close(&query);
		trx_json_close(&query);
		trx_json_close(&query);
	}

	trx_json_addobject(&query, "query");
	trx_json_addobject(&query, "bool");
	trx_json_addarray(&query, "must");
	trx_json_addobject(&query, NULL);
	trx_json_addobject(&query, "match");
	trx_json_adduint64(&query, "itemid", itemid);
	trx_json_close(&query);
	trx_json_close(&query);
	trx_json_close(&query);
	trx_json_addarray(&query, "filter");
	trx_json_addobject(&query, NULL);
	trx_json_addobject(&query, "range");
	trx_json_addobject(&query, "clock");

	if (0 < start)
		trx_json_adduint64(&query, "gt", start);

	if (0 < end)
		trx_json_adduint64(&query, "lte", end);

	trx_json_close(&query);
	trx_json_close(&query);
	trx_json_close(&query);
	trx_json_close(&query);
	trx_json_close(&query);
	trx_json_close(&query);
	trx_json_close(&query);

	curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

	curl_easy_setopt(data->handle, CURLOPT_URL, data->post_url);
	curl_easy_setopt(data->handle, CURLOPT_POSTFIELDS, query.buffer);
	curl_easy_setopt(data->handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(data->handle, CURLOPT_WRITEDATA, &page_r);
	curl_easy_setopt(data->handle, CURLOPT_HTTPHEADER, curl_headers);
	curl_easy_setopt(data->handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(data->handle, CURLOPT_ERRORBUFFER, errbuf);

	treegix_log(LOG_LEVEL_DEBUG, "sending query to %s; post data: %s", data->post_url, query.buffer);

	page_r.offset = 0;
	*errbuf = '\0';
	if (CURLE_OK != (err = curl_easy_perform(data->handle)))
	{
		elastic_log_error(data->handle, err, errbuf);
		goto out;
	}

	url_offset = 0;
	trx_snprintf_alloc(&data->post_url, &url_alloc, &url_offset, "%s/_search/scroll", data->base_url);

	curl_easy_setopt(data->handle, CURLOPT_URL, data->post_url);

	total = (0 == count ? -1 : count);

	/* For processing the records, we need to keep track of the total requested and if the response from the */
	/* elasticsearch server is empty. For this we use two variables, empty and total. If the result is empty or */
	/* the total reach zero, we terminate the scrolling query and return what we currently have. */
	do
	{
		struct trx_json_parse	jp, jp_values, jp_item, jp_sub, jp_hits, jp_source;
		trx_history_record_t	hr;
		const char		*p = NULL;

		empty = 1;

		treegix_log(LOG_LEVEL_DEBUG, "received from elasticsearch: %s", page_r.data);

		trx_json_open(page_r.data, &jp);
		trx_json_brackets_open(jp.start, &jp_values);

		/* get the scroll id immediately, for being used in subsequent queries */
		if (SUCCEED != trx_json_value_by_name_dyn(&jp_values, "_scroll_id", &scroll_id, &id_alloc))
		{
			treegix_log(LOG_LEVEL_WARNING, "elasticsearch version is not compatible with treegix server. "
					"_scroll_id tag is absent");
		}

		trx_json_brackets_by_name(&jp_values, "hits", &jp_sub);
		trx_json_brackets_by_name(&jp_sub, "hits", &jp_hits);

		while (NULL != (p = trx_json_next(&jp_hits, p)))
		{
			empty = 0;

			if (SUCCEED != trx_json_brackets_open(p, &jp_item))
				continue;

			if (SUCCEED != trx_json_brackets_by_name(&jp_item, "_source", &jp_source))
				continue;

			if (SUCCEED != history_parse_value(&jp_source, hist->value_type, &hr))
				continue;

			trx_vector_history_record_append_ptr(values, &hr);

			if (-1 != total)
				--total;

			if (0 == total)
			{
				empty = 1;
				break;
			}
		}

		if (1 == empty)
		{
			ret = SUCCEED;
			break;
		}

		/* scroll to the next page */
		scroll_offset = 0;
		trx_snprintf_alloc(&scroll_query, &scroll_alloc, &scroll_offset,
				"{\"scroll\":\"10s\",\"scroll_id\":\"%s\"}\n", TRX_NULL2EMPTY_STR(scroll_id));

		curl_easy_setopt(data->handle, CURLOPT_POSTFIELDS, scroll_query);

		page_r.offset = 0;
		*errbuf = '\0';
		if (CURLE_OK != (err = curl_easy_perform(data->handle)))
		{
			elastic_log_error(data->handle, err, errbuf);
			break;
		}
	}
	while (0 == empty);

	/* as recommended by the elasticsearch documentation, we close the scroll search through a DELETE request */
	if (NULL != scroll_id)
	{
		url_offset = 0;
		trx_snprintf_alloc(&data->post_url, &url_alloc, &url_offset, "%s/_search/scroll/%s", data->base_url,
				scroll_id);

		curl_easy_setopt(data->handle, CURLOPT_URL, data->post_url);
		curl_easy_setopt(data->handle, CURLOPT_POSTFIELDS, NULL);
		curl_easy_setopt(data->handle, CURLOPT_CUSTOMREQUEST, "DELETE");

		treegix_log(LOG_LEVEL_DEBUG, "elasticsearch closing scroll %s", data->post_url);

		page_r.offset = 0;
		*errbuf = '\0';
		if (CURLE_OK != (err = curl_easy_perform(data->handle)))
			elastic_log_error(data->handle, err, errbuf);
	}

out:
	elastic_close(hist);

	curl_slist_free_all(curl_headers);

	trx_json_free(&query);

	trx_free(scroll_id);
	trx_free(scroll_query);

	trx_vector_history_record_sort(values, (trx_compare_func_t)trx_history_record_compare_desc_func);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/************************************************************************************
 *                                                                                  *
 * Function: elastic_add_values                                                     *
 *                                                                                  *
 * Purpose: sends history data to the storage                                       *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *              history - [IN] the history data vector (may have mixed value types) *
 *                                                                                  *
 ************************************************************************************/
static int	elastic_add_values(trx_history_iface_t *hist, const trx_vector_ptr_t *history)
{
	trx_elastic_data_t	*data = (trx_elastic_data_t *)hist->data;
	int			i, num = 0;
	TRX_DC_HISTORY		*h;
	struct trx_json		json_idx, json;
	size_t			buf_alloc = 0, buf_offset = 0;
	char			pipeline[14]; /* index name length + suffix "-pipeline" */

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_json_init(&json_idx, TRX_IDX_JSON_ALLOCATE);

	trx_json_addobject(&json_idx, "index");
	trx_json_addstring(&json_idx, "_index", value_type_str[hist->value_type], TRX_JSON_TYPE_STRING);
	trx_json_addstring(&json_idx, "_type", "values", TRX_JSON_TYPE_STRING);

	if (1 == CONFIG_HISTORY_STORAGE_PIPELINES)
	{
		trx_snprintf(pipeline, sizeof(pipeline), "%s-pipeline", value_type_str[hist->value_type]);
		trx_json_addstring(&json_idx, "pipeline", pipeline, TRX_JSON_TYPE_STRING);
	}

	trx_json_close(&json_idx);
	trx_json_close(&json_idx);

	for (i = 0; i < history->values_num; i++)
	{
		h = (TRX_DC_HISTORY *)history->values[i];

		if (hist->value_type != h->value_type)
			continue;

		trx_json_init(&json, TRX_JSON_ALLOCATE);

		trx_json_adduint64(&json, "itemid", h->itemid);

		trx_json_addstring(&json, "value", history_value2str(h), TRX_JSON_TYPE_STRING);

		if (ITEM_VALUE_TYPE_LOG == h->value_type)
		{
			const trx_log_value_t	*log;

			log = h->value.log;

			trx_json_adduint64(&json, "timestamp", log->timestamp);
			trx_json_addstring(&json, "source", TRX_NULL2EMPTY_STR(log->source), TRX_JSON_TYPE_STRING);
			trx_json_adduint64(&json, "severity", log->severity);
			trx_json_adduint64(&json, "logeventid", log->logeventid);
		}

		trx_json_adduint64(&json, "clock", h->ts.sec);
		trx_json_adduint64(&json, "ns", h->ts.ns);
		trx_json_adduint64(&json, "ttl", h->ttl);

		trx_json_close(&json);

		trx_snprintf_alloc(&data->buf, &buf_alloc, &buf_offset, "%s\n%s\n", json_idx.buffer, json.buffer);

		trx_json_free(&json);

		num++;
	}

	if (num > 0)
	{
		data->post_url = trx_dsprintf(NULL, "%s/_bulk?refresh=true", data->base_url);
		elastic_writer_add_iface(hist);
	}

	trx_json_free(&json_idx);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return num;
}

/************************************************************************************
 *                                                                                  *
 * Function: elastic_flush                                                          *
 *                                                                                  *
 * Purpose: flushes the history data to storage                                     *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *                                                                                  *
 * Comments: This function will try to flush the data until it succeeds or          *
 *           unrecoverable error occurs                                             *
 *                                                                                  *
 ************************************************************************************/
static int	elastic_flush(trx_history_iface_t *hist)
{
	TRX_UNUSED(hist);

	return elastic_writer_flush();
}

/************************************************************************************
 *                                                                                  *
 * Function: trx_history_elastic_init                                               *
 *                                                                                  *
 * Purpose: initializes history storage interface                                   *
 *                                                                                  *
 * Parameters:  hist       - [IN] the history storage interface                     *
 *              value_type - [IN] the target value type                             *
 *              error      - [OUT] the error message                                *
 *                                                                                  *
 * Return value: SUCCEED - the history storage interface was initialized            *
 *               FAIL    - otherwise                                                *
 *                                                                                  *
 ************************************************************************************/
int	trx_history_elastic_init(trx_history_iface_t *hist, unsigned char value_type, char **error)
{
	trx_elastic_data_t	*data;

	if (0 != curl_global_init(CURL_GLOBAL_ALL))
	{
		*error = trx_strdup(*error, "Cannot initialize cURL library");
		return FAIL;
	}

	data = (trx_elastic_data_t *)trx_malloc(NULL, sizeof(trx_elastic_data_t));
	memset(data, 0, sizeof(trx_elastic_data_t));
	data->base_url = trx_strdup(NULL, CONFIG_HISTORY_STORAGE_URL);
	trx_rtrim(data->base_url, "/");
	data->buf = NULL;
	data->post_url = NULL;
	data->handle = NULL;

	hist->value_type = value_type;
	hist->data = data;
	hist->destroy = elastic_destroy;
	hist->add_values = elastic_add_values;
	hist->flush = elastic_flush;
	hist->get_values = elastic_get_values;
	hist->requires_trends = 0;

	return SUCCEED;
}

#else

int	trx_history_elastic_init(trx_history_iface_t *hist, unsigned char value_type, char **error)
{
	TRX_UNUSED(hist);
	TRX_UNUSED(value_type);

	*error = trx_strdup(*error, "cURL library support >= 7.28.0 is required for Elasticsearch history backend");
	return FAIL;
}

#endif
