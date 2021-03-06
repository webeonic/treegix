

#include "common.h"
#include "db.h"

/******************************************************************************
 *                                                                            *
 * Function: trx_db_lock_maintenanceids                                       *
 *                                                                            *
 * Purpose: lock maintenances in database                                     *
 *                                                                            *
 * Parameters: maintenanceids - [IN/OUT] a vector of unique maintenance ids   *
 *                                 IN - the maintenances to lock              *
 *                                 OUT - the locked maintenance ids (sorted)  *
 *                                                                            *
 * Return value: SUCCEED - at least one maintenance was locked                *
 *               FAIL    - no maintenances were locked (all target            *
 *                         maintenances were removed by user and              *
 *                         configuration cache was not yet updated)           *
 *                                                                            *
 * Comments: This function locks maintenances in database to avoid foreign    *
 *           key errors when a maintenance is removed in the middle of        *
 *           processing.                                                      *
 *           The output vector might contain less values than input vector if *
 *           a maintenance was removed before lock attempt.                   *
 *                                                                            *
 ******************************************************************************/
int	trx_db_lock_maintenanceids(trx_vector_uint64_t *maintenanceids)
{
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;
	trx_uint64_t	maintenanceid;
	int		i;
	DB_RESULT	result;
	DB_ROW		row;

	trx_vector_uint64_sort(maintenanceids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select maintenanceid from maintenances where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "maintenanceid", maintenanceids->values,
			maintenanceids->values_num);
#if defined(HAVE_MYSQL)
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by maintenanceid lock in share mode");
#elif defined(HAVE_IBM_DB2)
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by maintenanceid with rs use and keep share locks");
#else
	/* Row level shared locks are not supported in Oracle. For PostgreSQL table level locks */
	/* are used because row level shared locks have reader preference, which could lead to  */
	/* theoretical situation when server blocks out frontend from maintenances updates.     */
	DBexecute("lock table maintenances in share mode");
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by maintenanceid");
#endif

	result = DBselect("%s", sql);
	trx_free(sql);

	for (i = 0; NULL != (row = DBfetch(result)); i++)
	{
		TRX_STR2UINT64(maintenanceid, row[0]);

		while (maintenanceid != maintenanceids->values[i])
			trx_vector_uint64_remove(maintenanceids, i);
	}
	DBfree_result(result);

	while (i != maintenanceids->values_num)
		trx_vector_uint64_remove_noorder(maintenanceids, i);

	return (0 != maintenanceids->values_num ? SUCCEED : FAIL);
}
