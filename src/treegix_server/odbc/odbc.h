

#ifndef TREEGIX_TRXODBC_H
#define TREEGIX_TRXODBC_H

typedef struct zbx_odbc_data_source	zbx_odbc_data_source_t;
typedef struct zbx_odbc_query_result	zbx_odbc_query_result_t;

zbx_odbc_data_source_t	*zbx_odbc_connect(const char *dsn, const char *user, const char *pass, int timeout, char **error);
zbx_odbc_query_result_t	*zbx_odbc_select(const zbx_odbc_data_source_t *data_source, const char *query, char **error);

int	zbx_odbc_query_result_to_string(zbx_odbc_query_result_t *query_result, char **string, char **error);
int	zbx_odbc_query_result_to_lld_json(zbx_odbc_query_result_t *query_result, char **lld_json, char **error);
int	zbx_odbc_query_result_to_json(zbx_odbc_query_result_t *query_result, char **out_json, char **error);

void	zbx_odbc_query_result_free(zbx_odbc_query_result_t *query_result);
void	zbx_odbc_data_source_free(zbx_odbc_data_source_t *data_source);

#endif
