

#include "common.h"
#include "alias.h"
#include "sysinfo.h"
#include "log.h"

static ALIAS	*aliasList = NULL;

void	test_aliases(void)
{
	ALIAS	*alias;

	for (alias = aliasList; NULL != alias; alias = alias->next)
		test_parameter(alias->name);
}

void	add_alias(const char *name, const char *value)
{
	ALIAS	*alias = NULL;

	for (alias = aliasList; ; alias = alias->next)
	{
		/* add new Alias */
		if (NULL == alias)
		{
			alias = (ALIAS *)trx_malloc(alias, sizeof(ALIAS));

			alias->name = strdup(name);
			alias->value = strdup(value);
			alias->next = aliasList;
			aliasList = alias;

			treegix_log(LOG_LEVEL_DEBUG, "Alias added: \"%s\" -> \"%s\"", name, value);
			break;
		}

		/* treat duplicate Alias as error */
		if (0 == strcmp(alias->name, name))
		{
			treegix_log(LOG_LEVEL_CRIT, "failed to add Alias \"%s\": duplicate name", name);
			exit(EXIT_FAILURE);
		}
	}
}

void	alias_list_free(void)
{
	ALIAS	*curr, *next;

	next = aliasList;

	while (NULL != next)
	{
		curr = next;
		next = curr->next;
		trx_free(curr->value);
		trx_free(curr->name);
		trx_free(curr);
	}

	aliasList = NULL;
}

const char	*trx_alias_get(const char *orig)
{
	ALIAS				*alias;
	size_t				len_name, len_value;
	static TRX_THREAD_LOCAL char	*buffer = NULL;
	static TRX_THREAD_LOCAL size_t	buffer_alloc = 0;
	size_t				buffer_offset = 0;
	const char			*p = orig;

	if (SUCCEED != parse_key(&p) || '\0' != *p)
		return orig;

	for (alias = aliasList; NULL != alias; alias = alias->next)
	{
		if (0 == strcmp(alias->name, orig))
			return alias->value;
	}

	for (alias = aliasList; NULL != alias; alias = alias->next)
	{
		len_name = strlen(alias->name);
		if (3 >= len_name || 0 != strcmp(alias->name + len_name - 3, "[*]"))
			continue;

		if (0 != strncmp(alias->name, orig, len_name - 2))
			continue;

		len_value = strlen(alias->value);
		if (3 >= len_value || 0 != strcmp(alias->value + len_value - 3, "[*]"))
			return alias->value;

		trx_strncpy_alloc(&buffer, &buffer_alloc, &buffer_offset, alias->value, len_value - 3);
		trx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset, orig + len_name - 3);
		return buffer;
	}

	return orig;
}
