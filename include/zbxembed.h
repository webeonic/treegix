

#ifndef TREEGIX_TRXEMBED_H
#define TREEGIX_TRXEMBED_H

typedef struct zbx_es_env zbx_es_env_t;

typedef struct
{
	zbx_es_env_t	*env;
}
zbx_es_t;

void	zbx_es_init(zbx_es_t *es);
void	zbx_es_destroy(zbx_es_t *es);
int	zbx_es_init_env(zbx_es_t *es, char **error);
int	zbx_es_destroy_env(zbx_es_t *es, char **error);
int	zbx_es_is_env_initialized(zbx_es_t *es);
int	zbx_es_fatal_error(zbx_es_t *es);
int	zbx_es_compile(zbx_es_t *es, const char *script, char **code, int *size, char **error);
int	zbx_es_execute(zbx_es_t *es, const char *script, const char *code, int size, const char *param, char **output,
	char **error);
void	zbx_es_set_timeout(zbx_es_t *es, int timeout);

#endif /* TREEGIX_TRXEMBED_H */
