

#include "common.h"
#include "trxalgo.h"

#include "log.h"

/******************************************************************************
 *                                                                            *
 *                     Module for evaluating expressions                      *
 *                  ---------------------------------------                   *
 *                                                                            *
 * Global variables are used for efficiency reasons so that arguments do not  *
 * have to be passed to each of evaluate_termX() functions. For this reason,  *
 * too, this module is isolated into a separate file.                         *
 *                                                                            *
 * The priority of supported operators is as follows:                         *
 *                                                                            *
 *   - (unary)   evaluate_term8()                                             *
 *   not         evaluate_term7()                                             *
 *   * /         evaluate_term6()                                             *
 *   + -         evaluate_term5()                                             *
 *   < <= >= >   evaluate_term4()                                             *
 *   = <>        evaluate_term3()                                             *
 *   and         evaluate_term2()                                             *
 *   or          evaluate_term1()                                             *
 *                                                                            *
 * Function evaluate_term9() is used for parsing tokens on the lowest level:  *
 * those can be suffixed numbers like "12.345K" or parenthesized expressions. *
 *                                                                            *
 ******************************************************************************/

#define TRX_INFINITY	(1.0 / 0.0)	/* "Positive infinity" value used as a fatal error code */
#define TRX_UNKNOWN	(-1.0 / 0.0)	/* "Negative infinity" value used as a code for "Unknown" */

static const char	*ptr;		/* character being looked at */
static int		level;		/* expression nesting level  */

static char		*buffer;	/* error message buffer      */
static size_t		max_buffer_len;	/* error message buffer size */

/******************************************************************************
 *                                                                            *
 * Purpose: check whether the character delimits a numeric token              *
 *                                                                            *
 ******************************************************************************/
static int	is_number_delimiter(char c)
{
	return 0 == isdigit(c) && '.' != c && 0 == isalpha(c) ? SUCCEED : FAIL;
}

/******************************************************************************
 *                                                                            *
 * Purpose: check whether the character delimits a symbolic operator token    *
 *                                                                            *
 ******************************************************************************/
static int	is_operator_delimiter(char c)
{
	return ' ' == c || '(' == c || '\r' == c || '\n' == c || '\t' == c || ')' == c || '\0' == c ? SUCCEED : FAIL;
}

/******************************************************************************
 *                                                                            *
 * Purpose: evaluate a suffixed number like "12.345K"                         *
 *                                                                            *
 ******************************************************************************/
static double	evaluate_number(int *unknown_idx)
{
	double		result;
	int		len;

	/* Is it a special token of unknown value (e.g. TRX_UNKNOWN0, TRX_UNKNOWN1) ? */
	if (0 == strncmp(TRX_UNKNOWN_STR, ptr, TRX_UNKNOWN_STR_LEN))
	{
		const char	*p0, *p1;

		p0 = ptr + TRX_UNKNOWN_STR_LEN;
		p1 = p0;

		/* extract the message number which follows after 'TRX_UNKNOWN' */
		while (0 != isdigit((unsigned char)*p1))
			p1++;

		if (p0 < p1 && SUCCEED == is_number_delimiter(*p1))
		{
			ptr = p1;

			/* return 'unknown' and corresponding message number about its origin */
			*unknown_idx = atoi(p0);
			return TRX_UNKNOWN;
		}

		ptr = p0;

		return TRX_INFINITY;
	}

	if (SUCCEED == trx_suffixed_number_parse(ptr, &len) && SUCCEED == is_number_delimiter(*(ptr + len)))
	{
		result = atof(ptr) * suffix2factor(*(ptr + len - 1));
		ptr += len;
	}
	else
		result = TRX_INFINITY;

	return result;
}

static double	evaluate_term1(int *unknown_idx);

/******************************************************************************
 *                                                                            *
 * Purpose: evaluate a suffixed number or a parenthesized expression          *
 *                                                                            *
 ******************************************************************************/
