

#ifndef TREEGIX_LLD_H
#define TREEGIX_LLD_H

#include "common.h"
#include "zbxjson.h"
#include "zbxalgo.h"

typedef struct
{
	zbx_uint64_t	parent_itemid;
	zbx_uint64_t	itemid;		/* the item, created by the item prototype */
}
zbx_lld_item_link_t;

typedef struct
{
	struct zbx_json_parse	jp_row;
	zbx_vector_ptr_t	item_links;	/* the list of item prototypes */
}
zbx_lld_row_t;

void	lld_field_str_rollback(char **field, char **field_orig, zbx_uint64_t *flags, zbx_uint64_t flag);
void	lld_field_uint64_rollback(zbx_uint64_t *field, zbx_uint64_t *field_orig, zbx_uint64_t *flags,
		zbx_uint64_t flag);

int	lld_update_items(zbx_uint64_t hostid, zbx_uint64_t lld_ruleid, const zbx_vector_ptr_t *lld_rows,
		const zbx_vector_ptr_t *lld_macros, char **error, int lifetime, int lastcheck);

void	lld_item_links_sort(zbx_vector_ptr_t *lld_rows);

int	lld_update_triggers(zbx_uint64_t hostid, zbx_uint64_t lld_ruleid, const zbx_vector_ptr_t *lld_rows, const zbx_vector_ptr_t *lld_macros, char **error);

int	lld_update_graphs(zbx_uint64_t hostid, zbx_uint64_t lld_ruleid, const zbx_vector_ptr_t *lld_rows,
		const zbx_vector_ptr_t *lld_macros, char **error);

void	lld_update_hosts(zbx_uint64_t lld_ruleid, const zbx_vector_ptr_t *lld_rows, const zbx_vector_ptr_t *lld_macros,
		char **error, int lifetime, int lastcheck);

int	lld_end_of_life(int lastcheck, int lifetime);

#endif
