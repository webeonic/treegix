

#include <procfs.h>
#include "common.h"
#include "sysinfo.h"
#include "trxregexp.h"
#include "log.h"
#include "stats.h"

#if !defined(HAVE_ZONE_H) && defined(HAVE_SYS_UTSNAME_H)
#	include <sys/utsname.h>
#endif

extern int	CONFIG_TIMEOUT;

typedef struct
{
	pid_t		pid;
	uid_t		uid;

	char		*name;

	/* process command line in format <arg0> <arg1> ... <argN>\0 */
	char		*cmdline;

#ifdef HAVE_ZONE_H
	zoneid_t	zoneid;
#endif
}
trx_sysinfo_proc_t;

#ifndef HAVE_ZONE_H
/* helper functions for case if agent is compiled on Solaris 9 or earlier where zones are not supported */
/* but is running on a newer Solaris where zones are supported */

/******************************************************************************
 *                                                                            *
 * Function: trx_solaris_version_get                                          *
 *                                                                            *
 * Purpose: get Solaris version at runtime                                    *
 *                                                                            *
 * Parameters:                                                                *
 *     major_version - [OUT] major version (e.g. 5)                           *
 *     minor_version - [OUT] minor version (e.g. 9 for Solaris 9, 10 for      *
 *                           Solaris 10, 11 for Solaris 11)                   *
 * Return value:                                                              *
 *     SUCCEED - no errors, FAIL - an error occurred                          *
 *                                                                            *
 ******************************************************************************/
