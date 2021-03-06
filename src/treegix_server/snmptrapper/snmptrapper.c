

#include "common.h"
#include "dbcache.h"
#include "trxself.h"
#include "daemon.h"
#include "log.h"
#include "proxy.h"
#include "snmptrapper.h"
#include "trxserver.h"
#include "trxregexp.h"
#include "preproc.h"

static int	trap_fd = -1;
static off_t	trap_lastsize;
static ino_t	trap_ino = 0;
static char	*buffer = NULL;
static int	offset = 0;
static int	force = 0;

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

static void	DBget_lastsize(void)
{
	DB_RESULT	result;
	DB_ROW		row;

	DBbegin();

	result = DBselect("select snmp_lastsize from globalvars");

	if (NULL == (row = DBfetch(result)))
	{
		DBexecute("insert into globalvars (globalvarid,snmp_lastsize) values (1,0)");
		trap_lastsize = 0;
	}
	else
		TRX_STR2UINT64(trap_lastsize, row[0]);

	DBfree_result(result);

	DBcommit();
}

static void	DBupdate_lastsize(void)
{
	DBbegin();
	DBexecute("update globalvars set snmp_lastsize=%lld", (long long int)trap_lastsize);
	DBcommit();
}

/******************************************************************************
 *                                                                            *
 * Function: process_trap_for_interface                                       *
 *                                                                            *
 * Purpose: add trap to all matching items for the specified interface        *
 *                                                                            *
 * Return value: SUCCEED - a matching item was found                          *
 *               FAIL - no matching item was found (including fallback items) *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
static int	process_trap_for_interface(trx_uint64_t interfaceid, char *trap, trx_timespec_t *ts)
{
	DC_ITEM			*items = NULL;
	const char		*regex;
	char			error[ITEM_ERROR_LEN_MAX];
	size_t			num, i;
	int			ret = FAIL, fb = -1, *lastclocks = NULL, *errcodes = NULL, value_type, regexp_ret;
	trx_uint64_t		*itemids = NULL;
	unsigned char		*states = NULL;
	AGENT_RESULT		*results = NULL;
	AGENT_REQUEST		request;
	trx_vector_ptr_t	regexps;

	trx_vector_ptr_create(&regexps);

	num = DCconfig_get_snmp_items_by_interfaceid(interfaceid, &items);

	itemids = (trx_uint64_t *)trx_malloc(itemids, sizeof(trx_uint64_t) * num);
	states = (unsigned char *)trx_malloc(states, sizeof(unsigned char) * num);
	lastclocks = (int *)trx_malloc(lastclocks, sizeof(int) * num);
	errcodes = (int *)trx_malloc(errcodes, sizeof(int) * num);
	results = (AGENT_RESULT *)trx_malloc(results, sizeof(AGENT_RESULT) * num);

	for (i = 0; i < num; i++)
	{
		init_result(&results[i]);
		errcodes[i] = FAIL;

		items[i].key = trx_strdup(items[i].key, items[i].key_orig);
		if (SUCCEED != substitute_key_macros(&items[i].key, NULL, &items[i], NULL, NULL,
				MACRO_TYPE_ITEM_KEY, error, sizeof(error)))
		{
			SET_MSG_RESULT(&results[i], trx_strdup(NULL, error));
			errcodes[i] = NOTSUPPORTED;
			continue;
		}

		if (0 == strcmp(items[i].key, "snmptrap.fallback"))
		{
			fb = i;
			continue;
		}

		init_request(&request);

		if (SUCCEED != parse_item_key(items[i].key, &request))
			goto next;

		if (0 != strcmp(get_rkey(&request), "snmptrap"))
			goto next;

		if (1 < get_rparams_num(&request))
			goto next;

		if (NULL != (regex = get_rparam(&request, 0)))
		{
			if ('@' == *regex)
			{
				DCget_expressions_by_name(&regexps, regex + 1);

				if (0 == regexps.values_num)
				{
					SET_MSG_RESULT(&results[i], trx_dsprintf(NULL,
							"Global regular expression \"%s\" does not exist.", regex + 1));
					errcodes[i] = NOTSUPPORTED;
					goto next;
				}
			}

			if (TRX_REGEXP_NO_MATCH == (regexp_ret = regexp_match_ex(&regexps, trap, regex,
					TRX_CASE_SENSITIVE)))
			{
				goto next;
			}
			else if (FAIL == regexp_ret)
			{
				SET_MSG_RESULT(&results[i], trx_dsprintf(NULL,
						"Invalid regular expression \"%s\".", regex));
				errcodes[i] = NOTSUPPORTED;
				goto next;
			}
		}

		value_type = (ITEM_VALUE_TYPE_LOG == items[i].value_type ? ITEM_VALUE_TYPE_LOG : ITEM_VALUE_TYPE_TEXT);
		set_result_type(&results[i], value_type, trap);
		errcodes[i] = SUCCEED;
		ret = SUCCEED;
next:
		free_request(&request);
	}

	if (FAIL == ret && -1 != fb)
	{
		value_type = (ITEM_VALUE_TYPE_LOG == items[fb].value_type ? ITEM_VALUE_TYPE_LOG : ITEM_VALUE_TYPE_TEXT);
		set_result_type(&results[fb], value_type, trap);
		errcodes[fb] = SUCCEED;
		ret = SUCCEED;
	}

	for (i = 0; i < num; i++)
	{
		switch (errcodes[i])
		{
			case SUCCEED:
				if (ITEM_VALUE_TYPE_LOG == items[i].value_type)
				{
					calc_timestamp(results[i].log->value, &results[i].log->timestamp,
							items[i].logtimefmt);
				}

				items[i].state = ITEM_STATE_NORMAL;
				trx_preprocess_item_value(items[i].itemid, items[i].value_type, items[i].flags,
						&results[i], ts, items[i].state, NULL);

				itemids[i] = items[i].itemid;
				states[i] = items[i].state;
				lastclocks[i] = ts->sec;
				break;
			case NOTSUPPORTED:
				items[i].state = ITEM_STATE_NOTSUPPORTED;
				trx_preprocess_item_value(items[i].itemid, items[i].value_type, items[i].flags, NULL,
						ts, items[i].state, results[i].msg);

				itemids[i] = items[i].itemid;
				states[i] = items[i].state;
				lastclocks[i] = ts->sec;
				break;
		}

		trx_free(items[i].key);
		free_result(&results[i]);
	}

	trx_free(results);

	DCrequeue_items(itemids, states, lastclocks, errcodes, num);

	trx_free(errcodes);
	trx_free(lastclocks);
	trx_free(states);
	trx_free(itemids);

	DCconfig_clean_items(items, NULL, num);
	trx_free(items);

	trx_regexp_clean_expressions(&regexps);
	trx_vector_ptr_destroy(&regexps);

	trx_preprocessor_flush();

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_trap                                                     *
 *                                                                            *
 * Purpose: process a single trap                                             *
 *                                                                            *
 * Parameters: addr - [IN] address of the target interface(s)                 *
 *             begin - [IN] beginning of the trap message                     *
 *             end - [IN] end of the trap message                             *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
static void	process_trap(const char *addr, char *begin, char *end)
{
	trx_timespec_t	ts;
	trx_uint64_t	*interfaceids = NULL;
	int		count, i, ret = FAIL;
	char		*trap = NULL;

	trx_timespec(&ts);
	trap = trx_dsprintf(trap, "%s%s", begin, end);

	count = DCconfig_get_snmp_interfaceids_by_addr(addr, &interfaceids);

	for (i = 0; i < count; i++)
	{
		if (SUCCEED == process_trap_for_interface(interfaceids[i], trap, &ts))
			ret = SUCCEED;
	}

	if (FAIL == ret)
	{
		trx_config_t	cfg;

		trx_config_get(&cfg, TRX_CONFIG_FLAGS_SNMPTRAP_LOGGING);

		if (TRX_SNMPTRAP_LOGGING_ENABLED == cfg.snmptrap_logging)
			treegix_log(LOG_LEVEL_WARNING, "unmatched trap received from \"%s\": %s", addr, trap);

		trx_config_clean(&cfg);
	}

	trx_free(interfaceids);
	trx_free(trap);
}

/******************************************************************************
 *                                                                            *
 * Function: parse_traps                                                      *
 *                                                                            *
 * Purpose: split traps and process them with process_trap()                  *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
static void	parse_traps(int flag)
{
	char	*c, *line, *begin = NULL, *end = NULL, *addr = NULL, *pzbegin, *pzaddr = NULL, *pzdate = NULL;

	c = line = buffer;

	while ('\0' != *c)
	{
		if ('\n' == *c)
		{
			line = ++c;
			continue;
		}

		if (0 != strncmp(c, "TRXTRAP", 7))
		{
			c++;
			continue;
		}

		pzbegin = c;

		c += 7;	/* c now points to the delimiter between "TRXTRAP" and address */

		while ('\0' != *c && NULL != strchr(TRX_WHITESPACE, *c))
			c++;

		/* c now points to the address */

		/* process the previous trap */
		if (NULL != begin)
		{
			*(line - 1) = '\0';
			*pzdate = '\0';
			*pzaddr = '\0';

			process_trap(addr, begin, end);
			end = NULL;
		}

		/* parse the current trap */
		begin = line;
		addr = c;
		pzdate = pzbegin;

		while ('\0' != *c && NULL == strchr(TRX_WHITESPACE, *c))
			c++;

		pzaddr = c;

		end = c + 1;	/* the rest of the trap */
	}

	if (0 == flag)
	{
		if (NULL == begin)
			offset = c - buffer;
		else
			offset = c - begin;

		if (offset == MAX_BUFFER_LEN - 1)
		{
			if (NULL != end)
			{
				treegix_log(LOG_LEVEL_WARNING, "SNMP trapper buffer is full,"
						" trap data might be truncated");
				parse_traps(1);
			}
			else
				treegix_log(LOG_LEVEL_WARNING, "failed to find trap in SNMP trapper file");

			offset = 0;
			*buffer = '\0';
		}
		else
		{
			if (NULL != begin && begin != buffer)
				memmove(buffer, begin, offset + 1);
		}
	}
	else
	{
		if (NULL != end)
		{
			*(line - 1) = '\0';
			*pzdate = '\0';
			*pzaddr = '\0';

			process_trap(addr, begin, end);
			offset = 0;
			*buffer = '\0';
		}
		else
		{
			treegix_log(LOG_LEVEL_WARNING, "invalid trap data found \"%s\"", buffer);
			offset = 0;
			*buffer = '\0';
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: delay_trap_logs                                                  *
 *                                                                            *
 * Purpose: delay SNMP trapper file related issue log entries for 60 seconds  *
 *          unless this is the first time this issue has occurred             *
 *                                                                            *
 * Parameters: error     - [IN] string containing log entry text              *
 *             log_level - [IN] the log entry log level                       *
 *                                                                            *
 ******************************************************************************/
static void	delay_trap_logs(char *error, int log_level)
{
	int			now;
	static int		lastlogtime = 0;
	static trx_hash_t	last_error_hash = 0;
	trx_hash_t		error_hash;

	now = (int)time(NULL);
	error_hash = trx_default_string_hash_func(error);

	if (LOG_ENTRY_INTERVAL_DELAY <= now - lastlogtime || last_error_hash != error_hash)
	{
		treegix_log(log_level, "%s", error);
		lastlogtime = now;
		last_error_hash = error_hash;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: read_traps                                                       *
 *                                                                            *
 * Purpose: read the traps and then parse them with parse_traps()             *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
static int	read_traps(void)
{
	int	nbytes = 0;
	char	*error = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() lastsize: %lld", __func__, (long long int)trap_lastsize);

	if (-1 == lseek(trap_fd, trap_lastsize, SEEK_SET))
	{
		error = trx_dsprintf(error, "cannot set position to %lld for \"%s\": %s", (long long int)trap_lastsize,
				CONFIG_SNMPTRAP_FILE, trx_strerror(errno));
		delay_trap_logs(error, LOG_LEVEL_WARNING);
		goto out;
	}

	if (-1 == (nbytes = read(trap_fd, buffer + offset, MAX_BUFFER_LEN - offset - 1)))
	{
		error = trx_dsprintf(error, "cannot read from SNMP trapper file \"%s\": %s",
				CONFIG_SNMPTRAP_FILE, trx_strerror(errno));
		delay_trap_logs(error, LOG_LEVEL_WARNING);
		goto out;
	}

	if (0 < nbytes)
	{
		buffer[nbytes + offset] = '\0';
		trap_lastsize += nbytes;
		DBupdate_lastsize();
		parse_traps(0);
	}
out:
	trx_free(error);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return nbytes;
}

/******************************************************************************
 *                                                                            *
 * Function: close_trap_file                                                  *
 *                                                                            *
 * Purpose: close trap file and reset lastsize                                *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 * Comments: !!! do not reset lastsize elsewhere !!!                          *
 *                                                                            *
 ******************************************************************************/
static void	close_trap_file(void)
{
	if (-1 != trap_fd)
		close(trap_fd);

	trap_fd = -1;
	trap_lastsize = 0;
	DBupdate_lastsize();
}

/******************************************************************************
 *                                                                            *
 * Function: open_trap_file                                                   *
 *                                                                            *
 * Purpose: open the trap file and get it's node number                       *
 *                                                                            *
 * Return value: file descriptor of the opened file or -1 otherwise           *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
static int	open_trap_file(void)
{
	trx_stat_t	file_buf;
	char		*error = NULL;

	if (0 != trx_stat(CONFIG_SNMPTRAP_FILE, &file_buf))
	{
		error = trx_dsprintf(error, "cannot stat SNMP trapper file \"%s\": %s", CONFIG_SNMPTRAP_FILE,
				trx_strerror(errno));
		delay_trap_logs(error, LOG_LEVEL_CRIT);
		goto out;
	}

	if (-1 == (trap_fd = open(CONFIG_SNMPTRAP_FILE, O_RDONLY)))
	{
		if (ENOENT != errno)	/* file exists but cannot be opened */
		{
			error = trx_dsprintf(error, "cannot open SNMP trapper file \"%s\": %s",
					CONFIG_SNMPTRAP_FILE, trx_strerror(errno));
			delay_trap_logs(error, LOG_LEVEL_CRIT);
		}
		goto out;
	}

	trap_ino = file_buf.st_ino;	/* a new file was opened */
out:
	trx_free(error);

	return trap_fd;
}

/******************************************************************************
 *                                                                            *
 * Function: get_latest_data                                                  *
 *                                                                            *
 * Purpose: Open the latest trap file. If the current file has been rotated,  *
 *          process that and then open the latest file.                       *
 *                                                                            *
 * Return value: SUCCEED - there are new traps to be parsed                   *
 *               FAIL - there are no new traps or trap file does not exist    *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
static int	get_latest_data(void)
{
	trx_stat_t	file_buf;

	if (-1 != trap_fd)	/* a trap file is already open */
	{
		if (0 != trx_stat(CONFIG_SNMPTRAP_FILE, &file_buf))
		{
			/* file might have been renamed or deleted, process the current file */

			if (ENOENT != errno)
			{
				treegix_log(LOG_LEVEL_CRIT, "cannot stat SNMP trapper file \"%s\": %s",
						CONFIG_SNMPTRAP_FILE, trx_strerror(errno));
			}

			while (0 < read_traps())
				;

			if (0 != offset)
				parse_traps(1);

			close_trap_file();
		}
		else if (file_buf.st_ino != trap_ino || file_buf.st_size < trap_lastsize)
		{
			/* file has been rotated, process the current file */

			while (0 < read_traps())
				;

			if (0 != offset)
				parse_traps(1);

			close_trap_file();
		}
		/* in case when file access permission is changed and read permission is denied */
		else if (0 != access(CONFIG_SNMPTRAP_FILE, R_OK))
		{
			if (EACCES == errno)
				close_trap_file();
		}
		else if (file_buf.st_size == trap_lastsize)
		{
			if (1 == force)
			{
				parse_traps(1);
				force = 0;
			}
			else if (0 != offset && 0 == force)
			{
				force = 1;
			}

			return FAIL;	/* no new traps */
		}
	}

	force = 0;

	if (-1 == trap_fd && -1 == open_trap_file())
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: main_snmptrapper_loop                                            *
 *                                                                            *
 * Purpose: SNMP trap reader's entry point                                    *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
TRX_THREAD_ENTRY(snmptrapper_thread, args)
{
	double	sec;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	treegix_log(LOG_LEVEL_DEBUG, "In %s() trapfile:'%s'", __func__, CONFIG_SNMPTRAP_FILE);

	trx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

	DBconnect(TRX_DB_CONNECT_NORMAL);

	DBget_lastsize();

	buffer = (char *)trx_malloc(buffer, MAX_BUFFER_LEN);
	*buffer = '\0';

	while (TRX_IS_RUNNING())
	{
		sec = trx_time();
		trx_update_env(sec);

		trx_setproctitle("%s [processing data]", get_process_type_string(process_type));

		while (TRX_IS_RUNNING() && SUCCEED == get_latest_data())
			read_traps();
		sec = trx_time() - sec;

		trx_setproctitle("%s [processed data in " TRX_FS_DBL " sec, idle 1 sec]",
				get_process_type_string(process_type), sec);

		trx_sleep_loop(1);
	}

	trx_free(buffer);

	if (-1 != trap_fd)
		close(trap_fd);

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
}
