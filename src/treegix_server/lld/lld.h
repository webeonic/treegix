

#ifndef TREEGIX_LLD_H
#define TREEGIX_LLD_H

#include "common.h"
#include "trxjson.h"
#include "trxalgo.h"

typedef struct
{
	trx_uint64_t	parent_itemid;
	trx_uint64_t	itemid;		/* the item, created by the item prototype */
}
trx_lld_item_link_t;

typedef struct
{
	struct trx_json_parse	jp_row;
	trx_vector_ptr_t	item_links;	/* the list of item prototypes */
}
trx_lld_row_t;

void	lld_field_str_rollback(char **field, char **field_orig, trx_uint64_t *flags, trx_uint64_t flag);
void	lld_field_uint64_rollback(trx_uint64_t *field, trx_uint64_t *field_orig, trx_uint64_t *flags,
		trx_uint64_t flag);

int	lld_update_items(trx_uint64_t hostid, trx_uint64_t lld_ruleid, const trx_vector_ptr_t *lld_rows,
		const trx_vector_ptr_t *lld_macros, char **error, int lifetime, int lastcheck);

void	lld_item_links_sort(trx_vector_ptr_t *lld_rows);

int	lld_update_triggers(trx_uint64_t hostid, trx_uint64_t lld_ruleid, const trx_vector_ptr_t *lld_rows, const trx_vector_ptr_t *lld_macros, char **error);

int	lld_update_graphs(trx_uint64_t hostid, trx_uint64_t lld_ruleid, const trx_vector_ptr_t *lld_rows,
		const trx_vector_ptr_t *lld_macros, char **error);

void	lld_update_hosts(trx_uint64_t lld_ruleid, const trx_vector_ptr_t *lld_rows, const trx_vector_ptr_t *lld_macros,
		char **error, int lifetime, int lastcheck);

int	lld_end_of_life(int lastcheck, int lifetime);

#endif
