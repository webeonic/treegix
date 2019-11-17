

#include "common.h"

/******************************************************************************
 *                                                                            *
 * Function: zbx_variant_to_value_type                                        *
 *                                                                            *
 * Purpose: converts variant value to type compatible with requested value    *
 *          type                                                              *
 *                                                                            *
 * Parameters: value      - [IN/OUT] the value to convert                     *
 *             value_type - [IN] the target value type                        *
 *             errmsg     - [OUT] the error message                           *
 *                                                                            *
 * Return value: SUCCEED - Value conversion was successful.                   *
 *               FAIL    - Otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	zbx_variant_to_value_type(zbx_variant_t *value, unsigned char value_type, char **errmsg)
{
	int	ret;

	zbx_free(*errmsg);

	switch (value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			if (SUCCEED == (ret = zbx_variant_convert(value, TRX_VARIANT_DBL)))
			{
				if (FAIL == (ret = zbx_validate_value_dbl(value->data.dbl)))
				{
					*errmsg = zbx_dsprintf(NULL, "Value " TRX_FS_DBL " is too small or too large.",
							value->data.dbl);
				}
			}
			break;
		case ITEM_VALUE_TYPE_UINT64:
			ret = zbx_variant_convert(value, TRX_VARIANT_UI64);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
		case ITEM_VALUE_TYPE_LOG:
			ret = zbx_variant_convert(value, TRX_VARIANT_STR);
			break;
		default:
			*errmsg = zbx_dsprintf(NULL, "Unknown value type \"%d\"", value_type);
			THIS_SHOULD_NEVER_HAPPEN;
			ret = FAIL;
	}

	if (FAIL == ret && NULL == *errmsg)
	{
		*errmsg = zbx_dsprintf(NULL, "Value \"%s\" of type \"%s\" is not suitable for value type \"%s\"",
				zbx_variant_value_desc(value), zbx_variant_type_desc(value),
				zbx_item_value_type_string(value_type));
	}

	return ret;
}
