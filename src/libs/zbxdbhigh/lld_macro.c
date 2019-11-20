

#include "common.h"

#include "dbcache.h"
#include "log.h"

/******************************************************************************
 *                                                                            *
 * Function: lld_macro_paths_compare                                          *
 *                                                                            *
 * Purpose: sorting function to sort LLD macros by unique name                *
 *                                                                            *
 ******************************************************************************/
int	trx_lld_macro_paths_compare(const void *d1, const void *d2)
{
	const trx_lld_macro_path_t	*r1 = *(const trx_lld_macro_path_t **)d1;
	const trx_lld_macro_path_t	*r2 = *(const trx_lld_macro_path_t **)d2;

	return strcmp(r1->lld_macro, r2->lld_macro);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_macro_paths_get                                              *
 *                                                                            *
 * Purpose: retrieve list of LLD macros                                       *
 *                                                                            *
 * Parameters: lld_ruleid      - [IN] LLD id                                  *
 *             lld_macro_paths - [OUT] use json path to extract from jp_row   *
 *             error           - [OUT] in case json path is invalid           *
 *                                                                            *
 ******************************************************************************/
int	trx_lld_macro_paths_get(trx_uint64_t lld_ruleid, trx_vector_ptr_t *lld_macro_paths, char **error)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_lld_macro_path_t	*lld_macro_path;
	int			ret = SUCCEED;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select lld_macro,path"
			" from lld_macro_path"
			" where itemid=" TRX_FS_UI64
			" order by lld_macro",
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		trx_jsonpath_t	path;

		if (SUCCEED != (ret = trx_jsonpath_compile(row[1], &path)))
		{
			*error = trx_dsprintf(*error, "Cannot process LLD macro \"%s\": %s.\n", row[0],
					trx_json_strerror());
			break;
		}

		trx_jsonpath_clear(&path);

		lld_macro_path = (trx_lld_macro_path_t *)trx_malloc(NULL, sizeof(trx_lld_macro_path_t));
		lld_macro_path->lld_macro = trx_strdup(NULL, row[0]);
		lld_macro_path->path = trx_strdup(NULL, row[1]);

		trx_vector_ptr_append(lld_macro_paths, lld_macro_path);
	}
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_macro_path_free                                              *
 *                                                                            *
 * Purpose: release resources allocated by lld macro path                     *
 *                                                                            *
 * Parameters: lld_macro_path - [IN] json path to extract from lld_row        *
 *                                                                            *
 ******************************************************************************/
void	trx_lld_macro_path_free(trx_lld_macro_path_t *lld_macro_path)
{
	trx_free(lld_macro_path->path);
	trx_free(lld_macro_path->lld_macro);
	trx_free(lld_macro_path);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_lld_macro_value_by_name                                      *
 *                                                                            *
 * Purpose: get value of LLD macro using json path if available or by         *
 *          searching for such key in key value pairs of array entry          *
 *                                                                            *
 * Parameters: jp_row          - [IN] the lld data row                        *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *             macro           - [IN] LLD macro                               *
 *             value           - [OUT] value extracted from jp_row            *
 *                                                                            *
 ******************************************************************************/
int	trx_lld_macro_value_by_name(const struct trx_json_parse *jp_row, const trx_vector_ptr_t *lld_macro_paths,
		const char *macro, char **value)
{
	trx_lld_macro_path_t	lld_macro_path_local, *lld_macro_path;
	int			index;
	size_t			value_alloc = 0;

	lld_macro_path_local.lld_macro = (char *)macro;

	if (FAIL != (index = trx_vector_ptr_bsearch(lld_macro_paths, &lld_macro_path_local,
			trx_lld_macro_paths_compare)))
	{
		lld_macro_path = (trx_lld_macro_path_t *)lld_macro_paths->values[index];

		if (SUCCEED == trx_jsonpath_query(jp_row, lld_macro_path->path, value) && NULL != *value)
			return SUCCEED;

		return FAIL;
	}
	else
		return trx_json_value_by_name_dyn(jp_row, macro, value, &value_alloc);
}