static int	trx_solaris_version_get(unsigned int *major_version, unsigned int *minor_version)
{
	int		res;
	struct utsname	name;

	if (-1 == (res = uname(&name)))
	{
		treegix_log(LOG_LEVEL_WARNING, "%s(): uname() failed: %s", __func__, trx_strerror(errno));

		return FAIL;
	}

	/* expected result in name.release: "5.9" - Solaris 9, "5.10" - Solaris 10, "5.11" - Solaris 11 */

	if (2 != sscanf(name.release, "%u.%u", major_version, minor_version))
	{
		treegix_log(LOG_LEVEL_WARNING, "%s(): sscanf() failed on: \"%s\"", __func__, name.release);
		THIS_SHOULD_NEVER_HAPPEN;

		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_detect_zone_support                                          *
 *                                                                            *
 * Purpose: find if zones are supported                                       *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - zones supported                                              *
 *     FAIL - zones not supported or error occurred. For our purposes error   *
 *            counts as no support for zones.                                 *
 *                                                                            *
 ******************************************************************************/
static int	trx_detect_zone_support(void)
{
#define TRX_ZONE_SUPPORT_UNKNOWN	0
#define TRX_ZONE_SUPPORT_YES		1
#define TRX_ZONE_SUPPORT_NO		2

	static int	zone_support = TRX_ZONE_SUPPORT_UNKNOWN;
	unsigned int	major, minor;

	switch (zone_support)
	{
		case TRX_ZONE_SUPPORT_NO:
			return FAIL;
		case TRX_ZONE_SUPPORT_YES:
			return SUCCEED;
		default:
			/* zones are supported in Solaris 10 and later (minimum version is "5.10") */

			if (SUCCEED == trx_solaris_version_get(&major, &minor) &&
					((5 == major && 10 <= minor) || 5 < major))
			{
				zone_support = TRX_ZONE_SUPPORT_YES;
				return SUCCEED;
			}
			else	/* failure to get Solaris version also results in "zones not supported" */
			{
				zone_support = TRX_ZONE_SUPPORT_NO;
				return FAIL;
			}
	}
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_sysinfo_proc_free                                            *
 *                                                                            *
 * Purpose: frees process data structure                                      *
 *                                                                            *
 ******************************************************************************/
static void	trx_sysinfo_proc_free(trx_sysinfo_proc_t *proc)
{
	trx_free(proc->name);
	trx_free(proc->cmdline);

	trx_free(proc);
}

static int	check_procstate(psinfo_t *psinfo, int trx_proc_stat)
{
	if (trx_proc_stat == TRX_PROC_STAT_ALL)
		return SUCCEED;

	switch (trx_proc_stat)
	{
		case TRX_PROC_STAT_RUN:
			return (psinfo->pr_lwp.pr_state == SRUN || psinfo->pr_lwp.pr_state == SONPROC) ? SUCCEED : FAIL;
		case TRX_PROC_STAT_SLEEP:
			return (psinfo->pr_lwp.pr_state == SSLEEP) ? SUCCEED : FAIL;
		case TRX_PROC_STAT_ZOMB:
			return (psinfo->pr_lwp.pr_state == SZOMB) ? SUCCEED : FAIL;
	}

	return FAIL;
}

int	PROC_MEM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		tmp[MAX_STRING_LEN], *procname, *proccomm, *param, *memtype = NULL;
	DIR		*dir;
	struct dirent	*entries;
	struct passwd	*usrinfo;
	psinfo_t	psinfo;	/* In the correct procfs.h, the structure name is psinfo_t */
	int		fd = -1, do_task, proccount = 0, invalid_user = 0;
	trx_uint64_t	mem_size = 0, byte_value = 0;
	double		pct_size = 0.0, pct_value = 0.0;
	size_t		*p_value;

	if (5 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	procname = get_rparam(request, 0);
	param = get_rparam(request, 1);

	if (NULL != param && '\0' != *param)
	{
		errno = 0;

		if (NULL == (usrinfo = getpwnam(param)))
		{
			if (0 != errno)
			{
				SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain user information: %s",
						trx_strerror(errno)));
				return SYSINFO_RET_FAIL;
			}

			invalid_user = 1;
		}
	}
	else
		usrinfo = NULL;

	param = get_rparam(request, 2);

	if (NULL == param || '\0' == *param || 0 == strcmp(param, "sum"))
		do_task = TRX_DO_SUM;
	else if (0 == strcmp(param, "avg"))
		do_task = TRX_DO_AVG;
	else if (0 == strcmp(param, "max"))
		do_task = TRX_DO_MAX;
	else if (0 == strcmp(param, "min"))
		do_task = TRX_DO_MIN;
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	proccomm = get_rparam(request, 3);
	memtype = get_rparam(request, 4);

	if (NULL == memtype || '\0' == *memtype || 0 == strcmp(memtype, "vsize"))
	{
		p_value = &psinfo.pr_size;	/* size of process image in Kbytes */
	}
	else if (0 == strcmp(memtype, "rss"))
	{
		p_value = &psinfo.pr_rssize;	/* resident set size in Kbytes */
	}
	else if (0 == strcmp(memtype, "pmem"))
	{
		p_value = NULL;			/* for % of system memory used by process */
	}
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid fifth parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (1 == invalid_user)	/* handle 0 for non-existent user after all parameters have been parsed and validated */
		goto out;

	if (NULL == (dir = opendir("/proc")))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot open /proc: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	while (NULL != (entries = readdir(dir)))
	{
		if (-1 != fd)
		{
			close(fd);
			fd = -1;
		}

		trx_snprintf(tmp, sizeof(tmp), "/proc/%s/psinfo", entries->d_name);

		if (-1 == (fd = open(tmp, O_RDONLY)))
			continue;

		if (-1 == read(fd, &psinfo, sizeof(psinfo)))
			continue;

		if (NULL != procname && '\0' != *procname && 0 != strcmp(procname, psinfo.pr_fname))
			continue;

		if (NULL != usrinfo && usrinfo->pw_uid != psinfo.pr_uid)
			continue;

		if (NULL != proccomm && '\0' != *proccomm && NULL == trx_regexp_match(psinfo.pr_psargs, proccomm, NULL))
			continue;

		if (NULL != p_value)
		{
			/* pr_size or pr_rssize in Kbytes */
			byte_value = *p_value << 10;	/* kB to Byte */

			if (0 != proccount++)
			{
				if (TRX_DO_MAX == do_task)
					mem_size = MAX(mem_size, byte_value);
				else if (TRX_DO_MIN == do_task)
					mem_size = MIN(mem_size, byte_value);
				else
					mem_size += byte_value;
			}
			else
				mem_size = byte_value;
		}
		else
		{
			/* % of system memory used by process, measured in 16-bit binary fractions in the range */
			/* 0.0 - 1.0 with the binary point to the right of the most significant bit. 1.0 == 0x8000 */
			pct_value = (double)((int)psinfo.pr_pctmem * 100) / 32768.0;

			if (0 != proccount++)
			{
				if (TRX_DO_MAX == do_task)
					pct_size = MAX(pct_size, pct_value);
				else if (TRX_DO_MIN == do_task)
					pct_size = MIN(pct_size, pct_value);
				else
					pct_size += pct_value;
			}
			else
				pct_size = pct_value;
		}
	}

	closedir(dir);
	if (-1 != fd)
		close(fd);
out:
	if (NULL != p_value)
	{
		if (TRX_DO_AVG == do_task)
			SET_DBL_RESULT(result, 0 == proccount ? 0.0 : (double)mem_size / (double)proccount);
		else
			SET_UI64_RESULT(result, mem_size);
	}
	else
	{
		if (TRX_DO_AVG == do_task)
			SET_DBL_RESULT(result, 0 == proccount ? 0.0 : pct_size / (double)proccount);
		else
			SET_DBL_RESULT(result, pct_size);
	}

	return SYSINFO_RET_OK;
}

int	PROC_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		tmp[MAX_STRING_LEN], *procname, *proccomm, *param, *zone_parameter;
	DIR		*dir;
	struct dirent	*entries;
	trx_stat_t	buf;
	struct passwd	*usrinfo;
	psinfo_t	psinfo;	/* In the correct procfs.h, the structure name is psinfo_t */
	int		fd = -1, proccount = 0, invalid_user = 0, trx_proc_stat;
#ifdef HAVE_ZONE_H
	zoneid_t	zoneid;
	int		zoneflag;
#endif

	if (5 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	procname = get_rparam(request, 0);
	param = get_rparam(request, 1);

	if (NULL != param && '\0' != *param)
	{
		errno = 0;

		if (NULL == (usrinfo = getpwnam(param)))
		{
			if (0 != errno)
			{
				SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain user information: %s",
						trx_strerror(errno)));
				return SYSINFO_RET_FAIL;
			}

			invalid_user = 1;
		}
	}
	else
		usrinfo = NULL;

	param = get_rparam(request, 2);

	if (NULL == param || '\0' == *param || 0 == strcmp(param, "all"))
		trx_proc_stat = TRX_PROC_STAT_ALL;
	else if (0 == strcmp(param, "run"))
		trx_proc_stat = TRX_PROC_STAT_RUN;
	else if (0 == strcmp(param, "sleep"))
		trx_proc_stat = TRX_PROC_STAT_SLEEP;
	else if (0 == strcmp(param, "zomb"))
		trx_proc_stat = TRX_PROC_STAT_ZOMB;
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	proccomm = get_rparam(request, 3);

	if (NULL == (zone_parameter = get_rparam(request, 4)) || '\0' == *zone_parameter
			|| 0 == strcmp(zone_parameter, "current"))
	{
#ifdef HAVE_ZONE_H
		zoneflag = TRX_PROCSTAT_FLAGS_ZONE_CURRENT;
#else
		if (SUCCEED == trx_detect_zone_support())
		{
			/* Agent has been compiled on Solaris 9 or earlier where zones are not supported */
			/* but now it is running on a system with zone support. This agent cannot limit */
			/* results to only current zone. */

			SET_MSG_RESULT(result, trx_strdup(NULL, "The fifth parameter value \"current\" cannot be used"
					" with agent running on a Solaris version with zone support, but compiled on"
					" a Solaris version without zone support. Consider using \"all\" or install"
					" agent with Solaris zone support."));
			return SYSINFO_RET_FAIL;
		}
#endif
	}
	else if (0 == strcmp(zone_parameter, "all"))
	{
#ifdef HAVE_ZONE_H
		zoneflag = TRX_PROCSTAT_FLAGS_ZONE_ALL;
#endif
	}
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid fifth parameter."));
		return SYSINFO_RET_FAIL;
	}
