

#include "common.h"
#include "module.h"
#include "trxmodules.h"

#include "log.h"
#include "sysinfo.h"
#include "trxalgo.h"

#define TRX_MODULE_FUNC_INIT			"trx_module_init"
#define TRX_MODULE_FUNC_API_VERSION		"trx_module_api_version"
#define TRX_MODULE_FUNC_ITEM_LIST		"trx_module_item_list"
#define TRX_MODULE_FUNC_ITEM_PROCESS		"trx_module_item_process"
#define TRX_MODULE_FUNC_ITEM_TIMEOUT		"trx_module_item_timeout"
#define TRX_MODULE_FUNC_UNINIT			"trx_module_uninit"
#define TRX_MODULE_FUNC_HISTORY_WRITE_CBS	"trx_module_history_write_cbs"

static trx_vector_ptr_t	modules;

trx_history_float_cb_t		*history_float_cbs = NULL;
trx_history_integer_cb_t	*history_integer_cbs = NULL;
trx_history_string_cb_t		*history_string_cbs = NULL;
trx_history_text_cb_t		*history_text_cbs = NULL;
trx_history_log_cb_t		*history_log_cbs = NULL;

/******************************************************************************
 *                                                                            *
 * Function: trx_register_module_items                                        *
 *                                                                            *
 * Purpose: add items supported by module                                     *
 *                                                                            *
 * Parameters: metrics       - list of items supported by module              *
 *             error         - error buffer                                   *
 *             max_error_len - error buffer size                              *
 *                                                                            *
 * Return value: SUCCEED - all module items were added or there were none     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	trx_register_module_items(TRX_METRIC *metrics, char *error, size_t max_error_len)
{
	int	i;

	for (i = 0; NULL != metrics[i].key; i++)
	{
		/* accept only CF_HAVEPARAMS flag from module items */
		metrics[i].flags &= CF_HAVEPARAMS;
		/* the flag means that the items comes from a loadable module */
		metrics[i].flags |= CF_MODULE;

		if (SUCCEED != add_metric(&metrics[i], error, max_error_len))
			return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_register_module                                              *
 *                                                                            *
 * Purpose: add module to the list of successfully loaded modules             *
 *                                                                            *
 ******************************************************************************/
