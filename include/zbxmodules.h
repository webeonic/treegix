
#ifndef TREEGIX_TRXMODULES_H
#define TREEGIX_TRXMODULES_H

typedef struct
{
	void	*lib;
	char	*name;
}
trx_module_t;

typedef struct
{
	trx_module_t	*module;
	void		(*history_float_cb)(const TRX_HISTORY_FLOAT *, int);
}
trx_history_float_cb_t;

typedef struct
{
	trx_module_t	*module;
	void		(*history_integer_cb)(const TRX_HISTORY_INTEGER *, int);
}
trx_history_integer_cb_t;

typedef struct
{
	trx_module_t	*module;
	void		(*history_string_cb)(const TRX_HISTORY_STRING *, int);
}
trx_history_string_cb_t;

typedef struct
{
	trx_module_t	*module;
	void		(*history_text_cb)(const TRX_HISTORY_TEXT *, int);
}
trx_history_text_cb_t;

typedef struct
{
	trx_module_t	*module;
	void		(*history_log_cb)(const TRX_HISTORY_LOG *, int);
}
trx_history_log_cb_t;

extern trx_history_float_cb_t	*history_float_cbs;
extern trx_history_integer_cb_t	*history_integer_cbs;
extern trx_history_string_cb_t	*history_string_cbs;
extern trx_history_text_cb_t	*history_text_cbs;
extern trx_history_log_cb_t	*history_log_cbs;

int	trx_load_modules(const char *path, char **file_names, int timeout, int verbose);
void	trx_unload_modules(void);

#endif