static double	evaluate_term9(int *unknown_idx)
{
	double	result;

	while (' ' == *ptr || '\r' == *ptr || '\n' == *ptr || '\t' == *ptr)
		ptr++;

	if ('\0' == *ptr)
	{
		trx_strlcpy(buffer, "Cannot evaluate expression: unexpected end of expression.", max_buffer_len);
		return TRX_INFINITY;
	}

	if ('(' == *ptr)
	{
		ptr++;

		if (TRX_INFINITY == (result = evaluate_term1(unknown_idx)))
			return TRX_INFINITY;

		/* if evaluate_term1() returns TRX_UNKNOWN then continue as with regular number */

		if (')' != *ptr)
		{
			trx_snprintf(buffer, max_buffer_len, "Cannot evaluate expression:"
					" expected closing parenthesis at \"%s\".", ptr);
			return TRX_INFINITY;
		}

		ptr++;
	}
	else
	{
		if (TRX_INFINITY == (result = evaluate_number(unknown_idx)))
		{
			trx_snprintf(buffer, max_buffer_len, "Cannot evaluate expression:"
					" expected numeric token at \"%s\".", ptr);
			return TRX_INFINITY;
		}
	}

	while ('\0' != *ptr && (' ' == *ptr || '\r' == *ptr || '\n' == *ptr || '\t' == *ptr))
		ptr++;

	return result;
}

/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "-" (unary)                                              *
 *                                                                            *
 * -0.0     -> -0.0                                                           *
 * -1.2     -> -1.2                                                           *
 * -Unknown ->  Unknown                                                       *
 *                                                                            *
 ******************************************************************************/
static double	evaluate_term8(int *unknown_idx)
{
	double	result;

	while (' ' == *ptr || '\r' == *ptr || '\n' == *ptr || '\t' == *ptr)
		ptr++;

	if ('-' == *ptr)
	{
		ptr++;

		if (TRX_UNKNOWN == (result = evaluate_term9(unknown_idx)) || TRX_INFINITY == result)
			return result;

		result = -result;
	}
	else
		result = evaluate_term9(unknown_idx);

	return result;
}

/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "not"                                                    *
 *                                                                            *
 * not 0.0     ->  1.0                                                        *
 * not 1.2     ->  0.0                                                        *
 * not Unknown ->  Unknown                                                    *
 *                                                                            *
 ******************************************************************************/
static double	evaluate_term7(int *unknown_idx)
{
	double	result;

	while (' ' == *ptr || '\r' == *ptr || '\n' == *ptr || '\t' == *ptr)
		ptr++;

	if ('n' == ptr[0] && 'o' == ptr[1] && 't' == ptr[2] && SUCCEED == is_operator_delimiter(ptr[3]))
	{
		ptr += 3;

		if (TRX_UNKNOWN == (result = evaluate_term8(unknown_idx)) || TRX_INFINITY == result)
			return result;

		result = (SUCCEED == trx_double_compare(result, 0.0) ? 1.0 : 0.0);
	}
	else
		result = evaluate_term8(unknown_idx);

	return result;
}

/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "*" and "/"                                              *
 *                                                                            *
 *     0.0 * Unknown  ->  Unknown (yes, not 0 as we don't want to lose        *
 *                        Unknown in arithmetic operations)                   *
 *     1.2 * Unknown  ->  Unknown                                             *
 *     0.0 / 1.2      ->  0.0                                                 *
 *     1.2 / 0.0      ->  error (TRX_INFINITY)                                *
 * Unknown / 0.0      ->  error (TRX_INFINITY)                                *
 * Unknown / 1.2      ->  Unknown                                             *
 * Unknown / Unknown  ->  Unknown                                             *
 *     0.0 / Unknown  ->  Unknown                                             *
 *     1.2 / Unknown  ->  Unknown                                             *
 *                                                                            *
 ******************************************************************************/
static double	evaluate_term6(int *unknown_idx)
{
	char	op;
	double	result, operand;
	int	res_idx = -1, oper_idx = -2;	/* set invalid values to catch errors */

	if (TRX_INFINITY == (result = evaluate_term7(&res_idx)))
		return TRX_INFINITY;

	if (TRX_UNKNOWN == result)
		*unknown_idx = res_idx;

	/* if evaluate_term7() returns TRX_UNKNOWN then continue as with regular number */

	while ('*' == *ptr || '/' == *ptr)
	{
		op = *ptr++;

		/* 'TRX_UNKNOWN' in multiplication and division produces 'TRX_UNKNOWN'. */
		/* Even if 1st operand is Unknown we evaluate 2nd operand too to catch fatal errors in it. */

		if (TRX_INFINITY == (operand = evaluate_term7(&oper_idx)))
			return TRX_INFINITY;

		if ('*' == op)
		{
			if (TRX_UNKNOWN == operand)		/* (anything) * Unknown */
			{
				*unknown_idx = oper_idx;
				res_idx = oper_idx;
				result = TRX_UNKNOWN;
			}
			else if (TRX_UNKNOWN == result)		/* Unknown * known */
			{
				*unknown_idx = res_idx;
			}
			else
				result *= operand;
		}
		else
		{
			/* catch division by 0 even if 1st operand is Unknown */

			if (TRX_UNKNOWN != operand && SUCCEED == trx_double_compare(operand, 0.0))
			{
				trx_strlcpy(buffer, "Cannot evaluate expression: division by zero.", max_buffer_len);
				return TRX_INFINITY;
			}

			if (TRX_UNKNOWN == operand)		/* (anything) / Unknown */
			{
				*unknown_idx = oper_idx;
				res_idx = oper_idx;
				result = TRX_UNKNOWN;
			}
			else if (TRX_UNKNOWN == result)		/* Unknown / known */
			{
				*unknown_idx = res_idx;
			}
			else
				result /= operand;
		}
	}

	return result;
}

