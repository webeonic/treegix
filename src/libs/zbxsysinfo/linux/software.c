

#include "sysinfo.h"
#include "trxalgo.h"
#include "trxexec.h"
#include "cfg.h"
#include "software.h"
#include "trxregexp.h"
#include "log.h"

#ifdef HAVE_SYS_UTSNAME_H
#       include <sys/utsname.h>
#endif

int	SYSTEM_SW_ARCH(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	struct utsname	name;

	TRX_UNUSED(request);

	if (-1 == uname(&name))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_STR_RESULT(result, trx_strdup(NULL, name.machine));

	return SYSINFO_RET_OK;
}

int     SYSTEM_SW_OS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*type, line[MAX_STRING_LEN], tmp_line[MAX_STRING_LEN];
	int	ret = SYSINFO_RET_FAIL, line_read = FAIL;
	FILE	*f = NULL;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return ret;
	}

	type = get_rparam(request, 0);

	if (NULL == type || '\0' == *type || 0 == strcmp(type, "full"))
	{
		if (NULL == (f = fopen(SW_OS_FULL, "r")))
		{
			SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot open " SW_OS_FULL ": %s",
					trx_strerror(errno)));
			return ret;
		}
	}
	else if (0 == strcmp(type, "short"))
	{
		if (NULL == (f = fopen(SW_OS_SHORT, "r")))
		{
			SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot open " SW_OS_SHORT ": %s",
					trx_strerror(errno)));
			return ret;
		}
	}
	else if (0 == strcmp(type, "name"))
	{
		/* firstly need to check option PRETTY_NAME in /etc/os-release */
		/* if cannot find it, get value from /etc/issue.net            */
		if (NULL != (f = fopen(SW_OS_NAME_RELEASE, "r")))
		{
			while (NULL != fgets(tmp_line, sizeof(tmp_line), f))
			{
				if (0 != strncmp(tmp_line, SW_OS_OPTION_PRETTY_NAME,
						TRX_CONST_STRLEN(SW_OS_OPTION_PRETTY_NAME)))
					continue;

				if (1 == sscanf(tmp_line, SW_OS_OPTION_PRETTY_NAME "=\"%[^\"]", line))
				{
					line_read = SUCCEED;
					break;
				}
			}
			trx_fclose(f);
		}

		if (FAIL == line_read && NULL == (f = fopen(SW_OS_NAME, "r")))
		{
			SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot open " SW_OS_NAME ": %s",
					trx_strerror(errno)));
			return ret;
		}
	}
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		return ret;
	}

	if (SUCCEED == line_read || NULL != fgets(line, sizeof(line), f))
	{
		ret = SYSINFO_RET_OK;
		trx_rtrim(line, TRX_WHITESPACE);
		SET_STR_RESULT(result, trx_strdup(NULL, line));
	}
	else
		SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot read from file."));

	trx_fclose(f);

	return ret;
}

static int	dpkg_parser(const char *line, char *package, size_t max_package_len)
{
	char	fmt[32], tmp[32];

	trx_snprintf(fmt, sizeof(fmt), "%%" TRX_FS_SIZE_T "s %%" TRX_FS_SIZE_T "s",
			(trx_fs_size_t)(max_package_len - 1), (trx_fs_size_t)(sizeof(tmp) - 1));

	if (2 != sscanf(line, fmt, package, tmp) || 0 != strcmp(tmp, "install"))
		return FAIL;

	return SUCCEED;
}

static size_t	print_packages(char *buffer, size_t size, trx_vector_str_t *packages, const char *manager)
{
	size_t	offset = 0;
	int	i;

	if (NULL != manager)
		offset += trx_snprintf(buffer + offset, size - offset, "[%s]", manager);

	if (0 < packages->values_num)
	{
		if (NULL != manager)
			offset += trx_snprintf(buffer + offset, size - offset, " ");

		trx_vector_str_sort(packages, TRX_DEFAULT_STR_COMPARE_FUNC);

		for (i = 0; i < packages->values_num; i++)
			offset += trx_snprintf(buffer + offset, size - offset, "%s, ", packages->values[i]);

		offset -= 2;
	}

	buffer[offset] = '\0';

	return offset;
}

