

#ifndef TREEGIX_TRXODBC_H
#define TREEGIX_TRXODBC_H

typedef struct trx_odbc_data_source	trx_odbc_data_source_t;
typedef struct trx_odbc_query_result	trx_odbc_query_result_t;

trx_odbc_data_source_t	*trx_odbc_connect(const char *dsn, const char *user, const char *pass, int timeout, char **error);
trx_odbc_query_result_t	*trx_odbc_select(const trx_odbc_data_source_t *data_source, const char *query, char **error);

int	trx_odbc_query_result_to_string(trx_odbc_query_result_t *query_result, char **string, char **error);
int	trx_odbc_query_result_to_lld_json(trx_odbc_query_result_t *query_result, char **lld_json, char **error);
int	trx_odbc_query_result_to_json(trx_odbc_query_result_t *query_result, char **out_json, char **error);

void	trx_odbc_query_result_free(trx_odbc_query_result_t *query_result);
void	trx_odbc_data_source_free(trx_odbc_data_source_t *data_source);

#endif
