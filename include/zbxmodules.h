
#ifndef TREEGIX_ZBXMODULES_H
#define TREEGIX_ZBXMODULES_H

typedef struct
{
	void	*lib;
	char	*name;
}
zbx_module_t;

typedef struct
{
	zbx_module_t	*module;
	void		(*history_float_cb)(const ZBX_HISTORY_FLOAT *, int);
}
zbx_history_float_cb_t;

typedef struct
{
	zbx_module_t	*module;
	void		(*history_integer_cb)(const ZBX_HISTORY_INTEGER *, int);
}
zbx_history_integer_cb_t;

typedef struct
{
	zbx_module_t	*module;
	void		(*history_string_cb)(const ZBX_HISTORY_STRING *, int);
}
zbx_history_string_cb_t;

typedef struct
{
	zbx_module_t	*module;
	void		(*history_text_cb)(const ZBX_HISTORY_TEXT *, int);
}
zbx_history_text_cb_t;

typedef struct
{
	zbx_module_t	*module;
	void		(*history_log_cb)(const ZBX_HISTORY_LOG *, int);
}
zbx_history_log_cb_t;

extern zbx_history_float_cb_t	*history_float_cbs;
extern zbx_history_integer_cb_t	*history_integer_cbs;
extern zbx_history_string_cb_t	*history_string_cbs;
extern zbx_history_text_cb_t	*history_text_cbs;
extern zbx_history_log_cb_t	*history_log_cbs;

int	zbx_load_modules(const char *path, char **file_names, int timeout, int verbose);
void	zbx_unload_modules(void);

#endif
