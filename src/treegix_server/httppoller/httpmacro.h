

#ifndef TREEGIX_HTTPMACRO_H
#define TREEGIX_HTTPMACRO_H

typedef struct
{
	DB_HTTPTEST		httptest;
	char			*headers;
	trx_vector_ptr_pair_t	variables;
	/* httptest macro cache consisting of (key, value) pair array */
	trx_vector_ptr_pair_t	macros;
}
trx_httptest_t;

typedef struct
{
	DB_HTTPSTEP		*httpstep;
	trx_httptest_t		*httptest;

	char			*url;
	char			*headers;
	char			*posts;

	trx_vector_ptr_pair_t	variables;
}
trx_httpstep_t;

void	http_variable_urlencode(const char *source, char **result);
int	http_substitute_variables(const trx_httptest_t *httptest, char **data);
int	http_process_variables(trx_httptest_t *httptest, trx_vector_ptr_pair_t *variables, const char *data, char **err_str);

#endif
