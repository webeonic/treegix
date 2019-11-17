
#ifndef TREEGIX_ALIAS_H
#define TREEGIX_ALIAS_H

typedef struct zbx_alias
{
	struct zbx_alias	*next;
	char			*name;
	char			*value;
}
ALIAS;

void		test_aliases(void);
void		add_alias(const char *name, const char *value);
void		alias_list_free(void);
const char	*zbx_alias_get(const char *orig);

#endif	/* TREEGIX_ALIAS_H */