static trx_module_t	*trx_register_module(void *lib, char *name)
{
	trx_module_t	*module;

	module = (trx_module_t *)trx_malloc(NULL, sizeof(trx_module_t));
	module->lib = lib;
	module->name = trx_strdup(NULL, name);
	trx_vector_ptr_append(&modules, module);

	return module;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_register_history_write_cbs                                   *
 *                                                                            *
 * Purpose: registers callback functions for history export                   *
 *                                                                            *
 * Parameters: module            - module pointer for later reference         *
 *             history_write_cbs - callbacks                                  *
 *                                                                            *
 ******************************************************************************/
static void	trx_register_history_write_cbs(trx_module_t *module, TRX_HISTORY_WRITE_CBS history_write_cbs)
{
	if (NULL != history_write_cbs.history_float_cb)
	{
		int	j = 0;

		if (NULL == history_float_cbs)
		{
			history_float_cbs = (trx_history_float_cb_t *)trx_malloc(history_float_cbs, sizeof(trx_history_float_cb_t));
			history_float_cbs[0].module = NULL;
		}

		while (NULL != history_float_cbs[j].module)
			j++;

		history_float_cbs = (trx_history_float_cb_t *)trx_realloc(history_float_cbs, (j + 2) * sizeof(trx_history_float_cb_t));
		history_float_cbs[j].module = module;
		history_float_cbs[j].history_float_cb = history_write_cbs.history_float_cb;
		history_float_cbs[j + 1].module = NULL;
	}

	if (NULL != history_write_cbs.history_integer_cb)
	{
		int	j = 0;

		if (NULL == history_integer_cbs)
		{
			history_integer_cbs = (trx_history_integer_cb_t *)trx_malloc(history_integer_cbs, sizeof(trx_history_integer_cb_t));
			history_integer_cbs[0].module = NULL;
		}

		while (NULL != history_integer_cbs[j].module)
			j++;

		history_integer_cbs = (trx_history_integer_cb_t *)trx_realloc(history_integer_cbs, (j + 2) * sizeof(trx_history_integer_cb_t));
		history_integer_cbs[j].module = module;
		history_integer_cbs[j].history_integer_cb = history_write_cbs.history_integer_cb;
		history_integer_cbs[j + 1].module = NULL;
	}

	if (NULL != history_write_cbs.history_string_cb)
	{
		int	j = 0;

		if (NULL == history_string_cbs)
		{
			history_string_cbs = (trx_history_string_cb_t *)trx_malloc(history_string_cbs, sizeof(trx_history_string_cb_t));
			history_string_cbs[0].module = NULL;
		}

		while (NULL != history_string_cbs[j].module)
			j++;

		history_string_cbs = (trx_history_string_cb_t *)trx_realloc(history_string_cbs, (j + 2) * sizeof(trx_history_string_cb_t));
		history_string_cbs[j].module = module;
		history_string_cbs[j].history_string_cb = history_write_cbs.history_string_cb;
		history_string_cbs[j + 1].module = NULL;
	}

	if (NULL != history_write_cbs.history_text_cb)
	{
		int	j = 0;

		if (NULL == history_text_cbs)
		{
			history_text_cbs = (trx_history_text_cb_t *)trx_malloc(history_text_cbs, sizeof(trx_history_text_cb_t));
			history_text_cbs[0].module = NULL;
		}

		while (NULL != history_text_cbs[j].module)
			j++;

		history_text_cbs = (trx_history_text_cb_t *)trx_realloc(history_text_cbs, (j + 2) * sizeof(trx_history_text_cb_t));
		history_text_cbs[j].module = module;
		history_text_cbs[j].history_text_cb = history_write_cbs.history_text_cb;
		history_text_cbs[j + 1].module = NULL;
	}

	if (NULL != history_write_cbs.history_log_cb)
	{
		int	j = 0;

		if (NULL == history_log_cbs)
		{
			history_log_cbs = (trx_history_log_cb_t *)trx_malloc(history_log_cbs, sizeof(trx_history_log_cb_t));
			history_log_cbs[0].module = NULL;
		}

		while (NULL != history_log_cbs[j].module)
			j++;

		history_log_cbs = (trx_history_log_cb_t *)trx_realloc(history_log_cbs, (j + 2) * sizeof(trx_history_log_cb_t));
		history_log_cbs[j].module = module;
		history_log_cbs[j].history_log_cb = history_write_cbs.history_log_cb;
		history_log_cbs[j + 1].module = NULL;
	}
}

static int	trx_module_compare_func(const void *d1, const void *d2)
{
	const trx_module_t	*m1 = *(const trx_module_t **)d1;
	const trx_module_t	*m2 = *(const trx_module_t **)d2;

	TRX_RETURN_IF_NOT_EQUAL(m1->lib, m2->lib);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_load_module                                                  *
 *                                                                            *
 * Purpose: load loadable module                                              *
 *                                                                            *
 * Parameters: path    - directory where modules are located                  *
 *             name    - module name                                          *
 *             timeout - timeout in seconds for processing of items by module *
 *                                                                            *
 * Return value: SUCCEED - module was successfully loaded or found amongst    *
 *                         previously loaded                                  *
 *               FAIL    - loading of module failed                           *
 *                                                                            *
 ******************************************************************************/
static int	trx_load_module(const char *path, char *name, int timeout)
{
	void			*lib;
	char			full_name[MAX_STRING_LEN], error[MAX_STRING_LEN];
	int			(*func_init)(void), (*func_version)(void), version;
	TRX_METRIC		*(*func_list)(void);
	void			(*func_timeout)(int);
	TRX_HISTORY_WRITE_CBS	(*func_history_write_cbs)(void);
	trx_module_t		*module, module_tmp;

	if ('/' != *name)
		trx_snprintf(full_name, sizeof(full_name), "%s/%s", path, name);
	else
		trx_snprintf(full_name, sizeof(full_name), "%s", name);

	treegix_log(LOG_LEVEL_DEBUG, "loading module \"%s\"", full_name);

	if (NULL == (lib = dlopen(full_name, RTLD_NOW)))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot load module \"%s\": %s", name, dlerror());
		return FAIL;
	}

	module_tmp.lib = lib;
	if (FAIL != trx_vector_ptr_search(&modules, &module_tmp, trx_module_compare_func))
	{
		treegix_log(LOG_LEVEL_DEBUG, "module \"%s\" has already beed loaded", name);
		return SUCCEED;
	}

	if (NULL == (func_version = (int (*)(void))dlsym(lib, TRX_MODULE_FUNC_API_VERSION)))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot find \"" TRX_MODULE_FUNC_API_VERSION "()\""
				" function in module \"%s\": %s", name, dlerror());
		goto fail;
	}

	if (TRX_MODULE_API_VERSION != (version = func_version()))
	{
		treegix_log(LOG_LEVEL_CRIT, "unsupported module \"%s\" version: %d", name, version);
		goto fail;
	}

	if (NULL == (func_init = (int (*)(void))dlsym(lib, TRX_MODULE_FUNC_INIT)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "cannot find \"" TRX_MODULE_FUNC_INIT "()\""
				" function in module \"%s\": %s", name, dlerror());
	}
	else if (TRX_MODULE_OK != func_init())
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot initialize module \"%s\"", name);
		goto fail;
	}

	if (NULL == (func_list = (TRX_METRIC *(*)(void))dlsym(lib, TRX_MODULE_FUNC_ITEM_LIST)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "cannot find \"" TRX_MODULE_FUNC_ITEM_LIST "()\""
				" function in module \"%s\": %s", name, dlerror());
	}
	else
	{
		if (SUCCEED != trx_register_module_items(func_list(), error, sizeof(error)))
		{
			treegix_log(LOG_LEVEL_CRIT, "cannot load module \"%s\": %s", name, error);
			goto fail;
		}

		if (NULL == (func_timeout = (void (*)(int))dlsym(lib, TRX_MODULE_FUNC_ITEM_TIMEOUT)))
		{
			treegix_log(LOG_LEVEL_DEBUG, "cannot find \"" TRX_MODULE_FUNC_ITEM_TIMEOUT "()\""
					" function in module \"%s\": %s", name, dlerror());
		}
		else
			func_timeout(timeout);
	}

	/* module passed validation and can now be registered */
	module = trx_register_module(lib, name);

	if (NULL == (func_history_write_cbs = (TRX_HISTORY_WRITE_CBS (*)(void))dlsym(lib,
			TRX_MODULE_FUNC_HISTORY_WRITE_CBS)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "cannot find \"" TRX_MODULE_FUNC_HISTORY_WRITE_CBS "()\""
				" function in module \"%s\": %s", name, dlerror());
	}
	else
		trx_register_history_write_cbs(module, func_history_write_cbs());

	return SUCCEED;
