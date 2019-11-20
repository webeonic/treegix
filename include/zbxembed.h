

#ifndef TREEGIX_TRXEMBED_H
#define TREEGIX_TRXEMBED_H

typedef struct trx_es_env trx_es_env_t;

typedef struct
{
	trx_es_env_t	*env;
}
trx_es_t;

void	trx_es_init(trx_es_t *es);
void	trx_es_destroy(trx_es_t *es);
int	trx_es_init_env(trx_es_t *es, char **error);
int	trx_es_destroy_env(trx_es_t *es, char **error);
int	trx_es_is_env_initialized(trx_es_t *es);
int	trx_es_fatal_error(trx_es_t *es);
int	trx_es_compile(trx_es_t *es, const char *script, char **code, int *size, char **error);
int	trx_es_execute(trx_es_t *es, const char *script, const char *code, int size, const char *param, char **output,
	char **error);
void	trx_es_set_timeout(trx_es_t *es, int timeout);

#endif /* TREEGIX_TRXEMBED_H */