static TRX_PACKAGE_MANAGER	package_managers[] =
/*	NAME		TEST_CMD					LIST_CMD			PARSER */
{
	{"dpkg",	"dpkg --version 2> /dev/null",			"dpkg --get-selections",	dpkg_parser},
	{"pkgtools",	"[ -d /var/log/packages ] && echo true",	"ls /var/log/packages",		NULL},
	{"rpm",		"rpm --version 2> /dev/null",			"rpm -qa",			NULL},
	{"pacman",	"pacman --version 2> /dev/null",		"pacman -Q",			NULL},
	{NULL}
};

int	SYSTEM_SW_PACKAGES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	size_t			offset = 0;
	int			ret = SYSINFO_RET_FAIL, show_pm, i, check_regex, check_manager;
	char			buffer[MAX_BUFFER_LEN], *regex, *manager, *mode, tmp[MAX_STRING_LEN], *buf = NULL,
				*package;
	trx_vector_str_t	packages;
	TRX_PACKAGE_MANAGER	*mng;

	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return ret;
	}

	regex = get_rparam(request, 0);
	manager = get_rparam(request, 1);
	mode = get_rparam(request, 2);

	check_regex = (NULL != regex && '\0' != *regex && 0 != strcmp(regex, "all"));
	check_manager = (NULL != manager && '\0' != *manager && 0 != strcmp(manager, "all"));

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "full"))
		show_pm = 1;	/* show package managers' names */
	else if (0 == strcmp(mode, "short"))
		show_pm = 0;
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
		return ret;
	}

	*buffer = '\0';
	trx_vector_str_create(&packages);

	for (i = 0; NULL != package_managers[i].name; i++)
	{
		mng = &package_managers[i];

		if (1 == check_manager && 0 != strcmp(manager, mng->name))
			continue;

		if (SUCCEED == trx_execute(mng->test_cmd, &buf, tmp, sizeof(tmp), CONFIG_TIMEOUT,
				TRX_EXIT_CODE_CHECKS_DISABLED) &&
				'\0' != *buf)	/* consider PMS present, if test_cmd outputs anything to stdout */
		{
			if (SUCCEED != trx_execute(mng->list_cmd, &buf, tmp, sizeof(tmp), CONFIG_TIMEOUT,
					TRX_EXIT_CODE_CHECKS_DISABLED))
			{
				continue;
			}

			ret = SYSINFO_RET_OK;

			package = strtok(buf, "\n");

			while (NULL != package)
			{
				if (NULL != mng->parser)	/* check if the package name needs to be parsed */
				{
					if (SUCCEED == mng->parser(package, tmp, sizeof(tmp)))
						package = tmp;
					else
						goto next;
				}

				if (1 == check_regex && NULL == trx_regexp_match(package, regex, NULL))
					goto next;

				trx_vector_str_append(&packages, trx_strdup(NULL, package));
next:
				package = strtok(NULL, "\n");
			}

			if (1 == show_pm)
			{
				offset += print_packages(buffer + offset, sizeof(buffer) - offset, &packages, mng->name);
				offset += trx_snprintf(buffer + offset, sizeof(buffer) - offset, "\n");

				trx_vector_str_clear_ext(&packages, trx_str_free);
			}
		}
	}

	trx_free(buf);

	if (0 == show_pm)
	{
		print_packages(buffer + offset, sizeof(buffer) - offset, &packages, NULL);

		trx_vector_str_clear_ext(&packages, trx_str_free);
	}
	else if (0 != offset)
		buffer[--offset] = '\0';

	trx_vector_str_destroy(&packages);

	if (SYSINFO_RET_OK == ret)
		SET_TEXT_RESULT(result, trx_strdup(NULL, buffer));
	else
		SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot obtain package information."));

	return ret;
}