/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "+" and "-"                                              *
 *                                                                            *
 *     0.0 +/- Unknown  ->  Unknown                                           *
 *     1.2 +/- Unknown  ->  Unknown                                           *
 * Unknown +/- Unknown  ->  Unknown                                           *
 *                                                                            *
 ******************************************************************************/
static double	evaluate_term5(int *unknown_idx)
{
	char	op;
	double	result, operand;
	int	res_idx = -3, oper_idx = -4;	/* set invalid values to catch errors */

	if (TRX_INFINITY == (result = evaluate_term6(&res_idx)))
		return TRX_INFINITY;

	if (TRX_UNKNOWN == result)
		*unknown_idx = res_idx;

	/* if evaluate_term6() returns TRX_UNKNOWN then continue as with regular number */

	while ('+' == *ptr || '-' == *ptr)
	{
		op = *ptr++;

		/* even if 1st operand is Unknown we evaluate 2nd operand to catch fatal error if any occurs */

		if (TRX_INFINITY == (operand = evaluate_term6(&oper_idx)))
			return TRX_INFINITY;

		if (TRX_UNKNOWN == operand)		/* (anything) +/- Unknown */
		{
			*unknown_idx = oper_idx;
			res_idx = oper_idx;
			result = TRX_UNKNOWN;
		}
		else if (TRX_UNKNOWN == result)		/* Unknown +/- known */
		{
			*unknown_idx = res_idx;
		}
		else
		{
			if ('+' == op)
				result += operand;
			else
				result -= operand;
		}
	}

	return result;
}

/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "<", "<=", ">=", ">"                                     *
 *                                                                            *
 *     0.0 < Unknown  ->  Unknown                                             *
 *     1.2 < Unknown  ->  Unknown                                             *
 * Unknown < Unknown  ->  Unknown                                             *
 *                                                                            *
 ******************************************************************************/
static double	evaluate_term4(int *unknown_idx)
{
	char	op;
	double	result, operand;
	int	res_idx = -5, oper_idx = -6;	/* set invalid values to catch errors */

	if (TRX_INFINITY == (result = evaluate_term5(&res_idx)))
		return TRX_INFINITY;

	if (TRX_UNKNOWN == result)
		*unknown_idx = res_idx;

	/* if evaluate_term5() returns TRX_UNKNOWN then continue as with regular number */

	while (1)
	{
		if ('<' == ptr[0] && '=' == ptr[1])
		{
			op = 'l';
			ptr += 2;
		}
		else if ('>' == ptr[0] && '=' == ptr[1])
		{
			op = 'g';
			ptr += 2;
		}
		else if (('<' == ptr[0] && '>' != ptr[1]) || '>' == ptr[0])
		{
			op = *ptr++;
		}
		else
			break;

		/* even if 1st operand is Unknown we evaluate 2nd operand to catch fatal error if any occurs */

		if (TRX_INFINITY == (operand = evaluate_term5(&oper_idx)))
			return TRX_INFINITY;

		if (TRX_UNKNOWN == operand)		/* (anything) < Unknown */
		{
			*unknown_idx = oper_idx;
			res_idx = oper_idx;
			result = TRX_UNKNOWN;
		}
		else if (TRX_UNKNOWN == result)		/* Unknown < known */
		{
			*unknown_idx = res_idx;
		}
		else
		{
			if ('<' == op)
				result = (result < operand - TRX_DOUBLE_EPSILON);
			else if ('l' == op)
				result = (result <= operand + TRX_DOUBLE_EPSILON);
			else if ('g' == op)
				result = (result >= operand - TRX_DOUBLE_EPSILON);
			else
				result = (result > operand + TRX_DOUBLE_EPSILON);
		}
	}

	return result;
}

