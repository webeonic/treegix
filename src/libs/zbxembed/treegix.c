
#include "common.h"
#include "log.h"
#include "zbxjson.h"
#include "zbxembed.h"
#include "embed.h"
#include "duktape.h"
#include "treegix.h"

/******************************************************************************
 *                                                                            *
 * Function: es_treegix_dtor                                              *
 *                                                                            *
 * Purpose: Curltreegix destructor                                        *
 *                                                                            *
 ******************************************************************************/
static duk_ret_t	es_treegix_dtor(duk_context *ctx)
{
	TRX_UNUSED(ctx);
	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: es_treegix_ctor                                              *
 *                                                                            *
 * Purpose: Curltreegix constructor                                       *
 *                                                                            *
 ******************************************************************************/
static duk_ret_t	es_treegix_ctor(duk_context *ctx)
{
	if (!duk_is_constructor_call(ctx))
		return DUK_RET_TYPE_ERROR;

	duk_push_this(ctx);

	duk_push_c_function(ctx, es_treegix_dtor, 1);
	duk_set_finalizer(ctx, -2);
	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: es_treegix_status                                            *
 *                                                                            *
 * Purpose: Curltreegix.Status method                                     *
 *                                                                            *
 ******************************************************************************/
static duk_ret_t	es_treegix_log(duk_context *ctx)
{
	treegix_log(duk_to_int(ctx, 0), "%s", duk_to_string(ctx, 1));
	return 0;
}

static const duk_function_list_entry	treegix_methods[] = {
	{"Log", es_treegix_log, 2},
	{NULL, NULL, 0}
};

static int	es_treegix_create_object(duk_context *ctx)
{
	duk_push_c_function(ctx, es_treegix_ctor, 0);
	duk_push_object(ctx);

	duk_put_function_list(ctx, -1, treegix_methods);

	if (1 != duk_put_prop_string(ctx, -2, "prototype"))
		return FAIL;

	duk_new(ctx, 0);
	duk_put_global_string(ctx, "Treegix");

	return SUCCEED;
}

int	zbx_es_init_treegix(zbx_es_t *es, char **error)
{
	if (0 != setjmp(es->env->loc))
	{
		*error = zbx_strdup(*error, es->env->error);
		return FAIL;
	}

	if (FAIL == es_treegix_create_object(es->env->ctx))
	{
		*error = zbx_strdup(*error, duk_safe_to_string(es->env->ctx, -1));
		duk_pop(es->env->ctx);
		return FAIL;
	}

	return SUCCEED;
}
