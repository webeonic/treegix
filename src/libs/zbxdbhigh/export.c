

#include "common.h"
#include "log.h"
#include "export.h"

extern char		*CONFIG_EXPORT_DIR;
extern trx_uint64_t	CONFIG_EXPORT_FILE_SIZE;

static char	*history_file_name;
static FILE	*history_file;

static char	*trends_file_name;
static FILE	*trends_file;

static char	*problems_file_name;
static FILE	*problems_file;
static char	*export_dir;

#define TRX_EXPORT_WAIT_FAIL 10

int	trx_is_export_enabled(void)
{
	if (NULL == CONFIG_EXPORT_DIR)
		return FAIL;

	return SUCCEED;
}

int	trx_export_init(char **error)
{
	struct stat	fs;

	if (FAIL == trx_is_export_enabled())
		return SUCCEED;

	if (0 != stat(CONFIG_EXPORT_DIR, &fs))
	{
		*error = trx_dsprintf(*error, "Failed to stat the specified path \"%s\": %s.", CONFIG_EXPORT_DIR,
				trx_strerror(errno));
		return FAIL;
	}

	if (0 == S_ISDIR(fs.st_mode))
	{
		*error = trx_dsprintf(*error, "The specified path \"%s\" is not a directory.", CONFIG_EXPORT_DIR);
		return FAIL;
	}

	if (0 != access(CONFIG_EXPORT_DIR, W_OK | R_OK))
	{
		*error = trx_dsprintf(*error, "Cannot access path \"%s\": %s.", CONFIG_EXPORT_DIR, trx_strerror(errno));
		return FAIL;
	}

	export_dir = trx_strdup(NULL, CONFIG_EXPORT_DIR);

	if ('/' == export_dir[strlen(export_dir) - 1])
		export_dir[strlen(export_dir) - 1] = '\0';

	return SUCCEED;
}

void	trx_history_export_init(const char *process_name, int process_num)
{
	history_file_name = trx_dsprintf(NULL, "%s/history-%s-%d.ndjson", export_dir, process_name, process_num);

	if (NULL == (history_file = fopen(history_file_name, "a")))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot open export file '%s': %s", history_file_name,
				trx_strerror(errno));
		exit(EXIT_FAILURE);
	}

	trends_file_name = trx_dsprintf(NULL, "%s/trends-%s-%d.ndjson", export_dir, process_name, process_num);

	if (NULL == (trends_file = fopen(trends_file_name, "a")))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot open export file '%s': %s", trends_file_name,
				trx_strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void	trx_problems_export_init(const char *process_name, int process_num)
{
	problems_file_name = trx_dsprintf(NULL, "%s/problems-%s-%d.ndjson", export_dir, process_name, process_num);

	if (NULL == (problems_file = fopen(problems_file_name, "a")))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot open export file '%s': %s", problems_file_name,
				trx_strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static	void	file_write(const char *buf, size_t count, FILE **file, const char *name)
{
	size_t	ret;

	if (CONFIG_EXPORT_FILE_SIZE <= count + ftell(*file) + 1)
	{
		char	filename_old[MAX_STRING_LEN];

		strscpy(filename_old, name);
		trx_strlcat(filename_old, ".old", MAX_STRING_LEN);
		remove(filename_old);
		trx_fclose(*file);

		while (0 != rename(name, filename_old))
		{
			treegix_log(LOG_LEVEL_ERR, "cannot rename export file '%s': %s: retrying in %d seconds",
					name, trx_strerror(errno), TRX_EXPORT_WAIT_FAIL);
			sleep(TRX_EXPORT_WAIT_FAIL);
		}

		while (NULL == (*file = fopen(name, "a")))
		{
			treegix_log(LOG_LEVEL_ERR, "cannot open export file '%s': %s: retrying in %d seconds",
					name, trx_strerror(errno), TRX_EXPORT_WAIT_FAIL);
			sleep(TRX_EXPORT_WAIT_FAIL);
		}
	}

	while (0 < count)
	{
		if (count != (ret = (fwrite(buf, 1, count, *file))))
		{
			treegix_log(LOG_LEVEL_ERR, "cannot write to export file '%s': %s: retrying in %d seconds",
					name, trx_strerror(errno), TRX_EXPORT_WAIT_FAIL);
			sleep(TRX_EXPORT_WAIT_FAIL);
		}

		buf += ret;
		count -= ret;
	}

	while ('\n' != fputc('\n', *file))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot write to export file '%s': %s: retrying in %d seconds",
				name, trx_strerror(errno), TRX_EXPORT_WAIT_FAIL);
		sleep(TRX_EXPORT_WAIT_FAIL);
	}
}

void	trx_problems_export_write(const char *buf, size_t count)
{
	file_write(buf, count, &problems_file, problems_file_name);
}

void	trx_history_export_write(const char *buf, size_t count)
{
	file_write(buf, count, &history_file, history_file_name);
}

void	trx_trends_export_write(const char *buf, size_t count)
{
	file_write(buf, count, &trends_file, trends_file_name);
}

void	trx_problems_export_flush(void)
{
	if (0 != fflush(problems_file))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot flush export file '%s': %s", problems_file_name,
				trx_strerror(errno));
	}
}

void	trx_history_export_flush(void)
{
	if (0 != fflush(history_file))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot flush export file '%s': %s", history_file_name,
				trx_strerror(errno));
	}
}

void	trx_trends_export_flush(void)
{
	if (0 != fflush(trends_file))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot flush export file '%s': %s", trends_file_name,
				trx_strerror(errno));
	}
}
