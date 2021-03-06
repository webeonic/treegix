

#include "common.h"

#include "db.h"

/******************************************************************************
 *                                                                            *
 * Function: trx_db_save_item_changes                                         *
 *                                                                            *
 * Purpose: save item state, error, mtime, lastlogsize changes to             *
 *          database                                                          *
 *                                                                            *
 ******************************************************************************/
void	trx_db_save_item_changes(char **sql, size_t *sql_alloc, size_t *sql_offset, const trx_vector_ptr_t *item_diff)
{
	int			i;
	const trx_item_diff_t	*diff;
	char			*value_esc;

	for (i = 0; i < item_diff->values_num; i++)
	{
		char	delim = ' ';

		diff = (const trx_item_diff_t *)item_diff->values[i];

		if (0 == (TRX_FLAGS_ITEM_DIFF_UPDATE_DB & diff->flags))
			continue;

		trx_strcpy_alloc(sql, sql_alloc, sql_offset, "update item_rtdata set");

		if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_LASTLOGSIZE & diff->flags))
		{
			trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%clastlogsize=" TRX_FS_UI64, delim,
					diff->lastlogsize);
			delim = ',';
		}

		if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_MTIME & diff->flags))
		{
			trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%cmtime=%d", delim, diff->mtime);
			delim = ',';
		}

		if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_STATE & diff->flags))
		{
			trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%cstate=%d", delim, (int)diff->state);
			delim = ',';
		}

		if (0 != (TRX_FLAGS_ITEM_DIFF_UPDATE_ERROR & diff->flags))
		{
			value_esc = DBdyn_escape_field("item_rtdata", "error", diff->error);
			trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%cerror='%s'", delim, value_esc);
			trx_free(value_esc);
		}

		trx_snprintf_alloc(sql, sql_alloc, sql_offset, " where itemid=" TRX_FS_UI64 ";\n", diff->itemid);

		DBexecute_overflowed_sql(sql, sql_alloc, sql_offset);
	}
}
