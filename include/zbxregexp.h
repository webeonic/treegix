#ifndef TREEGIX_TRXREGEXP_H
#define TREEGIX_TRXREGEXP_H

#include "trxalgo.h"

#define TRX_REGEXP_NO_MATCH	0
#define TRX_REGEXP_MATCH	1

typedef struct trx_regexp trx_regexp_t;

typedef struct
{
	char		*name;
	char		*expression;
	int		expression_type;
	char		exp_delimiter;
	unsigned char	case_sensitive;
}
trx_expression_t;

/* regular expressions */
int	trx_regexp_compile(const char *pattern, trx_regexp_t **regexp, const char **err_msg_static);
int	trx_regexp_compile_ext(const char *pattern, trx_regexp_t **regexp, int flags, const char **error);
void	trx_regexp_free(trx_regexp_t *regexp);
int	trx_regexp_match_precompiled(const char *string, const trx_regexp_t *regexp);
char	*trx_regexp_match(const char *string, const char *pattern, int *len);
int	trx_regexp_sub(const char *string, const char *pattern, const char *output_template, char **out);
int	trx_mregexp_sub(const char *string, const char *pattern, const char *output_template, char **out);
int	trx_iregexp_sub(const char *string, const char *pattern, const char *output_template, char **out);
int	trx_mregexp_sub_precompiled(const char *string, const trx_regexp_t *regexp, const char *output_template,
		size_t limit, char **out);

void	trx_regexp_clean_expressions(trx_vector_ptr_t *expressions);

void	add_regexp_ex(trx_vector_ptr_t *regexps, const char *name, const char *expression, int expression_type,
		char exp_delimiter, int case_sensitive);
int	regexp_match_ex(const trx_vector_ptr_t *regexps, const char *string, const char *pattern, int case_sensitive);
int	regexp_sub_ex(const trx_vector_ptr_t *regexps, const char *string, const char *pattern, int case_sensitive,
		const char *output_template, char **output);
int	trx_global_regexp_exists(const char *name, const trx_vector_ptr_t *regexps);
void	trx_regexp_escape(char **string);

#endif /* TREEGIX_TRXREGEXP_H */