#ifdef HAVE_ZONE_H
	zoneid = getzoneid();
#endif

	if (1 == invalid_user)	/* handle 0 for non-existent user after all parameters have been parsed and validated */
		goto out;

	if (NULL == (dir = opendir("/proc")))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot open /proc: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	while (NULL != (entries = readdir(dir)))
	{
		if (-1 != fd)
		{
			close(fd);
			fd = -1;
		}

		trx_snprintf(tmp, sizeof(tmp), "/proc/%s/psinfo", entries->d_name);

		if (0 != trx_stat(tmp, &buf))
			continue;

		if (-1 == (fd = open(tmp, O_RDONLY)))
			continue;

		if (-1 == read(fd, &psinfo, sizeof(psinfo)))
			continue;

		if (NULL != procname && '\0' != *procname && 0 != strcmp(procname, psinfo.pr_fname))
			continue;

		if (NULL != usrinfo && usrinfo->pw_uid != psinfo.pr_uid)
			continue;

		if (FAIL == check_procstate(&psinfo, trx_proc_stat))
			continue;

		if (NULL != proccomm && '\0' != *proccomm && NULL == trx_regexp_match(psinfo.pr_psargs, proccomm, NULL))
			continue;

#ifdef HAVE_ZONE_H
		if (TRX_PROCSTAT_FLAGS_ZONE_CURRENT == zoneflag && zoneid != psinfo.pr_zoneid)
			continue;
#endif
		proccount++;
	}

	closedir(dir);
	if (-1 != fd)
		close(fd);
