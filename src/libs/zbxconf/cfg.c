

#include "common.h"
#include "cfg.h"
#include "log.h"
#include "comms.h"

extern unsigned char	program_type;

char	*CONFIG_FILE		= NULL;

char	*CONFIG_LOG_TYPE_STR	= NULL;
int	CONFIG_LOG_TYPE		= LOG_TYPE_UNDEFINED;
char	*CONFIG_LOG_FILE	= NULL;
int	CONFIG_LOG_FILE_SIZE	= 1;
int	CONFIG_ALLOW_ROOT	= 0;
int	CONFIG_TIMEOUT		= 3;

static int	__parse_cfg_file(const char *cfg_file, struct cfg_line *cfg, int level, int optional, int strict);

/******************************************************************************
 *                                                                            *
 * Function: match_glob                                                       *
 *                                                                            *
 * Purpose: see whether a file (e.g., "parameter.conf")                       *
 *          matches a pattern (e.g., "p*.conf")                               *
 *                                                                            *
 * Return value: SUCCEED - file matches a pattern                             *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
static int	match_glob(const char *file, const char *pattern)
{
	const char	*f, *g, *p, *q;

	f = file;
	p = pattern;

	while (1)
	{
		/* corner case */

		if ('\0' == *p)
			return '\0' == *f ? SUCCEED : FAIL;

		/* find a set of literal characters */

		while ('*' == *p)
			p++;

		for (q = p; '\0' != *q && '*' != *q; q++)
			;

		/* if literal characters are at the beginning... */

		if (pattern == p)
		{
#ifdef _WINDOWS
			if (0 != trx_strncasecmp(f, p, q - p))
#else
			if (0 != strncmp(f, p, q - p))
#endif
				return FAIL;

			f += q - p;
			p = q;

			continue;
		}

		/* if literal characters are at the end... */

		if ('\0' == *q)
		{
			for (g = f; '\0' != *g; g++)
				;

			if (g - f < q - p)
				return FAIL;
#ifdef _WINDOWS
			return 0 == strcasecmp(g - (q - p), p) ? SUCCEED : FAIL;
#else
			return 0 == strcmp(g - (q - p), p) ? SUCCEED : FAIL;
#endif
		}

		/* if literal characters are in the middle... */

		while (1)
		{
			if ('\0' == *f)
				return FAIL;
#ifdef _WINDOWS
			if (0 == trx_strncasecmp(f, p, q - p))
#else
			if (0 == strncmp(f, p, q - p))
#endif
			{
				f += q - p;
				p = q;

				break;
			}

			f++;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: parse_glob                                                       *
 *                                                                            *
 * Purpose: parse a glob like "/usr/local/etc/treegix_agentd.conf.d/p*.conf"   *
 *          into "/usr/local/etc/treegix_agentd.conf.d" and "p*.conf" parts    *
 *                                                                            *
 * Parameters: glob    - [IN] glob as specified in Include directive          *
 *             path    - [OUT] parsed path, either directory or file          *
 *             pattern - [OUT] parsed pattern, if path is directory           *
 *                                                                            *
 * Return value: SUCCEED - glob is valid and was parsed successfully          *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
static int	parse_glob(const char *glob, char **path, char **pattern)
{
	const char	*p;

	if (NULL == (p = strchr(glob, '*')))
	{
		*path = trx_strdup(NULL, glob);
		*pattern = NULL;

		goto trim;
	}

	if (NULL != strchr(p + 1, PATH_SEPARATOR))
	{
		trx_error("%s: glob pattern should be the last component of the path", glob);
		return FAIL;
	}

	do
	{
		if (glob == p)
		{
			trx_error("%s: path should be absolute", glob);
			return FAIL;
		}

		p--;
	}
	while (PATH_SEPARATOR != *p);

	*path = trx_strdup(NULL, glob);
	(*path)[p - glob] = '\0';

	*pattern = trx_strdup(NULL, p + 1);
trim:
#ifdef _WINDOWS
	if (0 != trx_rtrim(*path, "\\") && NULL == *pattern)
		*pattern = trx_strdup(NULL, "*");			/* make sure path is a directory */

	if (':' == (*path)[1] && '\0' == (*path)[2] && '\\' == glob[2])	/* retain backslash for "C:\" */
	{
		(*path)[2] = '\\';
		(*path)[3] = '\0';
	}
#else
	if (0 != trx_rtrim(*path, "/") && NULL == *pattern)
		*pattern = trx_strdup(NULL, "*");			/* make sure path is a directory */

	if ('\0' == (*path)[0] && '/' == glob[0])			/* retain forward slash for "/" */
	{
		(*path)[0] = '/';
		(*path)[1] = '\0';
	}
#endif
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: parse_cfg_dir                                                    *
 *                                                                            *
 * Purpose: parse directory with configuration files                          *
 *                                                                            *
 * Parameters: path    - full path to directory                               *
 *             pattern - pattern that files in the directory should match     *
 *             cfg     - pointer to configuration parameter structure         *
 *             level   - a level of included file                             *
 *             strict  - treat unknown parameters as error                    *
 *                                                                            *
 * Return value: SUCCEED - parsed successfully                                *
 *               FAIL - error processing directory                            *
 *                                                                            *
 ******************************************************************************/
#ifdef _WINDOWS
static int	parse_cfg_dir(const char *path, const char *pattern, struct cfg_line *cfg, int level, int strict)
{
	WIN32_FIND_DATAW	find_file_data;
	HANDLE			h_find;
	char 			*find_path = NULL, *file = NULL, *file_name;
	wchar_t			*wfind_path = NULL;
	int			ret = FAIL;

	find_path = trx_dsprintf(find_path, "%s\\*", path);
	wfind_path = trx_utf8_to_unicode(find_path);

	if (INVALID_HANDLE_VALUE == (h_find = FindFirstFileW(wfind_path, &find_file_data)))
		goto clean;

	while (0 != FindNextFileW(h_find, &find_file_data))
	{
		if (0 != (find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;

		file_name = trx_unicode_to_utf8(find_file_data.cFileName);

		if (NULL != pattern && SUCCEED != match_glob(file_name, pattern))
		{
			trx_free(file_name);
			continue;
		}

		file = trx_dsprintf(file, "%s\\%s", path, file_name);

		trx_free(file_name);

		if (SUCCEED != __parse_cfg_file(file, cfg, level, TRX_CFG_FILE_REQUIRED, strict))
			goto close;
	}

	ret = SUCCEED;
close:
	trx_free(file);
	FindClose(h_find);
clean:
	trx_free(wfind_path);
	trx_free(find_path);

	return ret;
}
#else
static int	parse_cfg_dir(const char *path, const char *pattern, struct cfg_line *cfg, int level, int strict)
{
	DIR		*dir;
	struct dirent	*d;
	trx_stat_t	sb;
	char		*file = NULL;
	int		ret = FAIL;

	if (NULL == (dir = opendir(path)))
	{
		trx_error("%s: %s", path, trx_strerror(errno));
		goto out;
	}

	while (NULL != (d = readdir(dir)))
	{
		file = trx_dsprintf(file, "%s/%s", path, d->d_name);

		if (0 != trx_stat(file, &sb) || 0 == S_ISREG(sb.st_mode))
			continue;

		if (NULL != pattern && SUCCEED != match_glob(d->d_name, pattern))
			continue;

		if (SUCCEED != __parse_cfg_file(file, cfg, level, TRX_CFG_FILE_REQUIRED, strict))
			goto close;
	}

	ret = SUCCEED;
close:
	if (0 != closedir(dir))
	{
		trx_error("%s: %s", path, trx_strerror(errno));
		ret = FAIL;
	}

	trx_free(file);
out:
	return ret;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: parse_cfg_object                                                 *
 *                                                                            *
 * Purpose: parse "Include=..." line in configuration file                    *
 *                                                                            *
 * Parameters: cfg_file - full name of config file                            *
 *             cfg      - pointer to configuration parameter structure        *
 *             level    - a level of included file                            *
 *             strict   - treat unknown parameters as error                   *
 *                                                                            *
 * Return value: SUCCEED - parsed successfully                                *
 *               FAIL - error processing object                               *
 *                                                                            *
 ******************************************************************************/
static int	parse_cfg_object(const char *cfg_file, struct cfg_line *cfg, int level, int strict)
{
	int		ret = FAIL;
	char		*path = NULL, *pattern = NULL;
	trx_stat_t	sb;

	if (SUCCEED != parse_glob(cfg_file, &path, &pattern))
		goto clean;

	if (0 != trx_stat(path, &sb))
	{
		trx_error("%s: %s", path, trx_strerror(errno));
		goto clean;
	}

	if (0 == S_ISDIR(sb.st_mode))
	{
		if (NULL == pattern)
		{
			ret = __parse_cfg_file(path, cfg, level, TRX_CFG_FILE_REQUIRED, strict);
			goto clean;
		}

		trx_error("%s: base path is not a directory", cfg_file);
		goto clean;
	}

	ret = parse_cfg_dir(path, pattern, cfg, level, strict);
clean:
	trx_free(pattern);
	trx_free(path);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: parse_cfg_file                                                   *
 *                                                                            *
 * Purpose: parse configuration file                                          *
 *                                                                            *
 * Parameters: cfg_file - full name of config file                            *
 *             cfg      - pointer to configuration parameter structure        *
 *             level    - a level of included file                            *
 *             optional - do not treat missing configuration file as error    *
 *             strict   - treat unknown parameters as error                   *
 *                                                                            *
 * Return value: SUCCEED - parsed successfully                                *
 *               FAIL - error processing config file                          *
 *                                                                            *
 * Author: Alexei Vladishev, Eugene Grigorjev                                 *
 *                                                                            *
 ******************************************************************************/
static int	__parse_cfg_file(const char *cfg_file, struct cfg_line *cfg, int level, int optional, int strict)
{
#define TRX_MAX_INCLUDE_LEVEL	10

#define TRX_CFG_LTRIM_CHARS	"\t "
#define TRX_CFG_RTRIM_CHARS	TRX_CFG_LTRIM_CHARS "\r\n"

	FILE		*file;
	int		i, lineno, param_valid;
	char		line[MAX_STRING_LEN + 3], *parameter, *value;
	trx_uint64_t	var;
	size_t		len;
#ifdef _WINDOWS
	wchar_t		*wcfg_file;
#endif
	if (++level > TRX_MAX_INCLUDE_LEVEL)
	{
		trx_error("Recursion detected! Skipped processing of '%s'.", cfg_file);
		return FAIL;
	}

	if (NULL != cfg_file)
	{
#ifdef _WINDOWS
		wcfg_file = trx_utf8_to_unicode(cfg_file);
		file = _wfopen(wcfg_file, L"r");
		trx_free(wcfg_file);

		if (NULL == file)
			goto cannot_open;
#else
		if (NULL == (file = fopen(cfg_file, "r")))
			goto cannot_open;
#endif
		for (lineno = 1; NULL != fgets(line, sizeof(line), file); lineno++)
		{
			/* check if line length exceeds limit (max. 2048 bytes) */
			len = strlen(line);
			if (MAX_STRING_LEN < len && NULL == strchr("\r\n", line[MAX_STRING_LEN]))
				goto line_too_long;

			trx_ltrim(line, TRX_CFG_LTRIM_CHARS);
			trx_rtrim(line, TRX_CFG_RTRIM_CHARS);

			if ('#' == *line || '\0' == *line)
				continue;

			/* we only support UTF-8 characters in the config file */
			if (SUCCEED != trx_is_utf8(line))
				goto non_utf8;

			parameter = line;
			if (NULL == (value = strchr(line, '=')))
				goto non_key_value;

			*value++ = '\0';

			trx_rtrim(parameter, TRX_CFG_RTRIM_CHARS);
			trx_ltrim(value, TRX_CFG_LTRIM_CHARS);

			treegix_log(LOG_LEVEL_DEBUG, "cfg: para: [%s] val [%s]", parameter, value);

			if (0 == strcmp(parameter, "Include"))
			{
				if (FAIL == parse_cfg_object(value, cfg, level, strict))
				{
					fclose(file);
					goto error;
				}

				continue;
			}

			param_valid = 0;

			for (i = 0; NULL != cfg[i].parameter; i++)
			{
				if (0 != strcmp(cfg[i].parameter, parameter))
					continue;

				param_valid = 1;

				treegix_log(LOG_LEVEL_DEBUG, "accepted configuration parameter: '%s' = '%s'",
						parameter, value);

				switch (cfg[i].type)
				{
					case TYPE_INT:
						if (FAIL == str2uint64(value, "KMGT", &var))
							goto incorrect_config;

						if (cfg[i].min > var || (0 != cfg[i].max && var > cfg[i].max))
							goto incorrect_config;

						*((int *)cfg[i].variable) = (int)var;
						break;
					case TYPE_STRING_LIST:
						trx_trim_str_list(value, ',');
						TRX_FALLTHROUGH;
					case TYPE_STRING:
						*((char **)cfg[i].variable) =
								trx_strdup(*((char **)cfg[i].variable), value);
						break;
					case TYPE_MULTISTRING:
						trx_strarr_add((char ***)cfg[i].variable, value);
						break;
					case TYPE_UINT64:
						if (FAIL == str2uint64(value, "KMGT", &var))
							goto incorrect_config;

						if (cfg[i].min > var || (0 != cfg[i].max && var > cfg[i].max))
							goto incorrect_config;

						*((trx_uint64_t *)cfg[i].variable) = var;
						break;
					default:
						assert(0);
				}
			}

			if (0 == param_valid && TRX_CFG_STRICT == strict)
				goto unknown_parameter;
		}
		fclose(file);
	}

	if (1 != level)	/* skip mandatory parameters check for included files */
		return SUCCEED;

	for (i = 0; NULL != cfg[i].parameter; i++) /* check for mandatory parameters */
	{
		if (PARM_MAND != cfg[i].mandatory)
			continue;

		switch (cfg[i].type)
		{
			case TYPE_INT:
				if (0 == *((int *)cfg[i].variable))
					goto missing_mandatory;
				break;
			case TYPE_STRING:
			case TYPE_STRING_LIST:
				if (NULL == (*(char **)cfg[i].variable))
					goto missing_mandatory;
				break;
			default:
				assert(0);
		}
	}

	return SUCCEED;
cannot_open:
	if (TRX_CFG_FILE_REQUIRED != optional)
		return SUCCEED;
	trx_error("cannot open config file \"%s\": %s", cfg_file, trx_strerror(errno));
	goto error;
line_too_long:
	fclose(file);
	trx_error("line %d exceeds %d byte length limit in config file \"%s\"", lineno, MAX_STRING_LEN, cfg_file);
	goto error;
non_utf8:
	fclose(file);
	trx_error("non-UTF-8 character at line %d \"%s\" in config file \"%s\"", lineno, line, cfg_file);
	goto error;
non_key_value:
	fclose(file);
	trx_error("invalid entry \"%s\" (not following \"parameter=value\" notation) in config file \"%s\", line %d",
			line, cfg_file, lineno);
	goto error;
incorrect_config:
	fclose(file);
	trx_error("wrong value of \"%s\" in config file \"%s\", line %d", cfg[i].parameter, cfg_file, lineno);
	goto error;
unknown_parameter:
	fclose(file);
	trx_error("unknown parameter \"%s\" in config file \"%s\", line %d", parameter, cfg_file, lineno);
	goto error;
missing_mandatory:
	trx_error("missing mandatory parameter \"%s\" in config file \"%s\"", cfg[i].parameter, cfg_file);
error:
	exit(EXIT_FAILURE);
}

int	parse_cfg_file(const char *cfg_file, struct cfg_line *cfg, int optional, int strict)
{
	return __parse_cfg_file(cfg_file, cfg, 0, optional, strict);
}

int	check_cfg_feature_int(const char *parameter, int value, const char *feature)
{
	if (0 != value)
	{
		trx_error("\"%s\" configuration parameter cannot be used: Treegix %s was compiled without %s",
				parameter, get_program_type_string(program_type), feature);
		return FAIL;
	}

	return SUCCEED;
}

int	check_cfg_feature_str(const char *parameter, const char *value, const char *feature)
{
	if (NULL != value)
	{
		trx_error("\"%s\" configuration parameter cannot be used: Treegix %s was compiled without %s",
				parameter, get_program_type_string(program_type), feature);
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_set_data_destination_hosts                                   *
 *                                                                            *
 * Purpose: parse "ServerActive' parameter value and set destination servers  *
 *          using a callback function                                         *
 *                                                                            *
 ******************************************************************************/
void	trx_set_data_destination_hosts(char *active_hosts, add_serveractive_host_f cb)
{
	char	*l = active_hosts, *r;

	do
	{
		char		*host = NULL;
		unsigned short	port;

		if (NULL != (r = strchr(l, ',')))
			*r = '\0';

		if (SUCCEED != parse_serveractive_element(l, &host, &port, (unsigned short)TRX_DEFAULT_SERVER_PORT))
		{
			trx_error("error parsing the \"ServerActive\" parameter: address \"%s\" is invalid", l);
			exit(EXIT_FAILURE);
		}

		if (SUCCEED != cb(host, port))
		{
			trx_error("error parsing the \"ServerActive\" parameter: address \"%s\" specified more than"
					" once", l);
			trx_free(host);
			exit(EXIT_FAILURE);
		}

		trx_free(host);

		if (NULL != r)
		{
			*r = ',';
			l = r + 1;
		}
	}
	while (NULL != r);
}
