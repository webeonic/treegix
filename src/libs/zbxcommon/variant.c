

#include "common.h"
#include "trxalgo.h"

void	*trx_variant_data_bin_copy(const void *bin)
{
	trx_uint32_t		size;
	void	*value_bin;

	memcpy(&size, bin, sizeof(size));
	value_bin = trx_malloc(NULL, size + sizeof(size));
	memcpy(value_bin, bin, size + sizeof(size));

	return value_bin;
}

void	*trx_variant_data_bin_create(const void *data, trx_uint32_t size)
{
	void	*value_bin;

	value_bin = trx_malloc(NULL, size + sizeof(size));
	memcpy(value_bin, &size, sizeof(size));
	memcpy((unsigned char *)value_bin + sizeof(size), data, size);

	return value_bin;
}

trx_uint32_t	trx_variant_data_bin_get(const void *bin, void **data)
{
	trx_uint32_t	size;

	memcpy(&size, bin, sizeof(trx_uint32_t));
	if (NULL != data)
		*data = ((unsigned char *)bin) + sizeof(size);
	return size;
}

void	trx_variant_clear(trx_variant_t *value)
{
	switch (value->type)
	{
		case TRX_VARIANT_STR:
			trx_free(value->data.str);
			break;
		case TRX_VARIANT_BIN:
			trx_free(value->data.bin);
			break;
	}

	value->type = TRX_VARIANT_NONE;
}

/******************************************************************************
 *                                                                            *
 * Setter functions assign passed data and set corresponding variant          *
 * type. Note that for complex data it means the pointer is simply copied     *
 * instead of making a copy of the specified data.                            *
 *                                                                            *
 * The contents of the destination value are not freed. When setting already  *
 * initialized variant it's safer to clear it beforehand, even if the variant *
 * contains primitive value (numeric).                                        *
 *                                                                            *
 ******************************************************************************/

void	trx_variant_set_str(trx_variant_t *value, char *text)
{
	value->data.str = text;
	value->type = TRX_VARIANT_STR;
}

void	trx_variant_set_dbl(trx_variant_t *value, double value_dbl)
{
	value->data.dbl = value_dbl;
	value->type = TRX_VARIANT_DBL;
}

void	trx_variant_set_ui64(trx_variant_t *value, trx_uint64_t value_ui64)
{
	value->data.ui64 = value_ui64;
	value->type = TRX_VARIANT_UI64;
}

void	trx_variant_set_none(trx_variant_t *value)
{
	value->type = TRX_VARIANT_NONE;
}

