

#include <sys/procfs.h>
#include "common.h"
#include "sysinfo.h"
#include "trxregexp.h"
#include "log.h"

int	PROC_MEM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	DIR		*dir;
	int		proc;
	struct dirent	*entries;
	trx_stat_t	buf;
	struct passwd	*usrinfo;
	struct prpsinfo	psinfo;
	char		filename[MAX_STRING_LEN];
	char		*procname, *proccomm, *param;
	double		memsize = -1;
	int		pgsize = getpagesize();
	int		proccount = 0, invalid_user = 0, do_task;
	pid_t		curr_pid = getpid();

	if (4 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	procname = get_rparam(request, 0);
	param = get_rparam(request, 1);

	if (NULL != param && '\0' != *param)
	{
		if (NULL == (usrinfo = getpwnam(param)))
			invalid_user = 1;
	}
	else
		usrinfo = NULL;

	param = get_rparam(request, 2);

	if (NULL == param || '\0' == *param || 0 == strcmp(param, "sum"))	/* default parameter */
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

	if (1 == invalid_user)	/* handle 0 for non-existent user after all parameters have been parsed and validated */
		goto out;

	if (NULL == (dir = opendir("/proc")))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot open /proc: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	while (NULL != (entries = readdir(dir)))
	{
		strscpy(filename, "/proc/");
		trx_strlcat(filename, entries->d_name, MAX_STRING_LEN);

		if (0 == trx_stat(filename, &buf))
		{
			proc = open(filename, O_RDONLY);
			if (-1 == proc)
				goto lbl_skip_procces;

			if (-1 == ioctl(proc, PIOCPSINFO, &psinfo))
				goto lbl_skip_procces;

			/* Self process information. It leads to incorrect results for proc.mem[treegix_agentd]. */
			if (psinfo.pr_pid == curr_pid)
				goto lbl_skip_procces;

			if (NULL != procname && '\0' != *procname)
				if (0 == strcmp(procname, psinfo.pr_fname))
					goto lbl_skip_procces;

			if (NULL != usrinfo)
				if (usrinfo->pw_uid != psinfo.pr_uid)
					goto lbl_skip_procces;

			if (NULL != proccomm && '\0' != *proccomm)
				if (NULL == trx_regexp_match(psinfo.pr_psargs, proccomm, NULL))
					goto lbl_skip_procces;

			proccount++;

			if (0 > memsize) /* first initialization */
			{
				memsize = (double)(psinfo.pr_rssize * pgsize);
			}
			else
			{
				if (TRX_DO_MAX == do_task)
					memsize = MAX(memsize, (double)(psinfo.pr_rssize * pgsize));
				else if (TRX_DO_MIN == do_task)
					memsize = MIN(memsize, (double)(psinfo.pr_rssize * pgsize));
				else	/* SUM */
					memsize += (double)(psinfo.pr_rssize * pgsize);
			}
lbl_skip_procces:
			if (-1 != proc)
				close(proc);
		}
	}

	closedir(dir);

	if (0 > memsize)
	{
		/* incorrect process name */
		memsize = 0;
	}
out:
	if (TRX_DO_AVG == do_task)
		SET_DBL_RESULT(result, 0 == proccount ? 0 : memsize / (double)proccount);
	else
		SET_UI64_RESULT(result, memsize);

	return SYSINFO_RET_OK;
}

int	PROC_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	DIR		*dir;
	int		proc;
	struct  dirent	*entries;
	trx_stat_t	buf;
	struct passwd	*usrinfo;
	struct prpsinfo	psinfo;
	char		filename[MAX_STRING_LEN];
	char		*procname, *proccomm, *param;
	int		proccount = 0, invalid_user = 0, trx_proc_stat;
	pid_t		curr_pid = getpid();

	if (4 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	procname = get_rparam(request, 0);
	param = get_rparam(request, 1);

	if (NULL != param && '\0' != *param)
	{
		if (NULL == (usrinfo = getpwnam(param)))
			invalid_user = 1;
	}
	else
		usrinfo = NULL;

	param = get_rparam(request, 2);

	if (NULL == param || '\0' == *param || 0 == strcmp(param, "all"))
		trx_proc_stat = -1;
	else if (0 == strcmp(param, "run"))
		trx_proc_stat = PR_SRUN;
	else if (0 == strcmp(param, "sleep"))
		trx_proc_stat = PR_SSLEEP;
	else if (0 == strcmp(param, "zomb"))
		trx_proc_stat = PR_SZOMB;
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	proccomm = get_rparam(request, 3);

	if (1 == invalid_user)	/* handle 0 for non-existent user after all parameters have been parsed and validated */
		goto out;

	if (NULL == (dir = opendir("/proc")))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot open /proc: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	while (NULL != (entries = readdir(dir)))
	{
		strscpy(filename, "/proc/");
		trx_strlcat(filename, entries->d_name,MAX_STRING_LEN);

		if (0 == trx_stat(filename, &buf))
		{
			proc = open(filename, O_RDONLY);
			if (-1 == proc)
				goto lbl_skip_procces;

			if (-1 == ioctl(proc, PIOCPSINFO, &psinfo))
				goto lbl_skip_procces;

			/* Self process information. It leads to incorrect results for proc.num[treegix_agentd]. */
			if (psinfo.pr_pid == curr_pid)
				goto lbl_skip_procces;

			if (NULL != procname && '\0' != *procname)
				if (0 != strcmp(procname, psinfo.pr_fname))
					goto lbl_skip_procces;

			if (NULL != usrinfo)
				if (usrinfo->pw_uid != psinfo.pr_uid)
					goto lbl_skip_procces;

			if (-1 != trx_proc_stat)
				if (psinfo.pr_sname != trx_proc_stat)
					goto lbl_skip_procces;

			if (NULL != proccomm && '\0' != *proccomm)
				if (NULL == trx_regexp_match(psinfo.pr_psargs, proccomm, NULL))
					goto lbl_skip_procces;

			proccount++;
lbl_skip_procces:
			if (-1 != proc)
				close(proc);
		}
	}

	closedir(dir);
out:
	SET_UI64_RESULT(result, proccount);

	return SYSINFO_RET_OK;
}