/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "=" and "<>"                                             *
 *                                                                            *
 *      0.0 = Unknown  ->  Unknown                                            *
 *      1.2 = Unknown  ->  Unknown                                            *
 *  Unknown = Unknown  ->  Unknown                                            *
 *     0.0 <> Unknown  ->  Unknown                                            *
 *     1.2 <> Unknown  ->  Unknown                                            *
 * Unknown <> Unknown  ->  Unknown                                            *
 *                                                                            *
 ******************************************************************************/
static double	evaluate_term3(int *unknown_idx)
{
	char	op;
	double	result, operand;
	int	res_idx = -7, oper_idx = -8;	/* set invalid values to catch errors */

	if (TRX_INFINITY == (result = evaluate_term4(&res_idx)))
		return TRX_INFINITY;

	if (TRX_UNKNOWN == result)
		*unknown_idx = res_idx;

	/* if evaluate_term4() returns TRX_UNKNOWN then continue as with regular number */

	while (1)
	{
		if ('=' == *ptr)
		{
			op = *ptr++;
		}
		else if ('<' == ptr[0] && '>' == ptr[1])
		{
			op = '#';
			ptr += 2;
		}
		else
			break;

		/* even if 1st operand is Unknown we evaluate 2nd operand to catch fatal error if any occurs */

		if (TRX_INFINITY == (operand = evaluate_term4(&oper_idx)))
			return TRX_INFINITY;

		if (TRX_UNKNOWN == operand)		/* (anything) = Unknown, (anything) <> Unknown */
		{
			*unknown_idx = oper_idx;
			res_idx = oper_idx;
			result = TRX_UNKNOWN;
		}
		else if (TRX_UNKNOWN == result)		/* Unknown = known, Unknown <> known */
		{
			*unknown_idx = res_idx;
		}
		else if ('=' == op)
		{
			result = (SUCCEED == trx_double_compare(result, operand));
		}
		else
			result = (SUCCEED != trx_double_compare(result, operand));
	}

	return result;
}

/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "and"                                                    *
 *                                                                            *
 *      0.0 and Unknown  -> 0.0                                               *
 *  Unknown and 0.0      -> 0.0                                               *
 *      1.0 and Unknown  -> Unknown                                           *
 *  Unknown and 1.0      -> Unknown                                           *
 *  Unknown and Unknown  -> Unknown                                           *
 *                                                                            *
 ******************************************************************************/
static double	evaluate_term2(int *unknown_idx)
{
	double	result, operand;
	int	res_idx = -9, oper_idx = -10;	/* set invalid values to catch errors */

	if (TRX_INFINITY == (result = evaluate_term3(&res_idx)))
		return TRX_INFINITY;

	if (TRX_UNKNOWN == result)
		*unknown_idx = res_idx;

	/* if evaluate_term3() returns TRX_UNKNOWN then continue as with regular number */

	while ('a' == ptr[0] && 'n' == ptr[1] && 'd' == ptr[2] && SUCCEED == is_operator_delimiter(ptr[3]))
	{
		ptr += 3;

		if (TRX_INFINITY == (operand = evaluate_term3(&oper_idx)))
			return TRX_INFINITY;

		if (TRX_UNKNOWN == result)
		{
			if (TRX_UNKNOWN == operand)				/* Unknown and Unknown */
			{
				*unknown_idx = oper_idx;
				res_idx = oper_idx;
				result = TRX_UNKNOWN;
			}
			else if (SUCCEED == trx_double_compare(operand, 0.0))	/* Unknown and 0 */
			{
				result = 0.0;
			}
			else							/* Unknown and 1 */
				*unknown_idx = res_idx;
		}
		else if (TRX_UNKNOWN == operand)
		{
			if (SUCCEED == trx_double_compare(result, 0.0))		/* 0 and Unknown */
			{
				result = 0.0;
			}
			else							/* 1 and Unknown */
			{
				*unknown_idx = oper_idx;
				res_idx = oper_idx;
				result = TRX_UNKNOWN;
			}
		}
		else
		{
			result = (SUCCEED != trx_double_compare(result, 0.0) &&
					SUCCEED != trx_double_compare(operand, 0.0));
		}
	}

	return result;
}

/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "or"                                                     *
 *                                                                            *
 *      1.0 or Unknown  -> 1.0                                                *
 *  Unknown or 1.0      -> 1.0                                                *
 *      0.0 or Unknown  -> Unknown                                            *
 *  Unknown or 0.0      -> Unknown                                            *
 *  Unknown or Unknown  -> Unknown                                            *
 *                                                                            *
 ******************************************************************************/
