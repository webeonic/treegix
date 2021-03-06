

#include "lld.h"

/******************************************************************************
 *                                                                            *
 * Function: lld_field_str_rollback                                           *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
void	lld_field_str_rollback(char **field, char **field_orig, trx_uint64_t *flags, trx_uint64_t flag)
{
	if (0 == (*flags & flag))
		return;

	trx_free(*field);
	*field = *field_orig;
	*field_orig = NULL;
	*flags &= ~flag;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_field_uint64_rollback                                        *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
void	lld_field_uint64_rollback(trx_uint64_t *field, trx_uint64_t *field_orig, trx_uint64_t *flags, trx_uint64_t flag)
{
	if (0 == (*flags & flag))
		return;

	*field = *field_orig;
	*field_orig = 0;
	*flags &= ~flag;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_end_of_life                                                  *
 *                                                                            *
 * Purpose: calculate when to delete lost resources in an overflow-safe way   *
 *                                                                            *
 ******************************************************************************/
int	lld_end_of_life(int lastcheck, int lifetime)
{
	return TRX_JAN_2038 - lastcheck > lifetime ? lastcheck + lifetime : TRX_JAN_2038;
}
