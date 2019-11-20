

#include "common.h"
#include "log.h"
#include "trxalgo.h"
#include "trxregexp.h"
#include "trxjson.h"
#include "json.h"
#include "json_parser.h"
#include "jsonpath.h"

#include "../trxalgo/vectorimpl.h"

TRX_VECTOR_DECL(var, trx_variant_t)
TRX_VECTOR_IMPL(var, trx_variant_t)

static int	jsonpath_query_object(const struct trx_json_parse *jp_root, const struct trx_json_parse *jp,
		const trx_jsonpath_t *jsonpath, int path_depth, trx_vector_str_t *objects);
static int	jsonpath_query_array(const struct trx_json_parse *jp_root, const struct trx_json_parse *jp,
		const trx_jsonpath_t *jsonpath, int path_depth, trx_vector_str_t *objects);

typedef struct
{
	trx_jsonpath_token_group_t	group;
	int				precedence;
}
trx_jsonpath_token_def_t;

/* define token groups and precedence */
static trx_jsonpath_token_def_t	jsonpath_tokens[] = {
	{0, 0},
	{TRX_JSONPATH_TOKEN_GROUP_OPERAND, 0},		/* TRX_JSONPATH_TOKEN_PATH_ABSOLUTE */
	{TRX_JSONPATH_TOKEN_GROUP_OPERAND, 0},		/* TRX_JSONPATH_TOKEN_PATH_RELATIVE */
	{TRX_JSONPATH_TOKEN_GROUP_OPERAND, 0},		/* TRX_JSONPATH_TOKEN_CONST_STR */
	{TRX_JSONPATH_TOKEN_GROUP_OPERAND, 0},		/* TRX_JSONPATH_TOKEN_CONST_NUM */
	{TRX_JSONPATH_TOKEN_GROUP_NONE, 0},		/* TRX_JSONPATH_TOKEN_PAREN_LEFT */
	{TRX_JSONPATH_TOKEN_GROUP_NONE, 0},		/* TRX_JSONPATH_TOKEN_PAREN_RIGHT */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR2, 4},	/* TRX_JSONPATH_TOKEN_OP_PLUS */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR2, 4},	/* TRX_JSONPATH_TOKEN_OP_MINUS */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR2, 3},	/* TRX_JSONPATH_TOKEN_OP_MULT */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR2, 3},	/* TRX_JSONPATH_TOKEN_OP_DIV */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR2, 7},	/* TRX_JSONPATH_TOKEN_OP_EQ */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR2, 7},	/* TRX_JSONPATH_TOKEN_OP_NE */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR2, 6},	/* TRX_JSONPATH_TOKEN_OP_GT */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR2, 6},	/* TRX_JSONPATH_TOKEN_OP_GE */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR2, 6},	/* TRX_JSONPATH_TOKEN_OP_LT */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR2, 6},	/* TRX_JSONPATH_TOKEN_OP_LE */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR1, 2},	/* TRX_JSONPATH_TOKEN_OP_NOT */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR2, 11},	/* TRX_JSONPATH_TOKEN_OP_AND */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR2, 12},	/* TRX_JSONPATH_TOKEN_OP_OR */
	{TRX_JSONPATH_TOKEN_GROUP_OPERATOR2, 7}		/* TRX_JSONPATH_TOKEN_OP_REGEXP */
};

static int	jsonpath_token_precedence(int type)
{
	return jsonpath_tokens[type].precedence;
}

