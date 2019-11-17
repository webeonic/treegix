

#ifndef TREEGIX_HTTPMACRO_H
#define TREEGIX_HTTPMACRO_H

typedef struct
{
	DB_HTTPTEST		httptest;
	char			*headers;
	zbx_vector_ptr_pair_t	variables;
	/* httptest macro cache consisting of (key, value) pair array */
	zbx_vector_ptr_pair_t	macros;
}
zbx_httptest_t;

typedef struct
{
	DB_HTTPSTEP		*httpstep;
	zbx_httptest_t		*httptest;

	char			*url;
	char			*headers;
	char			*posts;

	zbx_vector_ptr_pair_t	variables;
}
zbx_httpstep_t;

void	http_variable_urlencode(const char *source, char **result);
int	http_substitute_variables(const zbx_httptest_t *httptest, char **data);
int	http_process_variables(zbx_httptest_t *httptest, zbx_vector_ptr_pair_t *variables, const char *data, char **err_str);

#endif
