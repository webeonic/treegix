

#ifndef TREEGIX_JSONPATH_H
#define TREEGIX_JSONPATH_H

#include "trxalgo.h"

typedef enum
{
	TRX_JSONPATH_SEGMENT_UNKNOWN,
	TRX_JSONPATH_SEGMENT_MATCH_ALL,
	TRX_JSONPATH_SEGMENT_MATCH_LIST,
	TRX_JSONPATH_SEGMENT_MATCH_RANGE,
	TRX_JSONPATH_SEGMENT_MATCH_EXPRESSION,
	TRX_JSONPATH_SEGMENT_FUNCTION
}
trx_jsonpath_segment_type_t;

/* specifies if the match list contains object property names or array indices */
typedef enum
{
	TRX_JSONPATH_LIST_NAME = 1,
	TRX_JSONPATH_LIST_INDEX
}
trx_jsonpath_list_type_t;

typedef enum
{
	TRX_JSONPATH_FUNCTION_MIN = 1,
	TRX_JSONPATH_FUNCTION_MAX,
	TRX_JSONPATH_FUNCTION_AVG,
	TRX_JSONPATH_FUNCTION_LENGTH,
	TRX_JSONPATH_FUNCTION_FIRST,
	TRX_JSONPATH_FUNCTION_SUM
}
trx_jsonpath_function_type_t;

typedef struct trx_jsonpath_list_item
{
	struct trx_jsonpath_list_item	*next;
	/* the structure is always over-allocated so that either int */
	/* or a zero terminated string can be stored in data         */
	char				data[1];
}
trx_jsonpath_list_node_t;

typedef struct
{
	trx_jsonpath_list_node_t	*values;
	trx_jsonpath_list_type_t	type;
}
trx_jsonpath_list_t;

typedef struct
{
	int		start;
	int		end;
	unsigned int	flags;
}
trx_jsonpath_range_t;

/* expression tokens in postfix notation */
typedef struct
{
	trx_vector_ptr_t	tokens;
}
trx_jsonpath_expression_t;

typedef struct
{
	trx_jsonpath_function_type_t	type;
}
trx_jsonpath_function_t;

typedef union
{
	trx_jsonpath_list_t		list;
	trx_jsonpath_range_t		range;
	trx_jsonpath_expression_t	expression;
	trx_jsonpath_function_t		function;
}
trx_jsonpath_data_t;

struct trx_jsonpath_segment
{
	trx_jsonpath_segment_type_t	type;
	trx_jsonpath_data_t		data;

	/* set to 1 if the segment is 'detached' and can be anywhere in parent node tree */
	unsigned char			detached;
};

/*                                                                            */
/* Token groups:                                                              */
/*   operand - constant value, jsonpath reference, result of () evaluation    */
/*   operator2 - binary operator (arithmetic or comparison)                   */
/*   operator1 - unary operator (negation !)                                  */
/*                                                                            */
typedef enum
{
	TRX_JSONPATH_TOKEN_GROUP_NONE,
	TRX_JSONPATH_TOKEN_GROUP_OPERAND,
	TRX_JSONPATH_TOKEN_GROUP_OPERATOR2,	/* binary operator */
	TRX_JSONPATH_TOKEN_GROUP_OPERATOR1	/* unary operator */
}
trx_jsonpath_token_group_t;

/* expression token types */
typedef enum
{
	TRX_JSONPATH_TOKEN_PATH_ABSOLUTE = 1,
	TRX_JSONPATH_TOKEN_PATH_RELATIVE,
	TRX_JSONPATH_TOKEN_CONST_STR,
	TRX_JSONPATH_TOKEN_CONST_NUM,
	TRX_JSONPATH_TOKEN_PAREN_LEFT,
	TRX_JSONPATH_TOKEN_PAREN_RIGHT,
	TRX_JSONPATH_TOKEN_OP_PLUS,
	TRX_JSONPATH_TOKEN_OP_MINUS,
	TRX_JSONPATH_TOKEN_OP_MULT,
	TRX_JSONPATH_TOKEN_OP_DIV,
	TRX_JSONPATH_TOKEN_OP_EQ,
	TRX_JSONPATH_TOKEN_OP_NE,
	TRX_JSONPATH_TOKEN_OP_GT,
	TRX_JSONPATH_TOKEN_OP_GE,
	TRX_JSONPATH_TOKEN_OP_LT,
	TRX_JSONPATH_TOKEN_OP_LE,
	TRX_JSONPATH_TOKEN_OP_NOT,
	TRX_JSONPATH_TOKEN_OP_AND,
	TRX_JSONPATH_TOKEN_OP_OR,
	TRX_JSONPATH_TOKEN_OP_REGEXP
}
trx_jsonpath_token_type_t;

typedef struct
{
	unsigned char	type;
	char		*data;
}
trx_jsonpath_token_t;


#endif
