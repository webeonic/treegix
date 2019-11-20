
#ifndef TREEGIX_ALIAS_H
#define TREEGIX_ALIAS_H

typedef struct trx_alias
{
	struct trx_alias	*next;
	char			*name;
	char			*value;
}
ALIAS;

void		test_aliases(void);
void		add_alias(const char *name, const char *value);
void		alias_list_free(void);
const char	*trx_alias_get(const char *orig);

#endif	/* TREEGIX_ALIAS_H */