fail:
	dlclose(lib);

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_load_modules                                                 *
 *                                                                            *
 * Purpose: load loadable modules (dynamic libraries)                         *
 *                                                                            *
 * Parameters: path - directory where modules are located                     *
 *             file_names - list of module names                              *
 *             timeout - timeout in seconds for processing of items by module *
 *             verbose - output list of loaded modules                        *
 *                                                                            *
 * Return value: SUCCEED - all modules are successfully loaded                *
 *               FAIL - loading of modules failed                             *
 *                                                                            *
 ******************************************************************************/
int	trx_load_modules(const char *path, char **file_names, int timeout, int verbose)
{
	char	**file_name;
	int	ret = SUCCEED;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&modules);

	if (NULL == *file_names)
		goto out;

	for (file_name = file_names; NULL != *file_name; file_name++)
	{
		if (SUCCEED != (ret = trx_load_module(path, *file_name, timeout)))
			goto out;
	}

	if (0 != verbose)
	{
		char	*buffer;
		int	i = 0;

		/* if execution reached this point at least one module was loaded successfully */
		buffer = trx_strdcat(NULL, ((trx_module_t *)modules.values[i++])->name);

		while (i < modules.values_num)
		{
			buffer = trx_strdcat(buffer, ", ");
			buffer = trx_strdcat(buffer, ((trx_module_t *)modules.values[i++])->name);
		}

		treegix_log(LOG_LEVEL_WARNING, "loaded modules: %s", buffer);
		trx_free(buffer);
	}
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_unload_module                                                *
 *                                                                            *
 * Purpose: unload module and free allocated resources                        *
 *                                                                            *
 ******************************************************************************/
static void	trx_unload_module(void *data)
{
	trx_module_t	*module = (trx_module_t *)data;
	int		(*func_uninit)(void);

	if (NULL == (func_uninit = (int (*)(void))dlsym(module->lib, TRX_MODULE_FUNC_UNINIT)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "cannot find \"" TRX_MODULE_FUNC_UNINIT "()\""
				" function in module \"%s\": %s", module->name, dlerror());
	}
	else if (TRX_MODULE_OK != func_uninit())
		treegix_log(LOG_LEVEL_WARNING, "uninitialization of module \"%s\" failed", module->name);

	dlclose(module->lib);
	trx_free(module->name);
	trx_free(module);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_unload_modules                                               *
 *                                                                            *
 * Purpose: Unload already loaded loadable modules (dynamic libraries).       *
 *          It is called on process shutdown.                                 *
 *                                                                            *
 ******************************************************************************/
void	trx_unload_modules(void)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_free(history_float_cbs);
	trx_free(history_integer_cbs);
	trx_free(history_string_cbs);
	trx_free(history_text_cbs);
	trx_free(history_log_cbs);

	trx_vector_ptr_clear_ext(&modules, trx_unload_module);
	trx_vector_ptr_destroy(&modules);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