static int	jsonpath_token_group(int type)
{
	return jsonpath_tokens[type].group;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_jsonpath_error                                               *
 *                                                                            *
 * Purpose: set json error message and return FAIL                            *
 *                                                                            *
 * Comments: This function is used to return from json path parsing functions *
 *           in the case of failure.                                          *
 *                                                                            *
 ******************************************************************************/
static int	trx_jsonpath_error(const char *path)
{
	if ('\0' != *path)
		trx_set_json_strerror("unsupported construct in jsonpath starting with: \"%s\"", path);
	else
		trx_set_json_strerror("jsonpath was unexpectedly terminated");

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_strndup                                                 *
 *                                                                            *
 ******************************************************************************/
static char	*jsonpath_strndup(const char *source, size_t len)
{
	char	*str;

	str = (char *)trx_malloc(NULL, len + 1);
	memcpy(str, source, len);
	str[len] = '\0';

	return str;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_unquote                                                 *
 *                                                                            *
 * Purpose: unquote single or double quoted string by stripping               *
 *          leading/trailing quotes and unescaping backslash sequences        *
 *                                                                            *
 * Parameters: value - [OUT] the output value, must have at least len bytes   *
 *             start - [IN] a single or double quoted string to unquote       *
 *             len   - [IN] the length of the input string                    *
 *                                                                            *
 ******************************************************************************/
static void	jsonpath_unquote(char *value, const char *start, size_t len)
{
	const char	*end = start + len - 1;

	for (start++; start != end; start++)
	{
		if ('\\' == *start)
			start++;

		*value++ = *start;
	}

	*value = '\0';
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_unquote_dyn                                             *
 *                                                                            *
 * Purpose: unquote string stripping leading/trailing quotes and unescaping   *
 *          backspace sequences                                               *
 *                                                                            *
 * Parameters: start - [IN] the string to unquote including leading and       *
 *                          trailing quotes                                   *
 *             len   - [IN] the length of the input string                    *
 *                                                                            *
 * Return value: The unescaped string (must be freed by the caller).          *
 *                                                                            *
 ******************************************************************************/
static char	*jsonpath_unquote_dyn(const char *start, size_t len)
{
	char	*value;

	value = (char *)trx_malloc(NULL, len + 1);
	jsonpath_unquote(value, start, len);

	return value;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_list_create_item                                        *
 *                                                                            *
 * Purpose: create jsonpath list item of the specified size                   *
 *                                                                            *
 ******************************************************************************/
static trx_jsonpath_list_node_t	*jsonpath_list_create_node(size_t size)
{
	return (trx_jsonpath_list_node_t *)trx_malloc(NULL, offsetof(trx_jsonpath_list_node_t, data) + size);
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_list_free                                               *
 *                                                                            *
 * Purpose: free jsonpath list                                                *
 *                                                                            *
 ******************************************************************************/
static void	jsonpath_list_free(trx_jsonpath_list_node_t *list)
{
	trx_jsonpath_list_node_t	*item = list;

	while (NULL != list)
	{
		item = list;
		list = list->next;
		trx_free(item);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_create_token                                            *
 *                                                                            *
 * Purpose: create jsonpath expression token                                  *
 *                                                                            *
 * Parameters: type       - [IN] the token type                               *
 *             expression - [IN] the expression                               *
 *             loc        - [IN] the token location in the expression         *
 *                                                                            *
 * Return value: The created token (must be freed by the caller).             *
 *                                                                            *
 ******************************************************************************/
static trx_jsonpath_token_t	*jsonpath_create_token(int type, const char *expression, const trx_strloc_t *loc)
{
	trx_jsonpath_token_t	*token;

	token = (trx_jsonpath_token_t *)trx_malloc(NULL, sizeof(trx_jsonpath_token_t));
	token->type = type;

	switch (token->type)
	{
		case TRX_JSONPATH_TOKEN_CONST_STR:
			token->data = jsonpath_unquote_dyn(expression + loc->l, loc->r - loc->l + 1);
			break;
		case TRX_JSONPATH_TOKEN_PATH_ABSOLUTE:
		case TRX_JSONPATH_TOKEN_PATH_RELATIVE:
		case TRX_JSONPATH_TOKEN_CONST_NUM:
			token->data = jsonpath_strndup(expression + loc->l, loc->r - loc->l + 1);
			break;
		default:
			token->data = NULL;
	}

	return token;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_token_free                                              *
 *                                                                            *
 ******************************************************************************/
static void	jsonpath_token_free(trx_jsonpath_token_t *token)
{
	trx_free(token->data);
	trx_free(token);
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_reserve                                                 *
 *                                                                            *
 * Purpose: reserve space in jsonpath segments array for more segments        *
 *                                                                            *
 * Parameters: jsonpath - [IN] the jsonpath data                              *
 *             num      - [IN] the number of segments to reserve              *
 *                                                                            *
 ******************************************************************************/
static void	jsonpath_reserve(trx_jsonpath_t *jsonpath, int num)
{
	if (jsonpath->segments_num + num > jsonpath->segments_alloc)
	{
		int	old_alloc = jsonpath->segments_alloc;

		if (jsonpath->segments_alloc < num)
			jsonpath->segments_alloc = jsonpath->segments_num + num;
		else
			jsonpath->segments_alloc *= 2;

		jsonpath->segments = (trx_jsonpath_segment_t *)trx_realloc(jsonpath->segments,
				sizeof(trx_jsonpath_segment_t) * jsonpath->segments_alloc);

		/* Initialize the memory allocated for new segments, as parser can set     */
		/* detached flag for the next segment, so the memory cannot be initialized */
		/* when creating a segment.                                                */
		memset(jsonpath->segments + old_alloc, 0,
				(jsonpath->segments_alloc - old_alloc) * sizeof(trx_jsonpath_segment_t));
	}
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_segment_clear                                           *
 *                                                                            *
 ******************************************************************************/
static void	jsonpath_segment_clear(trx_jsonpath_segment_t *segment)
{
	switch (segment->type)
	{
		case TRX_JSONPATH_SEGMENT_MATCH_LIST:
			jsonpath_list_free(segment->data.list.values);
			break;
		case TRX_JSONPATH_SEGMENT_MATCH_EXPRESSION:
			trx_vector_ptr_clear_ext(&segment->data.expression.tokens,
					(trx_clean_func_t)jsonpath_token_free);
			trx_vector_ptr_destroy(&segment->data.expression.tokens);
			break;
		default:
			break;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_next                                                    *
 *                                                                            *
 * Purpose: find next component of json path                                  *
 *                                                                            *
 * Parameters: pnext - [IN/OUT] the reference to the next path component      *
 *                                                                            *
 * Return value: SUCCEED - the json path component was parsed successfully    *
 *               FAIL    - json path parsing error                            *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_next(const char **pnext)
{
	const char	*next = *pnext, *start;

	/* process dot notation component */
	if ('.' == *next)
	{
		if ('\0' == *(++next))
			return trx_jsonpath_error(*pnext);

		if ('[' != *next)
		{
			start = next;

			while (0 != isalnum((unsigned char)*next) || '_' == *next)
				next++;

			if (start == next)
				return trx_jsonpath_error(*pnext);

			*pnext = next;
			return SUCCEED;
		}
	}

	if ('[' != *next)
		return trx_jsonpath_error(*pnext);

	SKIP_WHITESPACE_NEXT(next);

	/* process array index component */
	if (0 != isdigit((unsigned char)*next))
	{
		size_t	pos;

		for (pos = 1; 0 != isdigit((unsigned char)next[pos]); pos++)
			;

		next += pos;
		SKIP_WHITESPACE(next);
	}
	else
	{
		char	quotes;

		if ('\'' != *next && '"' != *next)
			return trx_jsonpath_error(*pnext);

		start = next;

		for (quotes = *next++; quotes != *next; next++)
		{
			if ('\0' == *next)
				return trx_jsonpath_error(*pnext);
		}

		if (start == next)
			return trx_jsonpath_error(*pnext);

		SKIP_WHITESPACE_NEXT(next);
	}

	if (']' != *next++)
		return trx_jsonpath_error(*pnext);

	*pnext = next;
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_parse_substring                                         *
 *                                                                            *
 * Purpose: parse single or double quoted substring                           *
 *                                                                            *
 * Parameters: start - [IN] the substring start                               *
 *             len   - [OUT] the substring length                             *
 *                                                                            *
 * Return value: SUCCEED - the substring was parsed successfully              *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_parse_substring(const char *start, int *len)
{
	const char	*ptr;
	char		quotes;

	for (quotes = *start, ptr = start + 1; '\0' != *ptr; ptr++)
	{
		if (*ptr == quotes)
		{
			*len = ptr - start + 1;
			return SUCCEED;
		}

		if ('\\' == *ptr)
		{
			if (quotes != ptr[1] && '\\' != ptr[1] )
				return FAIL;
			ptr++;
		}
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_parse_path                                              *
 *                                                                            *
 * Purpose: parse jsonpath reference                                          *
 *                                                                            *
 * Parameters: start - [IN] the jsonpath start                                *
 *             len   - [OUT] the jsonpath length                              *
 *                                                                            *
 * Return value: SUCCEED - the jsonpath was parsed successfully               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: This function is used to parse jsonpath references used in       *
 *           jsonpath filter expressions.                                     *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_parse_path(const char *start, int *len)
{
	const char	*ptr = start + 1;

	while ('[' == *ptr || '.' == *ptr)
	{
		if (FAIL == jsonpath_next(&ptr))
			return FAIL;
	}

	*len = ptr - start;
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_parse_number                                            *
 *                                                                            *
 * Purpose: parse number value                                                *
 *                                                                            *
 * Parameters: start - [IN] the number start                                  *
 *             len   - [OUT] the number length                                *
 *                                                                            *
 * Return value: SUCCEED - the number was parsed successfully                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_parse_number(const char *start, int *len)
{
	const char	*ptr = start;
	int		size;

	if ('-' == *ptr || '+' == *ptr)
		ptr++;

	if (FAIL == trx_number_parse(ptr, &size))
		return FAIL;

	ptr += size;

	if ('e' == *ptr || 'E' == *ptr)
	{
		ptr++;

		if ('-' == *ptr || '+' == *ptr)
			ptr++;

		if (0 == isdigit((unsigned char)*ptr))
			return FAIL;

		while (0 != isdigit((unsigned char)*ptr))
			ptr++;
	}

	*len = ptr - start;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_expression_next_token                                   *
 *                                                                            *
 * Purpose: get next token in jsonpath expression                             *
 *                                                                            *
 * Parameters: exprsesion - [IN] the jsonpath expression                      *
 *             pos        - [IN] the position of token in the expression      *
 *             prev_group - [IN] the preceding token group, used to determine *
 *                               token type based on context if necessary     *
 *             type       - [OUT] the token type                              *
 *             loc        - [OUT] the token location in the expression        *
 *                                                                            *
 * Return value: SUCCEED - the token was parsed successfully                  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_expression_next_token(const char *expression, int pos, int prev_group,
		trx_jsonpath_token_type_t *type, trx_strloc_t *loc)
{
	int		len;
	const char	*ptr = expression + pos;

	SKIP_WHITESPACE(ptr);
	loc->l = ptr - expression;

	switch (*ptr)
	{
		case '(':
			*type = TRX_JSONPATH_TOKEN_PAREN_LEFT;
			loc->r = loc->l;
			return SUCCEED;
		case ')':
			*type = TRX_JSONPATH_TOKEN_PAREN_RIGHT;
			loc->r = loc->l;
			return SUCCEED;
		case '+':
			*type = TRX_JSONPATH_TOKEN_OP_PLUS;
			loc->r = loc->l;
			return SUCCEED;
		case '-':
			if (TRX_JSONPATH_TOKEN_GROUP_OPERAND == prev_group)
			{
				*type = TRX_JSONPATH_TOKEN_OP_MINUS;
				loc->r = loc->l;
				return SUCCEED;
			}
			break;
		case '/':
			*type = TRX_JSONPATH_TOKEN_OP_DIV;
			loc->r = loc->l;
			return SUCCEED;
		case '*':
			*type = TRX_JSONPATH_TOKEN_OP_MULT;
			loc->r = loc->l;
			return SUCCEED;
		case '!':
			if ('=' == ptr[1])
			{
				*type = TRX_JSONPATH_TOKEN_OP_NE;
				loc->r = loc->l + 1;
				return SUCCEED;
			}
			*type = TRX_JSONPATH_TOKEN_OP_NOT;
			loc->r = loc->l;
			return SUCCEED;
		case '=':
			switch (ptr[1])
			{
				case '=':
					*type = TRX_JSONPATH_TOKEN_OP_EQ;
					loc->r = loc->l + 1;
					return SUCCEED;
				case '~':
					*type = TRX_JSONPATH_TOKEN_OP_REGEXP;
					loc->r = loc->l + 1;
					return SUCCEED;
			}
			goto out;
		case '<':
			if ('=' == ptr[1])
			{
				*type = TRX_JSONPATH_TOKEN_OP_LE;
				loc->r = loc->l + 1;
				return SUCCEED;
			}
			*type = TRX_JSONPATH_TOKEN_OP_LT;
			loc->r = loc->l;
			return SUCCEED;
		case '>':
			if ('=' == ptr[1])
			{
				*type = TRX_JSONPATH_TOKEN_OP_GE;
				loc->r = loc->l + 1;
				return SUCCEED;
			}
			*type = TRX_JSONPATH_TOKEN_OP_GT;
			loc->r = loc->l;
			return SUCCEED;
		case '|':
			if ('|' == ptr[1])
			{
				*type = TRX_JSONPATH_TOKEN_OP_OR;
				loc->r = loc->l + 1;
				return SUCCEED;
			}
			goto out;
		case '&':
			if ('&' == ptr[1])
			{
				*type = TRX_JSONPATH_TOKEN_OP_AND;
				loc->r = loc->l + 1;
				return SUCCEED;
			}
			goto out;
		case '@':
			if (SUCCEED == jsonpath_parse_path(ptr, &len))
			{
				*type = TRX_JSONPATH_TOKEN_PATH_RELATIVE;
				loc->r = loc->l + len - 1;
				return SUCCEED;
			}
			goto out;

		case '$':
			if (SUCCEED == jsonpath_parse_path(ptr, &len))
			{
				*type = TRX_JSONPATH_TOKEN_PATH_ABSOLUTE;
				loc->r = loc->l + len - 1;
				return SUCCEED;
			}
			goto out;
		case '\'':
		case '"':
			if (SUCCEED == jsonpath_parse_substring(ptr, &len))
			{
				*type = TRX_JSONPATH_TOKEN_CONST_STR;
				loc->r = loc->l + len - 1;
				return SUCCEED;
			}
			goto out;
	}

	if ('-' == *ptr || 0 != isdigit((unsigned char)*ptr))
	{
		if (SUCCEED == jsonpath_parse_number(ptr, &len))
		{
			*type = TRX_JSONPATH_TOKEN_CONST_NUM;
			loc->r = loc->l + len - 1;
			return SUCCEED;
		}
	}
out:
	return trx_jsonpath_error(ptr);
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_parse_expression                                        *
 *                                                                            *
 * Purpose: parse jsonpath filter expression in format                        *
 *                                                                            *
 * Parameters: expression - [IN] the expression, including opening and        *
 *                               closing parenthesis                          *
 *             jsonpath   - [IN/OUT] the jsonpath                             *
 *             next       - [OUT] a pointer to the next character after       *
 *                                parsed expression                           *
 *                                                                            *
 * Return value: SUCCEED - the expression was parsed successfully             *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: This function uses shunting-yard algorithm to store parsed       *
 *           tokens in postfix notation for evaluation.                       *
 *                                                                            *
 *  The following token precedence rules are enforced:                        *
 *   1) binary operator must follow an operand                                *
 *   2) operand must follow an operator                                       *
 *   3) unary operator must follow an operator                                *
 *   4) ')' must follow an operand                                            *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_parse_expression(const char *expression, trx_jsonpath_t *jsonpath, const char **next)
{
	int				nesting = 1, ret = FAIL;
	trx_jsonpath_token_t		*optoken;
	trx_vector_ptr_t		output, operators;
	trx_strloc_t			loc = {0, 0};
	trx_jsonpath_token_type_t	token_type;
	trx_jsonpath_token_group_t	prev_group = TRX_JSONPATH_TOKEN_GROUP_NONE;

	if ('(' != *expression)
		return trx_jsonpath_error(expression);

	trx_vector_ptr_create(&output);
	trx_vector_ptr_create(&operators);

	while (SUCCEED == jsonpath_expression_next_token(expression, loc.r + 1, prev_group, &token_type, &loc))
	{
		switch (token_type)
		{
			case TRX_JSONPATH_TOKEN_PAREN_LEFT:
				nesting++;
				break;

			case TRX_JSONPATH_TOKEN_PAREN_RIGHT:
				if (TRX_JSONPATH_TOKEN_GROUP_OPERAND != prev_group)
				{
					trx_jsonpath_error(expression + loc.l);
					goto out;
				}

				if (0 == --nesting)
				{
					*next = expression + loc.r + 1;
					ret = SUCCEED;
					goto out;
				}
				break;
			default:
				break;
		}

		if (TRX_JSONPATH_TOKEN_GROUP_OPERAND == jsonpath_token_group(token_type))
		{
			/* expression cannot have two consequent operands */
			if (TRX_JSONPATH_TOKEN_GROUP_OPERAND == prev_group)
			{
				trx_jsonpath_error(expression + loc.l);
				goto out;
			}

			trx_vector_ptr_append(&output, jsonpath_create_token(token_type, expression, &loc));
			prev_group = jsonpath_token_group(token_type);
			continue;
		}

		if (TRX_JSONPATH_TOKEN_GROUP_OPERATOR2 == jsonpath_token_group(token_type) ||
				TRX_JSONPATH_TOKEN_GROUP_OPERATOR1 == jsonpath_token_group(token_type))
		{
			/* binary operator must follow an operand  */
			if (TRX_JSONPATH_TOKEN_GROUP_OPERATOR2 == jsonpath_token_group(token_type) &&
					TRX_JSONPATH_TOKEN_GROUP_OPERAND != prev_group)
			{
				trx_jsonpath_error(expression + loc.l);
				goto cleanup;
			}

			/* negation ! operator cannot follow an operand */
			if (TRX_JSONPATH_TOKEN_OP_NOT == token_type &&
					TRX_JSONPATH_TOKEN_GROUP_OPERAND == prev_group)
			{
				trx_jsonpath_error(expression + loc.l);
				goto cleanup;
			}

			for (; 0 < operators.values_num; operators.values_num--)
			{
				optoken = operators.values[operators.values_num - 1];

				if (jsonpath_token_precedence(optoken->type) >
						jsonpath_token_precedence(token_type))
				{
					break;
				}

				if (TRX_JSONPATH_TOKEN_PAREN_LEFT == optoken->type)
					break;

				trx_vector_ptr_append(&output, optoken);
			}

			trx_vector_ptr_append(&operators, jsonpath_create_token(token_type, expression, &loc));
			prev_group = jsonpath_token_group(token_type);
			continue;
		}

		if (TRX_JSONPATH_TOKEN_PAREN_LEFT == token_type)
		{
			trx_vector_ptr_append(&operators, jsonpath_create_token(token_type, expression, &loc));
			prev_group = TRX_JSONPATH_TOKEN_GROUP_NONE;
			continue;
		}

		if (TRX_JSONPATH_TOKEN_PAREN_RIGHT == token_type)
		{
			/* right parenthesis must follow and operand or right parenthesis */
			if (TRX_JSONPATH_TOKEN_GROUP_OPERAND != prev_group)
			{
				trx_jsonpath_error(expression + loc.l);
				goto cleanup;
			}

			for (optoken = 0; 0 < operators.values_num; operators.values_num--)
			{
				optoken = operators.values[operators.values_num - 1];

				if (TRX_JSONPATH_TOKEN_PAREN_LEFT == optoken->type)
				{
					operators.values_num--;
					break;
				}

				trx_vector_ptr_append(&output, optoken);
			}

			if (NULL == optoken)
			{
				trx_jsonpath_error(expression + loc.l);
				goto cleanup;
			}
			jsonpath_token_free(optoken);

			prev_group = TRX_JSONPATH_TOKEN_GROUP_OPERAND;
			continue;
		}
	}
out:
	if (SUCCEED == ret)
	{
		trx_jsonpath_segment_t	*segment;

		for (optoken = 0; 0 < operators.values_num; operators.values_num--)
		{
			optoken = operators.values[operators.values_num - 1];

			if (TRX_JSONPATH_TOKEN_PAREN_LEFT == optoken->type)
			{
				trx_set_json_strerror("mismatched () brackets in expression: %s", expression);
				ret = FAIL;
				goto cleanup;
			}

			trx_vector_ptr_append(&output, optoken);
		}

		jsonpath_reserve(jsonpath, 1);
		segment = &jsonpath->segments[jsonpath->segments_num++];
		segment->type = TRX_JSONPATH_SEGMENT_MATCH_EXPRESSION;
		trx_vector_ptr_create(&segment->data.expression.tokens);
		trx_vector_ptr_append_array(&segment->data.expression.tokens, output.values, output.values_num);

		jsonpath->definite = 0;
	}
cleanup:
	if (SUCCEED != ret)
	{
		trx_vector_ptr_clear_ext(&operators, (trx_clean_func_t)jsonpath_token_free);
		trx_vector_ptr_clear_ext(&output, (trx_clean_func_t)jsonpath_token_free);
	}

	trx_vector_ptr_destroy(&operators);
	trx_vector_ptr_destroy(&output);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_parse_names                                             *
 *                                                                            *
 * Purpose: parse a list of single or double quoted names, including trivial  *
 *          case when a single name is used                                   *
 *                                                                            *
 * Parameters: list     - [IN] the name list                                  *
 *             jsonpath - [IN/OUT] the jsonpath                               *
 *             next     - [OUT] a pointer to the next character after parsed  *
 *                              list                                          *
 *                                                                            *
 * Return value: SUCCEED - the list was parsed successfully                   *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: In the trivial case (when list contains one name) the name is    *
 *           stored into trx_jsonpath_list_t:value field and later its        *
 *           address is stored into trx_jsonpath_list_t:values to reduce      *
 *           allocations in trivial cases.                                    *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_parse_names(const char *list, trx_jsonpath_t *jsonpath, const char **next)
{
	trx_jsonpath_segment_t		*segment;
	int				ret = FAIL, parsed_name = 0;
	const char			*end, *start = NULL;
	trx_jsonpath_list_node_t	*head = NULL;

	for (end = list; ']' != *end || NULL != start; end++)
	{
		switch (*end)
		{
			case '\'':
			case '"':
				if (NULL == start)
				{
					start = end;
				}
				else if (*start == *end)
				{
					trx_jsonpath_list_node_t	*node;

					if (start + 1 == end)
					{
						ret = trx_jsonpath_error(start);
						goto out;
					}

					node = jsonpath_list_create_node(end - start + 1);
					jsonpath_unquote(node->data, start, end - start + 1);
					node->next = head;
					head = node;
					parsed_name = 1;
					start = NULL;
				}
				break;
			case '\\':
				if (NULL == start || ('\\' != end[1] && *start != end[1]))
				{
					ret = trx_jsonpath_error(end);
					goto out;
				}
				end++;
				break;
			case ' ':
			case '\t':
				break;
			case ',':
				if (NULL != start)
					break;

				if (0 == parsed_name)
				{
					ret = trx_jsonpath_error(end);
					goto out;
				}
				parsed_name = 0;
				break;
			case '\0':
				ret = trx_jsonpath_error(end);
				goto out;
			default:
				if (NULL == start)
				{
					ret = trx_jsonpath_error(end);
					goto out;
				}
		}
	}

	if (0 == parsed_name)
	{
		ret = trx_jsonpath_error(end);
		goto out;
	}

	segment = &jsonpath->segments[jsonpath->segments_num++];
	segment->type = TRX_JSONPATH_SEGMENT_MATCH_LIST;
	segment->data.list.type = TRX_JSONPATH_LIST_NAME;
	segment->data.list.values = head;

	if (NULL != head->next)
		jsonpath->definite = 0;

	head = NULL;
	*next = end;
	ret = SUCCEED;
out:
	if (NULL != head)
		jsonpath_list_free(head);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_parse_indexes                                           *
 *                                                                            *
 * Purpose: parse a list of array indexes or range start:end values           *
 *          case when a single name is used                                   *
 *                                                                            *
 * Parameters: list     - [IN] the index list                                 *
 *             jsonpath - [IN/OUT] the jsonpath                               *
 *             next     - [OUT] a pointer to the next character after parsed  *
 *                              list                                          *
 *                                                                            *
 * Return value: SUCCEED - the list was parsed successfully                   *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_parse_indexes(const char *list, trx_jsonpath_t *jsonpath, const char **next)
{
	trx_jsonpath_segment_t		*segment;
	const char			*end, *start = NULL;
	int				ret = FAIL, type = TRX_JSONPATH_SEGMENT_UNKNOWN;
	unsigned int			flags = 0, parsed_index = 0;
	trx_jsonpath_list_node_t	*head = NULL, *node;

	for (end = list; ; end++)
	{
		if (0 != isdigit((unsigned char)*end))
		{
			if (NULL == start)
				start = end;
			continue;
		}

		if ('-' == *end)
		{
			if (NULL != start)
			{
				ret = trx_jsonpath_error(end);
				goto out;
			}
			start = end;
			continue;
		}

		if (NULL != start)
		{
			int	value;

			if ('-' == *start && end == start + 1)
			{
				ret = trx_jsonpath_error(start);
				goto out;
			}

			node = jsonpath_list_create_node(sizeof(int));
			node->next = head;
			head = node;
			value = atoi(start);
			memcpy(node->data, &value, sizeof(int));
			start = NULL;
			parsed_index = 1;
		}

		if (']' == *end)
		{
			if (TRX_JSONPATH_SEGMENT_MATCH_RANGE != type)
			{
				if (0 == parsed_index)
				{
					ret = trx_jsonpath_error(end);
					goto out;
				}
			}
			else
				flags |= (parsed_index << 1);
			break;
		}

		if (':' == *end)
		{
			if (TRX_JSONPATH_SEGMENT_UNKNOWN != type)
			{
				ret = trx_jsonpath_error(end);
				goto out;
			}
			type = TRX_JSONPATH_SEGMENT_MATCH_RANGE;
			flags |= parsed_index;
			parsed_index = 0;
		}
		else if (',' == *end)
		{
			if (TRX_JSONPATH_SEGMENT_MATCH_RANGE == type || 0 == parsed_index)
			{
				ret = trx_jsonpath_error(end);
				goto out;
			}
			type = TRX_JSONPATH_SEGMENT_MATCH_LIST;
			parsed_index = 0;
		}
		else if (' ' != *end && '\t' != *end)
		{
			ret = trx_jsonpath_error(end);
			goto out;
		}
	}

	segment = &jsonpath->segments[jsonpath->segments_num++];

	if (TRX_JSONPATH_SEGMENT_MATCH_RANGE == type)
	{
		node = head;

		segment->type = TRX_JSONPATH_SEGMENT_MATCH_RANGE;
		segment->data.range.flags = flags;
		if (0 != (flags & 0x02))
		{
			memcpy(&segment->data.range.end, node->data, sizeof(int));
			node = node->next;
		}
		else
			segment->data.range.end = 0;

		if (0 != (flags & 0x01))
			memcpy(&segment->data.range.start, node->data, sizeof(int));
		else
			segment->data.range.start = 0;

		jsonpath->definite = 0;
	}
	else
	{
		segment->type = TRX_JSONPATH_SEGMENT_MATCH_LIST;
		segment->data.list.type = TRX_JSONPATH_LIST_INDEX;
		segment->data.list.values = head;

		if (NULL != head->next)
			jsonpath->definite = 0;

		head = NULL;
	}

	*next = end;
	ret = SUCCEED;
out:
	if (NULL != head)
		jsonpath_list_free(head);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_parse_bracket_segment                                   *
 *                                                                            *
 * Purpose: parse jsonpath bracket notation segment                           *
 *                                                                            *
 * Parameters: start     - [IN] the segment start                             *
 *             jsonpath  - [IN/OUT] the jsonpath                              *
 *             next      - [OUT] a pointer to the next character after parsed *
 *                               segment                                      *
 *                                                                            *
 * Return value: SUCCEED - the segment was parsed successfully                *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_parse_bracket_segment(const char *start, trx_jsonpath_t *jsonpath, const char **next)
{
	const char	*ptr = start;
	int		ret;

	SKIP_WHITESPACE(ptr);

	if ('?' == *ptr)
	{
		ret = jsonpath_parse_expression(ptr + 1, jsonpath, next);
	}
	else if ('*' == *ptr)
	{
		jsonpath->segments[jsonpath->segments_num++].type = TRX_JSONPATH_SEGMENT_MATCH_ALL;
		jsonpath->definite = 0;
		*next = ptr + 1;
		ret = SUCCEED;
	}
	else if ('\'' == *ptr || '"' == *ptr)
	{
		ret = jsonpath_parse_names(ptr, jsonpath, next);
	}
	else if (0 != isdigit((unsigned char)*ptr) || ':' == *ptr || '-' == *ptr)
	{
		ret = jsonpath_parse_indexes(ptr, jsonpath, next);
	}
	else
		ret = trx_jsonpath_error(ptr);

	if (SUCCEED == ret)
	{
		ptr = *next;
		SKIP_WHITESPACE(ptr);

		if (']' != *ptr)
			return trx_jsonpath_error(ptr);

		*next = ptr + 1;
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_parse_dot_segment                                       *
 *                                                                            *
 * Purpose: parse jsonpath dot notation segment                               *
 *                                                                            *
 * Parameters: start     - [IN] the segment start                             *
 *             jsonpath  - [IN/OUT] the jsonpath                              *
 *             next      - [OUT] a pointer to the next character after parsed *
 *                               segment                                      *
 *                                                                            *
 * Return value: SUCCEED - the segment was parsed successfully                *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_parse_dot_segment(const char *start, trx_jsonpath_t *jsonpath, const char **next)
{
	trx_jsonpath_segment_t	*segment;
	const char		*ptr;
	int			len;

	segment = &jsonpath->segments[jsonpath->segments_num];
	jsonpath->segments_num++;

	if ('*' == *start)
	{
		jsonpath->definite = 0;
		segment->type = TRX_JSONPATH_SEGMENT_MATCH_ALL;
		*next = start + 1;
		return SUCCEED;
	}

	for (ptr = start; 0 != isalnum((unsigned char)*ptr) || '_' == *ptr;)
		ptr++;

	if ('(' == *ptr)
	{
		const char	*end = ptr + 1;

		SKIP_WHITESPACE(end);
		if (')' == *end)
		{
			if (TRX_CONST_STRLEN("min") == ptr - start && 0 == strncmp(start, "min", ptr - start))
				segment->data.function.type = TRX_JSONPATH_FUNCTION_MIN;
			else if (TRX_CONST_STRLEN("max") == ptr - start && 0 == strncmp(start, "max", ptr - start))
				segment->data.function.type = TRX_JSONPATH_FUNCTION_MAX;
			else if (TRX_CONST_STRLEN("avg") == ptr - start && 0 == strncmp(start, "avg", ptr - start))
				segment->data.function.type = TRX_JSONPATH_FUNCTION_AVG;
			else if (TRX_CONST_STRLEN("length") == ptr - start && 0 == strncmp(start, "length", ptr - start))
				segment->data.function.type = TRX_JSONPATH_FUNCTION_LENGTH;
			else if (TRX_CONST_STRLEN("first") == ptr - start && 0 == strncmp(start, "first", ptr - start))
				segment->data.function.type = TRX_JSONPATH_FUNCTION_FIRST;
			else if (TRX_CONST_STRLEN("sum") == ptr - start && 0 == strncmp(start, "sum", ptr - start))
				segment->data.function.type = TRX_JSONPATH_FUNCTION_SUM;
			else
				return trx_jsonpath_error(start);

			segment->type = TRX_JSONPATH_SEGMENT_FUNCTION;
			*next = end + 1;
			return SUCCEED;
		}
	}

	if (0 < (len = ptr - start))
	{
		segment->type = TRX_JSONPATH_SEGMENT_MATCH_LIST;
		segment->data.list.type = TRX_JSONPATH_LIST_NAME;
		segment->data.list.values = jsonpath_list_create_node(len + 1);
		trx_strlcpy(segment->data.list.values->data, start, len + 1);
		segment->data.list.values->next = NULL;
		*next = start + len;
		return SUCCEED;
	}

	return trx_jsonpath_error(start);
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_pointer_to_jp                                           *
 *                                                                            *
 * Purpose: convert a pointer to an object/array/value in json data to        *
 *          json parse structure                                              *
 *                                                                            *
 * Parameters: pnext - [IN] a pointer to object/array/value data              *
 *             jp    - [OUT] json parse data with start/end set               *
 *                                                                            *
 * Return value: SUCCEED - pointer was converted successfully                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_pointer_to_jp(const char *pnext, struct trx_json_parse *jp)
{
	if ('[' == *pnext || '{' == *pnext)
	{
		return trx_json_brackets_open(pnext, jp);
	}
	else
	{
		jp->start = pnext;
		jp->end = pnext + json_parse_value(pnext, NULL) - 1;
		return SUCCEED;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_query_contents                                          *
 *                                                                            *
 * Purpose: perform the rest of jsonpath query on json data                   *
 *                                                                            *
 * Parameters: jp_root    - [IN] the document root                            *
 *             pnext      - [IN] a pointer to object/array/value in json data *
 *             jsonpath   - [IN] the jsonpath                                 *
 *             path_depth - [IN] the jsonpath segment to match                *
 *             objects    - [OUT] pointers to the matched objects in json     *
 *                                data                                        *
 *                                                                            *
 * Return value: SUCCEED - the data were queried successfully                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_query_contents(const struct trx_json_parse *jp_root, const char *pnext,
		const trx_jsonpath_t *jsonpath, int path_depth, trx_vector_str_t *objects)
{
	struct trx_json_parse	jp_child;

	switch (*pnext)
	{
		case '{':
			if (FAIL == trx_json_brackets_open(pnext, &jp_child))
				return FAIL;

			return jsonpath_query_object(jp_root, &jp_child, jsonpath, path_depth, objects);
		case '[':
			if (FAIL == trx_json_brackets_open(pnext, &jp_child))
				return FAIL;

			return jsonpath_query_array(jp_root, &jp_child, jsonpath, path_depth, objects);
	}
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_query_next_segment                                      *
 *                                                                            *
 * Purpose: query next segment                                                *
 *                                                                            *
 * Parameters: jp_root    - [IN] the document root                            *
 *             pnext      - [IN] a pointer to object/array/value in json data *
 *             jsonpath   - [IN] the jsonpath                                 *
 *             path_depth - [IN] the jsonpath segment to match                *
 *             objects    - [OUT] pointers to the matched objects in json     *
 *                                data                                        *
 *                                                                            *
 * Return value: SUCCEED - the segment was queried successfully               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_query_next_segment(const struct trx_json_parse *jp_root, const char *pnext,
		const trx_jsonpath_t *jsonpath, int path_depth, trx_vector_str_t *objects)
{
	/* check if jsonpath end has been reached, so we have found matching data */
	/* (functions are processed afterwards)                                   */
	if (++path_depth == jsonpath->segments_num ||
			TRX_JSONPATH_SEGMENT_FUNCTION == jsonpath->segments[path_depth].type)
	{
		trx_vector_str_append(objects, (char *)pnext);
		return SUCCEED;
	}

	/* continue by matching found data against the rest of jsonpath segments */
	return jsonpath_query_contents(jp_root, pnext, jsonpath, path_depth, objects);
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_match_name                                              *
 *                                                                            *
 * Purpose: match object value name against jsonpath segment name list        *
 *                                                                            *
 * Parameters: jp_root    - [IN] the document root                            *
 *             pnext      - [IN] a pointer to object value with the specified *
 *                               name                                         *
 *             jsonpath   - [IN] the jsonpath                                 *
 *             path_depth - [IN] the jsonpath segment to match                *
 *             name       - [IN] the object value name                        *
 *             objects    - [OUT] pointers to the matched objects in json     *
 *                                data                                        *
 *                                                                            *
 * Return value: SUCCEED - no errors, failed match is not an error            *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_match_name(const struct trx_json_parse *jp_root, const char *pnext,
		const trx_jsonpath_t *jsonpath, int path_depth, const char *name, trx_vector_str_t *objects)
{
	trx_jsonpath_segment_t		*segment;
	trx_jsonpath_list_node_t	*node;

	segment = &jsonpath->segments[path_depth];

	/* object contents can match only name list */
	if (TRX_JSONPATH_LIST_NAME != segment->data.list.type)
		return SUCCEED;

	for (node = segment->data.list.values; NULL != node; node = node->next)
	{
		if (0 == strcmp(name, node->data))
		{
			if (FAIL == jsonpath_query_next_segment(jp_root, pnext, jsonpath, path_depth, objects))
				return FAIL;
			break;
		}
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_extract_value                                           *
 *                                                                            *
 * Purpose: extract value from json data by the specified path                *
 *                                                                            *
 * Parameters: jp    - [IN] the parent object                                 *
 *             path  - [IN] the jsonpath (definite)                           *
 *             value - [OUT] the extracted value                              *
 *                                                                            *
 * Return value: SUCCEED - the value was extracted successfully               *
 *               FAIL    - in the case of errors or if there was no value to  *
 *                         extract                                            *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_extract_value(const struct trx_json_parse *jp, const char *path, trx_variant_t *value)
{
	struct trx_json_parse	jp_child;
	char			*data = NULL, *tmp_path = NULL;
	size_t			data_alloc = 0;
	int			ret = FAIL;

	if ('@' == *path)
	{
		tmp_path = trx_strdup(NULL, path);
		*tmp_path = '$';
		path = tmp_path;
	}

	if (FAIL == trx_json_open_path(jp, path, &jp_child))
		goto out;

	if (NULL == trx_json_decodevalue_dyn(jp_child.start, &data, &data_alloc, NULL))
	{
		size_t	len = jp_child.end - jp_child.start + 2;

		data = (char *)trx_malloc(NULL, len);
		trx_strlcpy(data, jp_child.start, len);
	}

	trx_variant_set_str(value, data);
	ret = SUCCEED;
out:
	trx_free(tmp_path);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_expression_to_str                                       *
 *                                                                            *
 * Purpose: convert jsonpath expression to text format                        *
 *                                                                            *
 * Parameters: expression - [IN] the jsonpath exprssion                       *
 *                                                                            *
 * Return value: The converted expression, must be freed by the caller.       *
 *                                                                            *
 * Comments: This function is used to include expression in error message.    *
 *                                                                            *
 ******************************************************************************/
static char	*jsonpath_expression_to_str(trx_jsonpath_expression_t *expression)
{
	int			i;
	char			*str = NULL;
	size_t			str_alloc = 0, str_offset = 0;

	for (i = 0; i < expression->tokens.values_num; i++)
	{
		trx_jsonpath_token_t	*token = (trx_jsonpath_token_t *)expression->tokens.values[i];

		if (0 != i)
			trx_strcpy_alloc(&str, &str_alloc, &str_offset, ",");

		switch (token->type)
		{
			case TRX_JSONPATH_TOKEN_PATH_ABSOLUTE:
				TRX_FALLTHROUGH;
			case TRX_JSONPATH_TOKEN_PATH_RELATIVE:
				TRX_FALLTHROUGH;
			case TRX_JSONPATH_TOKEN_CONST_STR:
				TRX_FALLTHROUGH;
			case TRX_JSONPATH_TOKEN_CONST_NUM:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, token->data);
				break;
			case TRX_JSONPATH_TOKEN_PAREN_LEFT:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "(");
				break;
			case TRX_JSONPATH_TOKEN_PAREN_RIGHT:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, ")");
				break;
			case TRX_JSONPATH_TOKEN_OP_PLUS:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "+");
				break;
			case TRX_JSONPATH_TOKEN_OP_MINUS:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "-");
				break;
			case TRX_JSONPATH_TOKEN_OP_MULT:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "*");
				break;
			case TRX_JSONPATH_TOKEN_OP_DIV:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "/");
				break;
			case TRX_JSONPATH_TOKEN_OP_EQ:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "==");
				break;
			case TRX_JSONPATH_TOKEN_OP_NE:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "!=");
				break;
			case TRX_JSONPATH_TOKEN_OP_GT:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, ">");
				break;
			case TRX_JSONPATH_TOKEN_OP_GE:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, ">=");
				break;
			case TRX_JSONPATH_TOKEN_OP_LT:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "<");
				break;
			case TRX_JSONPATH_TOKEN_OP_LE:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "<=");
				break;
			case TRX_JSONPATH_TOKEN_OP_NOT:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "!");
				break;
			case TRX_JSONPATH_TOKEN_OP_AND:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "&&");
				break;
			case TRX_JSONPATH_TOKEN_OP_OR:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "||");
				break;
			case TRX_JSONPATH_TOKEN_OP_REGEXP:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "=~");
				break;
			default:
				trx_strcpy_alloc(&str, &str_alloc, &str_offset, "?");
				break;
		}
	}

	return str;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_set_expression_error                                    *
 *                                                                            *
 * Purpose: set jsonpath expression error message                             *
 *                                                                            *
 * Parameters: expression - [IN] the jsonpath exprssion                       *
 *                                                                            *
 * Comments: This function is used to set error message when expression       *
 *           evaluation fails                                                 *
 *                                                                            *
 ******************************************************************************/
static void	jsonpath_set_expression_error(trx_jsonpath_expression_t *expression)
{
	char	*text;

	text = jsonpath_expression_to_str(expression);
	trx_set_json_strerror("invalid compiled expression: %s", text);
	trx_free(text);
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_variant_to_boolean                                      *
 *                                                                            *
 * Purpose: convert variant value to 'boolean' (1, 0)                         *
 *                                                                            *
 * Parameters: value - [IN/OUT] the value                                     *
 *                                                                            *
 * Comments: This function is used to cast operand to boolean value for       *
 *           boolean functions (and, or, negation).                           *
 *                                                                            *
 ******************************************************************************/
static void	jsonpath_variant_to_boolean(trx_variant_t *value)
{
	double	res;

	switch (value->type)
	{
		case TRX_VARIANT_UI64:
			res = (0 != value->data.ui64 ? 1 : 0);
			break;
		case TRX_VARIANT_DBL:
			res = (SUCCEED != trx_double_compare(value->data.dbl, 0.0) ? 1 : 0);
			break;
		case TRX_VARIANT_STR:
			res = ('\0' != *value->data.str ? 1 : 0);
			break;
		case TRX_VARIANT_NONE:
			res = 0;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			res = 0;
			break;
	}

	trx_variant_clear(value);
	trx_variant_set_dbl(value, res);
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_regexp_match                                            *
 *                                                                            *
 * Purpose: match text against regular expression                             *
 *                                                                            *
 * Parameters: text    - [IN] the text to match                               *
 *             pattern - [IN] the regular expression                          *
 *             result  - [OUT] 1.0 if match succeeded, 0.0 otherwise          *
 *                                                                            *
 * Return value: SUCCEED - regular expression match was performed             *
 *               FAIL    - regular expression error                           *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_regexp_match(const char *text, const char *pattern, double *result)
{
	trx_regexp_t	*rxp;
	const char	*error = NULL;

	if (FAIL == trx_regexp_compile(pattern, &rxp, &error))
	{
		trx_set_json_strerror("invalid regular expression in JSON path: %s", error);
		return FAIL;
	}
	*result = (0 == trx_regexp_match_precompiled(text, rxp) ? 1.0 : 0.0);
	trx_regexp_free(rxp);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_match_expression                                        *
 *                                                                            *
 * Purpose: match json array element/object value against jsonpath expression *
 *                                                                            *
 * Parameters: jp_root    - [IN] the document root                            *
 *             pnext      - [IN] a pointer to array element/object value      *
 *             jsonpath   - [IN] the jsonpath                                 *
 *             path_depth - [IN] the jsonpath segment to match                *
 *             objects    - [OUT] pointers to the matched objects in json     *
 *                                data                                        *
 *                                                                            *
 * Return value: SUCCEED - no errors, failed match is not an error            *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_match_expression(const struct trx_json_parse *jp_root, const char *pnext,
		const trx_jsonpath_t *jsonpath, int path_depth, trx_vector_str_t *objects)
{
	struct trx_json_parse	jp;
	trx_vector_var_t	stack;
	int			i, ret = SUCCEED;
	trx_jsonpath_segment_t	*segment;
	trx_variant_t		value, *right;
	double			res;

	if (SUCCEED != jsonpath_pointer_to_jp(pnext, &jp))
		return FAIL;

	trx_vector_var_create(&stack);

	segment = &jsonpath->segments[path_depth];

	for (i = 0; i < segment->data.expression.tokens.values_num; i++)
	{
		trx_variant_t		*left;
		trx_jsonpath_token_t	*token = (trx_jsonpath_token_t *)segment->data.expression.tokens.values[i];

		if (TRX_JSONPATH_TOKEN_GROUP_OPERATOR2 == jsonpath_token_group(token->type))
		{
			if (2 > stack.values_num)
			{
				jsonpath_set_expression_error(&segment->data.expression);
				ret = FAIL;
				goto out;
			}

			left = &stack.values[stack.values_num - 2];
			right = &stack.values[stack.values_num - 1];

			switch (token->type)
			{
				case TRX_JSONPATH_TOKEN_OP_PLUS:
					trx_variant_convert(left, TRX_VARIANT_DBL);
					trx_variant_convert(right, TRX_VARIANT_DBL);
					left->data.dbl += right->data.dbl;
					stack.values_num--;
					break;
				case TRX_JSONPATH_TOKEN_OP_MINUS:
					trx_variant_convert(left, TRX_VARIANT_DBL);
					trx_variant_convert(right, TRX_VARIANT_DBL);
					left->data.dbl -= right->data.dbl;
					stack.values_num--;
					break;
				case TRX_JSONPATH_TOKEN_OP_MULT:
					trx_variant_convert(left, TRX_VARIANT_DBL);
					trx_variant_convert(right, TRX_VARIANT_DBL);
					left->data.dbl *= right->data.dbl;
					stack.values_num--;
					break;
				case TRX_JSONPATH_TOKEN_OP_DIV:
					trx_variant_convert(left, TRX_VARIANT_DBL);
					trx_variant_convert(right, TRX_VARIANT_DBL);
					left->data.dbl /= right->data.dbl;
					stack.values_num--;
					break;
				case TRX_JSONPATH_TOKEN_OP_EQ:
					res = (0 == trx_variant_compare(left, right) ? 1.0 : 0.0);
					trx_variant_clear(left);
					trx_variant_clear(right);
					trx_variant_set_dbl(left, res);
					stack.values_num--;
					break;
				case TRX_JSONPATH_TOKEN_OP_NE:
					res = (0 != trx_variant_compare(left, right) ? 1.0 : 0.0);
					trx_variant_clear(left);
					trx_variant_clear(right);
					trx_variant_set_dbl(left, res);
					stack.values_num--;
					break;
				case TRX_JSONPATH_TOKEN_OP_GT:
					res = (0 < trx_variant_compare(left, right) ? 1.0 : 0.0);
					trx_variant_clear(left);
					trx_variant_clear(right);
					trx_variant_set_dbl(left, res);
					stack.values_num--;
					break;
				case TRX_JSONPATH_TOKEN_OP_GE:
					res = (0 <= trx_variant_compare(left, right) ? 1.0 : 0.0);
					trx_variant_clear(left);
					trx_variant_clear(right);
					trx_variant_set_dbl(left, res);
					stack.values_num--;
					break;
				case TRX_JSONPATH_TOKEN_OP_LT:
					res = (0 > trx_variant_compare(left, right) ? 1.0 : 0.0);
					trx_variant_clear(left);
					trx_variant_clear(right);
					trx_variant_set_dbl(left, res);
					stack.values_num--;
					break;
				case TRX_JSONPATH_TOKEN_OP_LE:
					res = (0 >= trx_variant_compare(left, right) ? 1.0 : 0.0);
					trx_variant_clear(left);
					trx_variant_clear(right);
					trx_variant_set_dbl(left, res);
					stack.values_num--;
					break;
				case TRX_JSONPATH_TOKEN_OP_AND:
					jsonpath_variant_to_boolean(left);
					jsonpath_variant_to_boolean(right);
					if (SUCCEED != trx_double_compare(left->data.dbl, 0.0) &&
							SUCCEED != trx_double_compare(right->data.dbl, 0.0))
					{
						res = 1.0;
					}
					else
						res = 0.0;
					trx_variant_set_dbl(left, res);
					trx_variant_clear(right);
					stack.values_num--;
					break;
				case TRX_JSONPATH_TOKEN_OP_OR:
					jsonpath_variant_to_boolean(left);
					jsonpath_variant_to_boolean(right);
					if (SUCCEED != trx_double_compare(left->data.dbl, 0.0) ||
							SUCCEED != trx_double_compare(right->data.dbl, 0.0))
					{
						res = 1.0;
					}
					else
						res = 0.0;
					trx_variant_set_dbl(left, res);
					trx_variant_clear(right);
					stack.values_num--;
					break;
				case TRX_JSONPATH_TOKEN_OP_REGEXP:
					trx_variant_convert(left, TRX_VARIANT_STR);
					trx_variant_convert(right, TRX_VARIANT_STR);
					ret = jsonpath_regexp_match(left->data.str, right->data.str, &res);
					trx_variant_clear(left);
					trx_variant_clear(right);

					if (FAIL == ret)
						goto out;

					trx_variant_set_dbl(left, res);
					stack.values_num--;
					break;
				default:
					break;
			}
			continue;
		}

		switch (token->type)
		{
			case TRX_JSONPATH_TOKEN_PATH_ABSOLUTE:
				if (FAIL == jsonpath_extract_value(jp_root, token->data, &value))
					trx_variant_set_none(&value);
				trx_vector_var_append_ptr(&stack, &value);
				break;
			case TRX_JSONPATH_TOKEN_PATH_RELATIVE:
				/* relative path can be applied only to array or object */
				if ('[' != *jp.start && '{' != *jp.start)
					goto out;

				if (FAIL == jsonpath_extract_value(&jp, token->data, &value))
					trx_variant_set_none(&value);
				trx_vector_var_append_ptr(&stack, &value);
				break;
			case TRX_JSONPATH_TOKEN_CONST_STR:
				trx_variant_set_str(&value, trx_strdup(NULL, token->data));
				trx_vector_var_append_ptr(&stack, &value);
				break;
			case TRX_JSONPATH_TOKEN_CONST_NUM:
				trx_variant_set_dbl(&value, atof(token->data));
				trx_vector_var_append_ptr(&stack, &value);
				break;
			case TRX_JSONPATH_TOKEN_OP_NOT:
				if (1 > stack.values_num)
				{
					jsonpath_set_expression_error(&segment->data.expression);
					ret = FAIL;
					goto out;
				}
				right = &stack.values[stack.values_num - 1];
				jsonpath_variant_to_boolean(right);
				right->data.dbl = 1 - right->data.dbl;
				break;
			default:
				break;
		}
	}

	if (1 != stack.values_num)
	{
		jsonpath_set_expression_error(&segment->data.expression);
		goto out;
	}

	jsonpath_variant_to_boolean(&stack.values[0]);
	if (SUCCEED != trx_double_compare(stack.values[0].data.dbl, 0.0))
		ret = jsonpath_query_next_segment(jp_root, pnext, jsonpath, path_depth, objects);
out:
	for (i = 0; i < stack.values_num; i++)
		trx_variant_clear(&stack.values[i]);
	trx_vector_var_destroy(&stack);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_query_object                                            *
 *                                                                            *
 * Purpose: query object fields for jsonpath segment match                    *
 *                                                                            *
 * Parameters: jp_root    - [IN] the document root                            *
 *             jp         - [IN] the json object to query                     *
 *             jsonpath   - [IN] the jsonpath                                 *
 *             path_depth - [IN] the jsonpath segment to match                *
 *             objects    - [OUT] pointers to the matched objects in json     *
 *                                data                                        *
 *                                                                            *
 * Return value: SUCCEED - the object was queried successfully                *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_query_object(const struct trx_json_parse *jp_root, const struct trx_json_parse *jp,
		const trx_jsonpath_t *jsonpath, int path_depth, trx_vector_str_t *objects)
{
	const char		*pnext = NULL;
	char			name[MAX_STRING_LEN];
	trx_jsonpath_segment_t	*segment;
	int			ret = SUCCEED;

	segment = &jsonpath->segments[path_depth];

	while (NULL != (pnext = trx_json_pair_next(jp, pnext, name, sizeof(name))) && SUCCEED == ret)
	{
		switch (segment->type)
		{
			case TRX_JSONPATH_SEGMENT_MATCH_ALL:
				ret = jsonpath_query_next_segment(jp_root, pnext, jsonpath, path_depth, objects);
				break;
			case TRX_JSONPATH_SEGMENT_MATCH_LIST:
				ret = jsonpath_match_name(jp_root, pnext, jsonpath, path_depth, name, objects);
				break;
			case TRX_JSONPATH_SEGMENT_MATCH_EXPRESSION:
				ret = jsonpath_match_expression(jp_root, pnext, jsonpath, path_depth, objects);
				break;
			default:
				break;
		}

		if (1 == segment->detached)
			ret = jsonpath_query_contents(jp_root, pnext, jsonpath, path_depth, objects);
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_match_index                                             *
 *                                                                            *
 * Purpose: match array element against segment index list                    *
 *                                                                            *
 * Parameters: jp_root      - [IN] the document root                          *
 *             pnext        - [IN] a pointer to an array element              *
 *             jsonpath     - [IN] the jsonpath                               *
 *             path_depth   - [IN] the jsonpath segment to match              *
 *             index        - [IN] the array element index                    *
 *             elements_num - [IN] the total number of elements in array      *
 *             objects      - [OUT] pointers to the matched objects in json   *
 *                                  data                                      *
 *                                                                            *
 * Return value: SUCCEED - no errors, failed match is not an error            *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_match_index(const struct trx_json_parse *jp_root, const char *pnext,
		const trx_jsonpath_t *jsonpath, int path_depth, int index, int elements_num, trx_vector_str_t *objects)
{
	trx_jsonpath_segment_t		*segment;
	trx_jsonpath_list_node_t	*node;

	segment = &jsonpath->segments[path_depth];

	/* array contents can match only index list */
	if (TRX_JSONPATH_LIST_INDEX != segment->data.list.type)
		return SUCCEED;

	for (node = segment->data.list.values; NULL != node; node = node->next)
	{
		int	query_index;

		memcpy(&query_index, node->data, sizeof(query_index));

		if ((query_index >= 0 && index == query_index) || index == elements_num + query_index)
		{
			if (FAIL == jsonpath_query_next_segment(jp_root, pnext, jsonpath, path_depth, objects))
				return FAIL;
			break;
		}
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_match_range                                             *
 *                                                                            *
 * Purpose: match array element against segment index range                   *
 *                                                                            *
 * Parameters: jp_root      - [IN] the document root                          *
 *             pnext        - [IN] a pointer to an array element              *
 *             jsonpath     - [IN] the jsonpath                               *
 *             path_depth   - [IN] the jsonpath segment to match              *
 *             index        - [IN] the array element index                    *
 *             elements_num - [IN] the total number of elements in array      *
 *             objects      - [OUT] pointers to the matched objects in json   *
 *                                  data                                      *
 *                                                                            *
 * Return value: SUCCEED - no errors, failed match is not an error            *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_match_range(const struct trx_json_parse *jp_root, const char *pnext,
		const trx_jsonpath_t *jsonpath, int path_depth, int index, int elements_num, trx_vector_str_t *objects)
{
	int			start_index, end_index;
	trx_jsonpath_segment_t	*segment;

	segment = &jsonpath->segments[path_depth];

	start_index = (0 != (segment->data.range.flags & 0x01) ? segment->data.range.start : 0);
	end_index = (0 != (segment->data.range.flags & 0x02) ? segment->data.range.end : elements_num);

	if (0 > start_index)
		start_index += elements_num;
	if (0 > end_index)
		end_index += elements_num;

	if (start_index <= index && end_index > index)
	{
		if (FAIL == jsonpath_query_next_segment(jp_root, pnext, jsonpath, path_depth, objects))
			return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_query_array                                             *
 *                                                                            *
 * Purpose: query array elements for jsonpath segment match                   *
 *                                                                            *
 * Parameters: jp_root    - [IN] the document root                            *
 *             jp         - [IN] the json array to query                      *
 *             jsonpath   - [IN] the jsonpath                                 *
 *             path_depth - [IN] the jsonpath segment to match                *
 *             objects    - [OUT] pointers to the matched objects in json     *
 *                                data                                        *
 *                                                                            *
 * Return value: SUCCEED - the array was queried successfully                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_query_array(const struct trx_json_parse *jp_root, const struct trx_json_parse *jp,
		const trx_jsonpath_t *jsonpath, int path_depth, trx_vector_str_t *objects)
{
	const char		*pnext = NULL;
	int			index = 0, elements_num = 0, ret = SUCCEED;
	trx_jsonpath_segment_t	*segment;

	segment = &jsonpath->segments[path_depth];

	while (NULL != (pnext = trx_json_next(jp, pnext)))
		elements_num++;

	while (NULL != (pnext = trx_json_next(jp, pnext)) && SUCCEED == ret)
	{
		switch (segment->type)
		{
			case TRX_JSONPATH_SEGMENT_MATCH_ALL:
				ret = jsonpath_query_next_segment(jp_root, pnext, jsonpath, path_depth, objects);
				break;
			case TRX_JSONPATH_SEGMENT_MATCH_LIST:
				ret = jsonpath_match_index(jp_root, pnext, jsonpath, path_depth, index,
						elements_num, objects);
				break;
			case TRX_JSONPATH_SEGMENT_MATCH_RANGE:
				ret = jsonpath_match_range(jp_root, pnext, jsonpath, path_depth, index,
						elements_num, objects);
				break;
			case TRX_JSONPATH_SEGMENT_MATCH_EXPRESSION:
				ret = jsonpath_match_expression(jp_root, pnext, jsonpath, path_depth, objects);
				break;
			default:
				break;
		}

		if (1 == segment->detached)
			ret = jsonpath_query_contents(jp_root, pnext, jsonpath, path_depth, objects);

		index++;
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_extract_element                                         *
 *                                                                            *
 * Purpose: extract JSON element value from data                              *
 *                                                                            *
 * Parameters: ptr     - [IN] pointer to the element to extract               *
 *             element - [OUT] the extracted element                          *
 *                                                                            *
 * Return value: SUCCEED - the element was extracted successfully             *
 *               FAIL    - the pointer was not pointing to a JSON element     *
 *                                                                            *
 * Comments: String value element is unquoted, other elements are copied as   *
 *           is.                                                              *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_extract_element(const char *ptr, char **element)
{
	size_t	element_size = 0;

	if (NULL == trx_json_decodevalue_dyn(ptr, element, &element_size, NULL))
	{
		struct trx_json_parse	jp;

		if (SUCCEED != trx_json_brackets_open(ptr, &jp))
			return FAIL;

		*element = jsonpath_strndup(jp.start, jp.end - jp.start + 1);
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_extract_numeric_value                                   *
 *                                                                            *
 * Purpose: extract numeric value from json data                              *
 *                                                                            *
 * Parameters: ptr   - [IN] pointer to the value to extract                   *
 *             value - [OUT] the extracted value                              *
 *                                                                            *
 * Return value: SUCCEED - the value was extracted successfully               *
 *               FAIL    - the pointer was not pointing at numeric value      *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_extract_numeric_value(const char *ptr, double *value)
{
	char	buffer[MAX_STRING_LEN];

	if (NULL == trx_json_decodevalue(ptr, buffer, sizeof(buffer), NULL) ||
		SUCCEED != is_double(buffer))
	{
		trx_set_json_strerror("array value is not a number starting with: %s", ptr);
		return FAIL;
	}

	*value = atof(buffer);
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_apply_function                                          *
 *                                                                            *
 * Purpose: apply jsonpath function to the extracted object list              *
 *                                                                            *
 * Parameters: objects       - [IN] pointers to the objects/arrays/values in  *
 *                                  json data                                 *
 *             type          - [IN] the function type                         *
 *             definite_path - [IN] 1 - if the path is definite (pointing at  *
 *                                      single object)                        *
 *                                  0 - otherwise                             *
 *             output        - [OUT] the output value                         *
 *                                                                            *
 * Return value: SUCCEED - the function was applied successfully              *
 *               FAIL    - invalid input data for the function or internal    *
 *                         json error                                         *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_apply_function(const trx_vector_str_t *objects, trx_jsonpath_function_type_t type,
		int definite_path, char **output)
{
	int			i, ret = FAIL;
	trx_vector_str_t	objects_tmp;
	double			result;

	trx_vector_str_create(&objects_tmp);

	/* convert definite path result to object array if possible */
	if (0 != definite_path)
	{
		const char		*pnext;
		struct trx_json_parse	jp;

		if (0 == objects->values_num || '[' != *objects->values[0])
		{
			/* all functions can be applied only to arrays        */
			/* attempt to apply a function to non-array will fail */
			trx_set_json_strerror("cannot apply function to non-array JSON element");
			goto out;
		}

		if (FAIL == trx_json_brackets_open(objects->values[0], &jp))
			goto out;

		for (pnext = NULL; NULL != (pnext = trx_json_next(&jp, pnext));)
			trx_vector_str_append(&objects_tmp, (char*)pnext);

		objects = &objects_tmp;
	}

	if (TRX_JSONPATH_FUNCTION_LENGTH == type)
	{
		*output = trx_dsprintf(NULL, "%d", objects->values_num);
		ret = SUCCEED;
		goto out;
	}

	if (TRX_JSONPATH_FUNCTION_FIRST == type)
	{
		if (0 < objects->values_num)
			ret = jsonpath_extract_element(objects->values[0], output);
		else
			ret = SUCCEED;

		goto out;
	}

	if (0 == objects->values_num)
	{
		trx_set_json_strerror("cannot apply aggregation function to empty array");
		goto out;
	}

	if (FAIL == jsonpath_extract_numeric_value(objects->values[0], &result))
		goto out;

	for (i = 1; i < objects->values_num; i++)
	{
		double	value;

		if (FAIL == jsonpath_extract_numeric_value(objects->values[i], &value))
			goto out;

		switch (type)
		{
			case TRX_JSONPATH_FUNCTION_MIN:
				if (value < result)
					result = value;
				break;
			case TRX_JSONPATH_FUNCTION_MAX:
				if (value > result)
					result = value;
				break;
			case TRX_JSONPATH_FUNCTION_AVG:
			case TRX_JSONPATH_FUNCTION_SUM:
				result += value;
				break;
			default:
				break;
		}
	}

	if (TRX_JSONPATH_FUNCTION_AVG == type)
		result /= objects->values_num;

	*output = trx_dsprintf(NULL, TRX_FS_DBL, result);
	if (SUCCEED != is_double(*output))
	{
		trx_set_json_strerror("invalid function result: %s", *output);
		goto out;
	}
	del_zeros(*output);
	ret = SUCCEED;
out:
	trx_vector_str_destroy(&objects_tmp);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_apply_functions                                         *
 *                                                                            *
 * Purpose: apply jsonpath function to the extracted object list              *
 *                                                                            *
 * Parameters: jp_root    - [IN] the document root                            *
 *             objects    - [IN] pointers to the objects/arrays/values in json*
 *                               data                                         *
 *             jsonpath   - [IN] the jsonpath                                 *
 *             path_depth - [IN] the jsonpath segment to match                *
 *             output     - [OUT] the output value                            *
 *                                                                            *
 * Return value: SUCCEED - the function was applied successfully              *
 *               FAIL    - invalid input data for the function or internal    *
 *                         json error                                         *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_apply_functions(const struct trx_json_parse *jp_root, const trx_vector_str_t *objects,
		const trx_jsonpath_t *jsonpath, int path_depth, char **output)
{
	int			ret, clear_input_contents = 0, definite_path;
	trx_vector_str_t	input;

	trx_vector_str_create(&input);

	/* when functions are applied directly to the json document (at the start of the jsonpath ) */
	/* it makes all document as input object                                                    */
	if (0 == path_depth)
		trx_vector_str_append(&input, (char *)jp_root->start);
	else
		trx_vector_str_append_array(&input, objects->values, objects->values_num);

	definite_path = jsonpath->definite;

	while (SUCCEED == (ret = jsonpath_apply_function(&input, jsonpath->segments[path_depth].data.function.type,
			definite_path, output)))
	{
		if (++path_depth == jsonpath->segments_num)
			break;

		if (0 != clear_input_contents)
			trx_vector_str_clear_ext(&input, trx_str_free);
		else
			trx_vector_str_clear(&input);

		trx_vector_str_append(&input, *output);
		*output = NULL;
		clear_input_contents = 1;

		/* functions return single value, so for the next functions path becomes definite */
		definite_path = 1;
	}

	if (0 != clear_input_contents)
		trx_vector_str_clear_ext(&input, trx_str_free);
	trx_vector_str_destroy(&input);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_format_query_result                                     *
 *                                                                            *
 * Purpose: format query result, depending on jsonpath type                   *
 *                                                                            *
 * Parameters: objects  - [IN] pointers to the objects/arrays/values in json  *
 *                             data                                           *
 *             jsonpath - [IN] the jsonpath used to acquire result            *
 *             output   - [OUT] the output value                              *
 *                                                                            *
 * Return value: SUCCEED - the result was formatted successfully              *
 *               FAIL    - invalid result data (internal json error)          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_format_query_result(const trx_vector_str_t *objects, trx_jsonpath_t *jsonpath, char **output)
{
	size_t	output_offset = 0, output_alloc;
	int	i;

	if (0 == objects->values_num)
		return SUCCEED;

	if (1 == jsonpath->definite)
	{
		return jsonpath_extract_element(objects->values[0], output);
	}

	/* reserve 32 bytes per returned object plus array start/end [] and terminating zero */
	output_alloc = objects->values_num * 32 + 3;
	*output = (char *)trx_malloc(NULL, output_alloc);

	trx_chrcpy_alloc(output, &output_alloc, &output_offset, '[');

	for (i = 0; i < objects->values_num; i++)
	{
		struct trx_json_parse	jp;

		if (FAIL == jsonpath_pointer_to_jp(objects->values[i], &jp))
		{
			trx_set_json_strerror("cannot format query result, unrecognized json part starting with: %s",
					objects->values[i]);
			trx_free(*output);
			return FAIL;
		}

		if (0 != i)
			trx_chrcpy_alloc(output, &output_alloc, &output_offset, ',');

		trx_strncpy_alloc(output, &output_alloc, &output_offset, jp.start, jp.end - jp.start + 1);
	}

	trx_chrcpy_alloc(output, &output_alloc, &output_offset, ']');

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_jsonpath_clear                                               *
 *                                                                            *
 ******************************************************************************/
void	trx_jsonpath_clear(trx_jsonpath_t *jsonpath)
{
	int	i;

	for (i = 0; i < jsonpath->segments_num; i++)
		jsonpath_segment_clear(&jsonpath->segments[i]);

	trx_free(jsonpath->segments);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_jsonpath_compile                                             *
 *                                                                            *
 * Purpose: compile jsonpath to be used in queries                            *
 *                                                                            *
 * Parameters: path     - [IN] the path to parse                              *
 *             jsonpath  - [IN/OUT] the compiled jsonpath                     *
 *                                                                            *
 * Return value: SUCCEED - the segment was parsed successfully                *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	trx_jsonpath_compile(const char *path, trx_jsonpath_t *jsonpath)
{
	int				ret = FAIL;
	const char			*ptr = path, *next;
	trx_jsonpath_segment_type_t	segment_type, last_segment_type = TRX_JSONPATH_SEGMENT_UNKNOWN;
	trx_jsonpath_t			jpquery;

	if ('$' != *ptr || '\0' == ptr[1])
	{
		trx_set_json_strerror("JSONPath query must start with the root object/element $.");
		return FAIL;
	}

	memset(&jpquery, 0, sizeof(trx_jsonpath_t));
	jsonpath_reserve(&jpquery, 4);
	jpquery.definite = 1;

	for (ptr++; '\0' != *ptr; ptr = next)
	{
		char	prefix;

		jsonpath_reserve(&jpquery, 1);

		if ('.' == (prefix = *ptr))
		{
			if ('.' == *(++ptr))
			{
				/* mark next segment as detached */
				trx_jsonpath_segment_t	*segment = &jpquery.segments[jpquery.segments_num];

				if (1 != segment->detached)
				{
					segment->detached = 1;
					jpquery.definite = 0;
					ptr++;
				}
			}

			switch (*ptr)
			{
				case '[':
					prefix = *ptr;
					break;
				case '\0':
				case '.':
					prefix = 0;
					break;
			}
		}

		switch (prefix)
		{
			case '.':
				ret = jsonpath_parse_dot_segment(ptr, &jpquery, &next);
				break;
			case '[':
				ret = jsonpath_parse_bracket_segment(ptr + 1, &jpquery, &next);
				break;
			default:
				ret = trx_jsonpath_error(ptr);
				break;
		}

		if (SUCCEED != ret)
			break;

		/* function segments can followed only by function segments */
		segment_type = jpquery.segments[jpquery.segments_num - 1].type;
		if (TRX_JSONPATH_SEGMENT_FUNCTION == last_segment_type && TRX_JSONPATH_SEGMENT_FUNCTION != segment_type)
		{
			ret = trx_jsonpath_error(ptr);
			break;
		}
		last_segment_type = segment_type;
	}

	if (SUCCEED == ret && 0 == jpquery.segments_num)
		ret = trx_jsonpath_error(ptr);

	if (SUCCEED == ret)
		*jsonpath = jpquery;
	else
		trx_jsonpath_clear(&jpquery);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_jsonpath_query                                               *
 *                                                                            *
 * Purpose: perform jsonpath query on the specified json data                 *
 *                                                                            *
 * Parameters: jp     - [IN] the json data                                    *
 *             path   - [IN] the jsonpath                                     *
 *             output - [OUT] the output value                                *
 *                                                                            *
 * Return value: SUCCEED - the query was performed successfully (empty result *
 *                         being counted as successful query)                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	trx_jsonpath_query(const struct trx_json_parse *jp, const char *path, char **output)
{
	trx_jsonpath_t		jsonpath;
	int			path_depth = 0, ret = SUCCEED;
	trx_vector_str_t	objects;

	if (FAIL == trx_jsonpath_compile(path, &jsonpath))
		return FAIL;

	trx_vector_str_create(&objects);

	if ('{' == *jp->start)
		ret = jsonpath_query_object(jp, jp, &jsonpath, path_depth, &objects);
	else if ('[' == *jp->start)
		ret = jsonpath_query_array(jp, jp, &jsonpath, path_depth, &objects);

	if (SUCCEED == ret)
	{
		path_depth = jsonpath.segments_num;
		while (0 < path_depth && TRX_JSONPATH_SEGMENT_FUNCTION == jsonpath.segments[path_depth - 1].type)
			path_depth--;

		if (path_depth < jsonpath.segments_num)
			ret = jsonpath_apply_functions(jp, &objects, &jsonpath, path_depth, output);
		else
			ret = jsonpath_format_query_result(&objects, &jsonpath, output);
	}

	trx_vector_str_destroy(&objects);
	trx_jsonpath_clear(&jsonpath);

	return ret;
}