void	trx_variant_set_bin(trx_variant_t *value, void *value_bin)
{
	value->data.bin = value_bin;
	value->type = TRX_VARIANT_BIN;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_variant_copy                                                 *
 *                                                                            *
 * Purpose: copy variant contents from source to value                        *
 *                                                                            *
 * Comments: String and binary data are cloned, which is different from       *
 *           setters where only the pointers are copied.                      *
 *           The contents of the destination value are not freed. If copied   *
 *           over already initialized variant it's safer to clear it          *
 *           beforehand.                                                      *
 *                                                                            *
 ******************************************************************************/
void	trx_variant_copy(trx_variant_t *value, const trx_variant_t *source)
{
	switch (source->type)
	{
		case TRX_VARIANT_STR:
			trx_variant_set_str(value, trx_strdup(NULL, source->data.str));
			break;
		case TRX_VARIANT_UI64:
			trx_variant_set_ui64(value, source->data.ui64);
			break;
		case TRX_VARIANT_DBL:
			trx_variant_set_dbl(value, source->data.dbl);
			break;
		case TRX_VARIANT_BIN:
			trx_variant_set_bin(value, trx_variant_data_bin_copy(source->data.bin));
			break;
		case TRX_VARIANT_NONE:
			value->type = TRX_VARIANT_NONE;
			break;
	}
}

static int	variant_to_dbl(trx_variant_t *value)
{
	char	buffer[MAX_STRING_LEN];
	double	value_dbl;

	switch (value->type)
	{
		case TRX_VARIANT_DBL:
			return SUCCEED;
		case TRX_VARIANT_UI64:
			trx_variant_set_dbl(value, (double)value->data.ui64);
			return SUCCEED;
		case TRX_VARIANT_STR:
			trx_strlcpy(buffer, value->data.str, sizeof(buffer));
			break;
		default:
			return FAIL;
	}

	trx_rtrim(buffer, "\n\r"); /* trim newline for historical reasons / backwards compatibility */
	trx_trim_float(buffer);

	if (SUCCEED != is_double(buffer))
		return FAIL;

	value_dbl = atof(buffer);

	trx_variant_clear(value);
	trx_variant_set_dbl(value, value_dbl);

	return SUCCEED;
}

static int	variant_to_ui64(trx_variant_t *value)
{
	trx_uint64_t	value_ui64;
	char		buffer[MAX_STRING_LEN];

	switch (value->type)
	{
		case TRX_VARIANT_UI64:
			return SUCCEED;
		case TRX_VARIANT_DBL:
			if (0 > value->data.dbl)
				return FAIL;

			trx_variant_set_ui64(value, value->data.dbl);
			return SUCCEED;
		case TRX_VARIANT_STR:
			trx_strlcpy(buffer, value->data.str, sizeof(buffer));
			break;
		default:
			return FAIL;
	}

	trx_rtrim(buffer, "\n\r"); /* trim newline for historical reasons / backwards compatibility */
	trx_trim_integer(buffer);
	del_zeros(buffer);

	if (SUCCEED != is_uint64(buffer, &value_ui64))
		return FAIL;

	trx_variant_clear(value);
	trx_variant_set_ui64(value, value_ui64);

	return SUCCEED;
}

static int	variant_to_str(trx_variant_t *value)
{
	char	*value_str;

	switch (value->type)
	{
		case TRX_VARIANT_STR:
			return SUCCEED;
		case TRX_VARIANT_DBL:
			value_str = trx_dsprintf(NULL, TRX_FS_DBL, value->data.dbl);
			del_zeros(value_str);
			break;
		case TRX_VARIANT_UI64:
			value_str = trx_dsprintf(NULL, TRX_FS_UI64, value->data.ui64);
			break;
		default:
			return FAIL;
	}

	trx_variant_clear(value);
	trx_variant_set_str(value, value_str);

	return SUCCEED;
}

int	trx_variant_convert(trx_variant_t *value, int type)
{
	switch(type)
	{
		case TRX_VARIANT_UI64:
			return variant_to_ui64(value);
		case TRX_VARIANT_DBL:
			return variant_to_dbl(value);
		case TRX_VARIANT_STR:
			return variant_to_str(value);
		case TRX_VARIANT_NONE:
			trx_variant_clear(value);
			return SUCCEED;
		default:
			return FAIL;
	}
}

int	trx_variant_set_numeric(trx_variant_t *value, const char *text)
{
	trx_uint64_t	value_ui64;
	char		buffer[MAX_STRING_LEN];

	trx_strlcpy(buffer, text, sizeof(buffer));

	trx_rtrim(buffer, "\n\r"); /* trim newline for historical reasons / backwards compatibility */
	trx_trim_integer(buffer);
	del_zeros(buffer);

	if ('+' == buffer[0])
	{
		/* trx_trim_integer() stripped one '+' sign, so there's more than one '+' sign in the 'text' argument */
		return FAIL;
	}

	if (SUCCEED == is_uint64(buffer, &value_ui64))
	{
		trx_variant_set_ui64(value, value_ui64);
		return SUCCEED;
	}

	if (SUCCEED == is_double(buffer))
	{
		trx_variant_set_dbl(value, atof(buffer));
		return SUCCEED;
	}

	return FAIL;
}

const char	*trx_variant_value_desc(const trx_variant_t *value)
{
	static TRX_THREAD_LOCAL char	buffer[TRX_MAX_UINT64_LEN + 1];
	trx_uint32_t			size, i, len;

	switch (value->type)
	{
		case TRX_VARIANT_DBL:
			trx_snprintf(buffer, sizeof(buffer), TRX_FS_DBL, value->data.dbl);
			del_zeros(buffer);
			return buffer;
		case TRX_VARIANT_UI64:
			trx_snprintf(buffer, sizeof(buffer), TRX_FS_UI64, value->data.ui64);
			return buffer;
		case TRX_VARIANT_STR:
			return value->data.str;
		case TRX_VARIANT_NONE:
			return "";
		case TRX_VARIANT_BIN:
			memcpy(&size, value->data.bin, sizeof(size));
			if (0 != (len = MIN(sizeof(buffer) / 3, size)))
			{
				const unsigned char	*ptr = (const unsigned char *)value->data.bin + sizeof(size);

				for (i = 0; i < len; i++)
					trx_snprintf(buffer + i * 3, sizeof(buffer) - i * 3, "%02x ", ptr[i]);

				buffer[i * 3 - 1] = '\0';
			}
			else
				buffer[0] = '\0';
			return buffer;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return TRX_UNKNOWN_STR;
	}
}

const char	*trx_get_variant_type_desc(unsigned char type)
{
	switch (type)
	{
		case TRX_VARIANT_DBL:
			return "double";
		case TRX_VARIANT_UI64:
			return "uint64";
		case TRX_VARIANT_STR:
			return "string";
		case TRX_VARIANT_NONE:
			return "none";
		case TRX_VARIANT_BIN:
			return "binary";
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return TRX_UNKNOWN_STR;
	}
}

const char	*trx_variant_type_desc(const trx_variant_t *value)
{
	return trx_get_variant_type_desc(value->type);
}

int	trx_validate_value_dbl(double value)
{
	/* field with precision 16, scale 4 [NUMERIC(16,4)] */
	const double	pg_min_numeric = -1e12;
	const double	pg_max_numeric = 1e12;

	if (value <= pg_min_numeric || value >= pg_max_numeric)
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: variant_compare_empty                                            *
 *                                                                            *
 * Purpose: compares two variant values when at least one is empty (having    *
 *          type of TRX_VARIANT_NONE)                                         *
 *                                                                            *
 ******************************************************************************/
static int	variant_compare_empty(const trx_variant_t *value1, const trx_variant_t *value2)
{
	if (TRX_VARIANT_NONE == value1->type)
	{
		if (TRX_VARIANT_NONE == value2->type)
			return 0;

		return -1;
	}

	return 1;
}

/******************************************************************************
 *                                                                            *
 * Function: variant_compare_bin                                              *
 *                                                                            *
 * Purpose: compare two variant values when at least one contains binary data *
 *                                                                            *
 ******************************************************************************/
static int	variant_compare_bin(const trx_variant_t *value1, const trx_variant_t *value2)
{
	if (TRX_VARIANT_BIN == value1->type)
	{
		trx_uint32_t	size1, size2;

		if (TRX_VARIANT_BIN != value2->type)
			return 1;

		memcpy(&size1, value1->data.bin, sizeof(size1));
		memcpy(&size2, value2->data.bin, sizeof(size2));
		TRX_RETURN_IF_NOT_EQUAL(size1, size2);
		return memcmp(value1->data.bin, value2->data.bin, size1 + sizeof(size1));
	}

	return -1;
}

/******************************************************************************
 *                                                                            *
 * Function: variant_compare_str                                              *
 *                                                                            *
 * Purpose: compare two variant values when at least one is string            *
 *                                                                            *
 ******************************************************************************/
static int	variant_compare_str(const trx_variant_t *value1, const trx_variant_t *value2)
{
	if (TRX_VARIANT_STR == value1->type)
		return strcmp(value1->data.str, trx_variant_value_desc(value2));

	return strcmp(trx_variant_value_desc(value1), value2->data.str);
}

/******************************************************************************
 *                                                                            *
 * Function: variant_compare_dbl                                              *
 *                                                                            *
 * Purpose: compare two variant values when at least one is double and the    *
 *          other is double, uint64 or a string representing a valid double   *
 *          value                                                             *
 *                                                                            *
 ******************************************************************************/
static int	variant_compare_dbl(const trx_variant_t *value1, const trx_variant_t *value2)
{
	double	value1_dbl, value2_dbl;

	switch (value1->type)
	{
		case TRX_VARIANT_DBL:
			value1_dbl = value1->data.dbl;
			break;
		case TRX_VARIANT_UI64:
			value1_dbl = value1->data.ui64;
			break;
		case TRX_VARIANT_STR:
			value1_dbl = atof(value1->data.str);
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
	}

	switch (value2->type)
	{
		case TRX_VARIANT_DBL:
			value2_dbl = value2->data.dbl;
			break;
		case TRX_VARIANT_UI64:
			value2_dbl = value2->data.ui64;
			break;
		case TRX_VARIANT_STR:
			value2_dbl = atof(value2->data.str);
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
	}

	if (SUCCEED == trx_double_compare(value1_dbl, value2_dbl))
		return 0;

	TRX_RETURN_IF_NOT_EQUAL(value1_dbl, value2_dbl);

	THIS_SHOULD_NEVER_HAPPEN;
	exit(EXIT_FAILURE);
}

/******************************************************************************
 *                                                                            *
 * Function: variant_compare_ui64                                             *
 *                                                                            *
 * Purpose: compare two variant values when both are uint64                   *
 *                                                                            *
 ******************************************************************************/
static int	variant_compare_ui64(const trx_variant_t *value1, const trx_variant_t *value2)
{
	TRX_RETURN_IF_NOT_EQUAL(value1->data.ui64, value2->data.ui64);
	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_variant_compare                                              *
 *                                                                            *
 * Purpose: compare two variant values                                        *
 *                                                                            *
 * Parameters: value1 - [IN] the first value                                  *
 *             value2 - [IN] the second value                                 *
 *                                                                            *
 * Return value: <0 - the first value is less than the second                 *
 *               >0 - the first value is greater than the second              *
 *               0  - the values are equal                                    *
 *                                                                            *
 * Comments: The following comparison logic is applied:                       *
 *           1) value of 'none' type is always less than other types, two     *
 *              'none' types are equal                                        *
 *           2) value of binary type is always greater than other types, two  *
 *              binary types are compared by length and then by contents      *
 *           3) if both values have uint64 types, they are compared as is     *
 *           4) if both values can be converted to floating point values the  *
 *              conversion is done and the result is compared                 *
 *           5) if any of value is of string type, the other is converted to  *
 *              string and both are compared                                  *
 *                                                                            *
 ******************************************************************************/
int	trx_variant_compare(const trx_variant_t *value1, const trx_variant_t *value2)
{
	if (TRX_VARIANT_NONE == value1->type || TRX_VARIANT_NONE == value2->type)
		return variant_compare_empty(value1, value2);

	if (TRX_VARIANT_BIN == value1->type || TRX_VARIANT_BIN == value2->type)
		return variant_compare_bin(value1, value2);

	if (TRX_VARIANT_UI64 == value1->type && TRX_VARIANT_UI64 == value2->type)
		return  variant_compare_ui64(value1, value2);

	if ((TRX_VARIANT_STR != value1->type || SUCCEED == is_double(value1->data.str)) &&
			(TRX_VARIANT_STR != value2->type || SUCCEED == is_double(value2->data.str)))
	{
		return variant_compare_dbl(value1, value2);
	}

	/* at this point at least one of the values is string data, other can be uint64, floating or string */
	return variant_compare_str(value1, value2);
}