out:
	SET_UI64_RESULT(result, proccount);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: proc_match_name                                                  *
 *                                                                            *
 * Purpose: checks if the process name matches filter                         *
 *                                                                            *
 ******************************************************************************/
static int	proc_match_name(const trx_sysinfo_proc_t *proc, const char *procname)
{
	if (NULL == procname)
		return SUCCEED;

	if (NULL != proc->name && 0 == strcmp(procname, proc->name))
		return SUCCEED;

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: proc_match_user                                                  *
 *                                                                            *
 * Purpose: checks if the process user matches filter                         *
 *                                                                            *
 ******************************************************************************/
static int	proc_match_user(const trx_sysinfo_proc_t *proc, const struct passwd *usrinfo)
{
	if (NULL == usrinfo)
		return SUCCEED;

	if (proc->uid == usrinfo->pw_uid)
		return SUCCEED;

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: proc_match_cmdline                                               *
 *                                                                            *
 * Purpose: checks if the process command line matches filter                 *
 *                                                                            *
 ******************************************************************************/
static int	proc_match_cmdline(const trx_sysinfo_proc_t *proc, const char *cmdline)
{
	if (NULL == cmdline)
		return SUCCEED;

	if (NULL != proc->cmdline && NULL != trx_regexp_match(proc->cmdline, cmdline, NULL))
		return SUCCEED;

	return FAIL;
}

#ifdef HAVE_ZONE_H
/******************************************************************************
 *                                                                            *
 * Function: proc_match_zone                                                  *
 *                                                                            *
 * Purpose: checks if the process zone matches filter                         *
 *                                                                            *
 ******************************************************************************/
static int	proc_match_zone(const trx_sysinfo_proc_t *proc, trx_uint64_t flags, zoneid_t zoneid)
{
	if (0 != (TRX_PROCSTAT_FLAGS_ZONE_ALL & flags))
		return SUCCEED;

	if (proc->zoneid == zoneid)
		return SUCCEED;

	return FAIL;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: proc_read_cpu_util                                               *
 *                                                                            *
 * Purpose: reads process cpu utilization values from /proc/[pid]/usage file  *
 *                                                                            *
 * Parameters: procutil - [IN/OUT] the process cpu utilization data           *
 *                                                                            *
 * Return value: SUCCEED - the process cpu utilization data was read          *
 *                         successfully                                       *
 *               <0      - otherwise, -errno code is returned                 *
 *                                                                            *
 * Comments: we use /proc/[pid]/usage since /proc/[pid]/status contains       *
 *           sensitive information and by default can only be read by the     *
 *           owner or privileged user.                                        *
 *                                                                            *
 *           In addition to user and system-call CPU time the                 *
 *           /proc/[pid]/usage also contains CPU time spent in trap context   *
 *           Currently trap CPU time is not taken into account.               *
 *                                                                            *
 *           prstat(1) skips processes 0 (sched), 2 (pageout) and 3 (fsflush) *
 *           however we take them into account.                               *
 *                                                                            *
 ******************************************************************************/
static int	proc_read_cpu_util(trx_procstat_util_t *procutil)
{
	int		fd, n;
	char		tmp[MAX_STRING_LEN];
	psinfo_t	psinfo;
	prusage_t	prusage;

	trx_snprintf(tmp, sizeof(tmp), "/proc/%d/psinfo", (int)procutil->pid);

	if (-1 == (fd = open(tmp, O_RDONLY)))
		return -errno;

	n = read(fd, &psinfo, sizeof(psinfo));
	close(fd);

	if (-1 == n)
		return -errno;

	procutil->starttime = psinfo.pr_start.tv_sec;

	trx_snprintf(tmp, sizeof(tmp), "/proc/%d/usage", (int)procutil->pid);

	if (-1 == (fd = open(tmp, O_RDONLY)))
		return -errno;

	n = read(fd, &prusage, sizeof(prusage));
	close(fd);

	if (-1 == n)
		return -errno;

	/* convert cpu utilization time to clock ticks */
	procutil->utime = ((trx_uint64_t)prusage.pr_utime.tv_sec * 1e9 + prusage.pr_utime.tv_nsec) *
			sysconf(_SC_CLK_TCK) / 1e9;

	procutil->stime = ((trx_uint64_t)prusage.pr_stime.tv_sec * 1e9 + prusage.pr_stime.tv_nsec) *
			sysconf(_SC_CLK_TCK) / 1e9;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_proc_get_process_stats                                       *
 *                                                                            *
 * Purpose: get process cpu utilization data                                  *
 *                                                                            *
 * Parameters: procs     - [IN/OUT] an array of process utilization data      *
 *             procs_num - [IN] the number of items in procs array            *
 *                                                                            *
 ******************************************************************************/
void	trx_proc_get_process_stats(trx_procstat_util_t *procs, int procs_num)
{
	int	i;

	treegix_log(LOG_LEVEL_TRACE, "In %s() procs_num:%d", __func__, procs_num);

	for (i = 0; i < procs_num; i++)
		procs[i].error = proc_read_cpu_util(&procs[i]);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_proc_get_processes                                           *
 *                                                                            *
 * Purpose: get system processes                                              *
 *                                                                            *
 * Parameters: processes - [OUT] the system processes                         *
 *             flags     - [IN] the flags specifying the process properties   *
 *                              that must be returned                         *
 *                                                                            *
 * Return value: SUCCEED - the system processes were retrieved successfully   *
 *               FAIL    - failed to open /proc directory                     *
 *                                                                            *
 ******************************************************************************/
int	trx_proc_get_processes(trx_vector_ptr_t *processes, unsigned int flags)
{
	DIR			*dir;
	struct dirent		*entries;
	char			tmp[MAX_STRING_LEN];
	int			pid, ret = FAIL, fd = -1, n;
	psinfo_t		psinfo;	/* In the correct procfs.h, the structure name is psinfo_t */
	trx_sysinfo_proc_t	*proc;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	if (NULL == (dir = opendir("/proc")))
		goto out;

	while (NULL != (entries = readdir(dir)))
	{
		/* skip entries not containing pids */
		if (FAIL == is_uint32(entries->d_name, &pid))
			continue;

		trx_snprintf(tmp, sizeof(tmp), "/proc/%s/psinfo", entries->d_name);

		if (-1 == (fd = open(tmp, O_RDONLY)))
			continue;

		n = read(fd, &psinfo, sizeof(psinfo));
		close(fd);

		if (-1 == n)
			continue;

		proc = (trx_sysinfo_proc_t *)trx_malloc(NULL, sizeof(trx_sysinfo_proc_t));
		memset(proc, 0, sizeof(trx_sysinfo_proc_t));

		proc->pid = pid;

		if (0 != (flags & TRX_SYSINFO_PROC_NAME))
			proc->name = trx_strdup(NULL, psinfo.pr_fname);

		if (0 != (flags & TRX_SYSINFO_PROC_USER))
			proc->uid = psinfo.pr_uid;

		if (0 != (flags & TRX_SYSINFO_PROC_CMDLINE))
			proc->cmdline = trx_strdup(NULL, psinfo.pr_psargs);

#ifdef HAVE_ZONE_H
		proc->zoneid = psinfo.pr_zoneid;
#endif

		trx_vector_ptr_append(processes, proc);
	}

	closedir(dir);

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_TRACE, "End of %s(): %s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_proc_free_processes                                          *
 *                                                                            *
 * Purpose: frees process vector read by trx_proc_get_processes function      *
 *                                                                            *
 * Parameters: processes - [IN/OUT] the process vector to free                *
 *                                                                            *
 ******************************************************************************/
void	trx_proc_free_processes(trx_vector_ptr_t *processes)
{
	trx_vector_ptr_clear_ext(processes, (trx_mem_free_func_t)trx_sysinfo_proc_free);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_proc_get_matching_pids                                       *
 *                                                                            *
 * Purpose: get pids matching the specified process name, user name and       *
 *          command line                                                      *
 *                                                                            *
 * Parameters: procname    - [IN] the process name, NULL - all                *
 *             username    - [IN] the user name, NULL - all                   *
 *             cmdline     - [IN] the command line, NULL - all                *
 *             pids        - [OUT] the vector of matching pids                *
 *                                                                            *
 * Return value: SUCCEED   - the pids were read successfully                  *
 *               -errno    - failed to read pids                              *
 *                                                                            *
 ******************************************************************************/
void	trx_proc_get_matching_pids(const trx_vector_ptr_t *processes, const char *procname, const char *username,
		const char *cmdline, trx_uint64_t flags, trx_vector_uint64_t *pids)
{
	struct passwd		*usrinfo;
	int			i;
	trx_sysinfo_proc_t	*proc;
#ifdef HAVE_ZONE_H
	zoneid_t		zoneid;
#endif

	treegix_log(LOG_LEVEL_TRACE, "In %s() procname:%s username:%s cmdline:%s zone:%d", __func__,
			TRX_NULL2EMPTY_STR(procname), TRX_NULL2EMPTY_STR(username), TRX_NULL2EMPTY_STR(cmdline), flags);

	if (NULL != username)
	{
		/* in the case of invalid user there are no matching processes, return empty vector */
		if (NULL == (usrinfo = getpwnam(username)))
			goto out;
	}
	else
		usrinfo = NULL;

#ifdef HAVE_ZONE_H
	zoneid = getzoneid();
#endif

	for (i = 0; i < processes->values_num; i++)
	{
		proc = (trx_sysinfo_proc_t *)processes->values[i];

		if (SUCCEED != proc_match_user(proc, usrinfo))
			continue;

		if (SUCCEED != proc_match_name(proc, procname))
			continue;

		if (SUCCEED != proc_match_cmdline(proc, cmdline))
			continue;

#ifdef HAVE_ZONE_H
		if (SUCCEED != proc_match_zone(proc, flags, zoneid))
			continue;
#endif

		trx_vector_uint64_append(pids, (trx_uint64_t)proc->pid);
	}
out:
	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

int	PROC_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	const char	*procname, *username, *cmdline, *tmp, *flags;
	char		*errmsg = NULL;
	int		period, type;
	double		value;
	trx_uint64_t	zoneflag;
	trx_timespec_t	ts_timeout, ts;

	/* proc.cpu.util[<procname>,<username>,(user|system),<cmdline>,(avg1|avg5|avg15),(current|all)] */
	if (6 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* trx_procstat_get_* functions expect NULL for default values -       */
	/* convert empty procname, username and cmdline strings to NULL values */
	if (NULL != (procname = get_rparam(request, 0)) && '\0' == *procname)
		procname = NULL;

	if (NULL != (username = get_rparam(request, 1)) && '\0' == *username)
		username = NULL;

	if (NULL != (cmdline = get_rparam(request, 3)) && '\0' == *cmdline)
		cmdline = NULL;

	/* utilization type parameter (user|system) */
	if (NULL == (tmp = get_rparam(request, 2)) || '\0' == *tmp || 0 == strcmp(tmp, "total"))
	{
		type = TRX_PROCSTAT_CPU_TOTAL;
	}
	else if (0 == strcmp(tmp, "user"))
	{
		type = TRX_PROCSTAT_CPU_USER;
	}
	else if (0 == strcmp(tmp, "system"))
	{
		type = TRX_PROCSTAT_CPU_SYSTEM;
	}
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* mode parameter (avg1|avg5|avg15) */
	if (NULL == (tmp = get_rparam(request, 4)) || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
	{
		period = SEC_PER_MIN;
	}
	else if (0 == strcmp(tmp, "avg5"))
	{
		period = SEC_PER_MIN * 5;
	}
	else if (0 == strcmp(tmp, "avg15"))
	{
		period = SEC_PER_MIN * 15;
	}
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid fifth parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (flags = get_rparam(request, 5)) || '\0' == *flags || 0 == strcmp(flags, "current"))
	{
#ifndef HAVE_ZONE_H
		if (SUCCEED == trx_detect_zone_support())
		{
			/* Agent has been compiled on Solaris 9 or earlier where zones are not supported */
			/* but now it is running on a system with zone support. This agent cannot limit */
			/* results to only current zone. */

			SET_MSG_RESULT(result, trx_strdup(NULL, "The sixth parameter value \"current\" cannot be used"
					" with agent running on a Solaris version with zone support, but compiled on"
					" a Solaris version without zone support. Consider using \"all\" or install"
					" agent with Solaris zone support."));
			return SYSINFO_RET_FAIL;
		}

		/* zones are not supported, the agent can accept 6th parameter with default value "current" */
#endif
		zoneflag = TRX_PROCSTAT_FLAGS_ZONE_CURRENT;
	}
	else if (0 == strcmp(flags, "all"))
	{
		zoneflag = TRX_PROCSTAT_FLAGS_ZONE_ALL;
	}
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid sixth parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (SUCCEED != trx_procstat_collector_started())
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Collector is not started."));
		return SYSINFO_RET_FAIL;
	}

	trx_timespec(&ts_timeout);
	ts_timeout.sec += CONFIG_TIMEOUT;

	while (SUCCEED != trx_procstat_get_util(procname, username, cmdline, zoneflag, period, type, &value, &errmsg))
	{
		/* trx_procstat_get_* functions will return FAIL when either a collection   */
		/* error was registered or if less than 2 data samples were collected.      */
		/* In the first case the errmsg will contain error message.                 */
		if (NULL != errmsg)
		{
			SET_MSG_RESULT(result, errmsg);
			return SYSINFO_RET_FAIL;
		}

		trx_timespec(&ts);

		if (0 > trx_timespec_compare(&ts_timeout, &ts))
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Timeout while waiting for collector data."));
			return SYSINFO_RET_FAIL;
		}

		sleep(1);
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
}