static double	evaluate_term1(int *unknown_idx)
{
	double	result, operand;
	int	res_idx = -11, oper_idx = -12;	/* set invalid values to catch errors */

	level++;

	if (32 < level)
	{
		trx_strlcpy(buffer, "Cannot evaluate expression: nesting level is too deep.", max_buffer_len);
		return TRX_INFINITY;
	}

	if (TRX_INFINITY == (result = evaluate_term2(&res_idx)))
		return TRX_INFINITY;

	if (TRX_UNKNOWN == result)
		*unknown_idx = res_idx;

	/* if evaluate_term2() returns TRX_UNKNOWN then continue as with regular number */

	while ('o' == ptr[0] && 'r' == ptr[1] && SUCCEED == is_operator_delimiter(ptr[2]))
	{
		ptr += 2;

		if (TRX_INFINITY == (operand = evaluate_term2(&oper_idx)))
			return TRX_INFINITY;

		if (TRX_UNKNOWN == result)
		{
			if (TRX_UNKNOWN == operand)				/* Unknown or Unknown */
			{
				*unknown_idx = oper_idx;
				res_idx = oper_idx;
				result = TRX_UNKNOWN;
			}
			else if (SUCCEED != trx_double_compare(operand, 0.0))	/* Unknown or 1 */
			{
				result = 1;
			}
			else							/* Unknown or 0 */
				*unknown_idx = res_idx;
		}
		else if (TRX_UNKNOWN == operand)
		{
			if (SUCCEED != trx_double_compare(result, 0.0))		/* 1 or Unknown */
			{
				result = 1;
			}
			else							/* 0 or Unknown */
			{
				*unknown_idx = oper_idx;
				res_idx = oper_idx;
				result = TRX_UNKNOWN;
			}
		}
		else
		{
			result = (SUCCEED != trx_double_compare(result, 0.0) ||
					SUCCEED != trx_double_compare(operand, 0.0));
		}
	}

	level--;

	return result;
}

/******************************************************************************
 *                                                                            *
 * Purpose: evaluate an expression like "(26.416>10) or (0=1)"                *
 *                                                                            *
 ******************************************************************************/
int	evaluate(double *value, const char *expression, char *error, size_t max_error_len,
		trx_vector_ptr_t *unknown_msgs)
{
	int	unknown_idx = -13;	/* index of message in 'unknown_msgs' vector, set to invalid value */
					/* to catch errors */

	treegix_log(LOG_LEVEL_DEBUG, "In %s() expression:'%s'", __func__, expression);

	ptr = expression;
	level = 0;

	buffer = error;
	max_buffer_len = max_error_len;

	*value = evaluate_term1(&unknown_idx);

	if ('\0' != *ptr && TRX_INFINITY != *value)
	{
		trx_snprintf(error, max_error_len, "Cannot evaluate expression: unexpected token at \"%s\".", ptr);
		*value = TRX_INFINITY;
	}

	if (TRX_UNKNOWN == *value)
	{
		/* Map Unknown result to error. Callers currently do not operate with TRX_UNKNOWN. */
		if (NULL != unknown_msgs)
		{
			if (0 > unknown_idx)
			{
				THIS_SHOULD_NEVER_HAPPEN;
				treegix_log(LOG_LEVEL_WARNING, "%s() internal error: " TRX_UNKNOWN_STR " index:%d"
						" expression:'%s'", __func__, unknown_idx, expression);
				trx_snprintf(error, max_error_len, "Internal error: " TRX_UNKNOWN_STR " index %d."
						" Please report this to Treegix developers.", unknown_idx);
			}
			else if (unknown_msgs->values_num > unknown_idx)
			{
				trx_snprintf(error, max_error_len, "Cannot evaluate expression: \"%s\".",
						(char *)(unknown_msgs->values[unknown_idx]));
			}
			else
			{
				trx_snprintf(error, max_error_len, "Cannot evaluate expression: unsupported "
						TRX_UNKNOWN_STR "%d value.", unknown_idx);
			}
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
			/* do not leave garbage in error buffer, write something helpful */
			trx_snprintf(error, max_error_len, "%s(): internal error: no message for unknown result",
					__func__);
		}

		*value = TRX_INFINITY;
	}

	if (TRX_INFINITY == *value)
	{
		treegix_log(LOG_LEVEL_DEBUG, "End of %s() error:'%s'", __func__, error);
		return FAIL;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() value:" TRX_FS_DBL, __func__, *value);

	return SUCCEED;
}
