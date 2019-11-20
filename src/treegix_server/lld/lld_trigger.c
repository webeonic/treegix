

#include "lld.h"
#include "db.h"
#include "log.h"
#include "trxalgo.h"
#include "trxserver.h"

typedef struct
{
	trx_uint64_t		triggerid;
	char			*description;
	char			*expression;
	char			*recovery_expression;
	char			*comments;
	char			*url;
	char			*correlation_tag;
	char			*opdata;
	unsigned char		status;
	unsigned char		type;
	unsigned char		priority;
	unsigned char		recovery_mode;
	unsigned char		correlation_mode;
	unsigned char		manual_close;
	trx_vector_ptr_t	functions;
	trx_vector_ptr_t	dependencies;
	trx_vector_ptr_t	tags;
}
trx_lld_trigger_prototype_t;

typedef struct
{
	trx_uint64_t		triggerid;
	trx_uint64_t		parent_triggerid;
	char			*description;
	char			*description_orig;
	char			*expression;
	char			*expression_orig;
	char			*recovery_expression;
	char			*recovery_expression_orig;
	char			*comments;
	char			*comments_orig;
	char			*url;
	char			*url_orig;
	char			*correlation_tag;
	char			*correlation_tag_orig;
	char			*opdata;
	char			*opdata_orig;
	trx_vector_ptr_t	functions;
	trx_vector_ptr_t	dependencies;
	trx_vector_ptr_t	dependents;
	trx_vector_ptr_t	tags;
#define TRX_FLAG_LLD_TRIGGER_UNSET			__UINT64_C(0x0000)
#define TRX_FLAG_LLD_TRIGGER_DISCOVERED			__UINT64_C(0x0001)
#define TRX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION		__UINT64_C(0x0002)
#define TRX_FLAG_LLD_TRIGGER_UPDATE_EXPRESSION		__UINT64_C(0x0004)
#define TRX_FLAG_LLD_TRIGGER_UPDATE_TYPE		__UINT64_C(0x0008)
#define TRX_FLAG_LLD_TRIGGER_UPDATE_PRIORITY		__UINT64_C(0x0010)
#define TRX_FLAG_LLD_TRIGGER_UPDATE_COMMENTS		__UINT64_C(0x0020)
#define TRX_FLAG_LLD_TRIGGER_UPDATE_URL			__UINT64_C(0x0040)
#define TRX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_EXPRESSION	__UINT64_C(0x0080)
#define TRX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_MODE	__UINT64_C(0x0100)
#define TRX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_MODE	__UINT64_C(0x0200)
#define TRX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_TAG	__UINT64_C(0x0400)
#define TRX_FLAG_LLD_TRIGGER_UPDATE_MANUAL_CLOSE	__UINT64_C(0x0800)
#define TRX_FLAG_LLD_TRIGGER_UPDATE_OPDATA		__UINT64_C(0x1000)
#define TRX_FLAG_LLD_TRIGGER_UPDATE										\
		(TRX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION | TRX_FLAG_LLD_TRIGGER_UPDATE_EXPRESSION |		\
		TRX_FLAG_LLD_TRIGGER_UPDATE_TYPE | TRX_FLAG_LLD_TRIGGER_UPDATE_PRIORITY |			\
		TRX_FLAG_LLD_TRIGGER_UPDATE_COMMENTS | TRX_FLAG_LLD_TRIGGER_UPDATE_URL |			\
		TRX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_EXPRESSION | TRX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_MODE |	\
		TRX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_MODE | TRX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_TAG |	\
		TRX_FLAG_LLD_TRIGGER_UPDATE_MANUAL_CLOSE | TRX_FLAG_LLD_TRIGGER_UPDATE_OPDATA)
	trx_uint64_t		flags;
}
trx_lld_trigger_t;

typedef struct
{
	trx_uint64_t	functionid;
	trx_uint64_t	index;
	trx_uint64_t	itemid;
	trx_uint64_t	itemid_orig;
	char		*function;
	char		*function_orig;
	char		*parameter;
	char		*parameter_orig;
#define TRX_FLAG_LLD_FUNCTION_UNSET			__UINT64_C(0x00)
#define TRX_FLAG_LLD_FUNCTION_DISCOVERED		__UINT64_C(0x01)
#define TRX_FLAG_LLD_FUNCTION_UPDATE_ITEMID		__UINT64_C(0x02)
#define TRX_FLAG_LLD_FUNCTION_UPDATE_FUNCTION		__UINT64_C(0x04)
#define TRX_FLAG_LLD_FUNCTION_UPDATE_PARAMETER		__UINT64_C(0x08)
#define TRX_FLAG_LLD_FUNCTION_UPDATE								\
		(TRX_FLAG_LLD_FUNCTION_UPDATE_ITEMID | TRX_FLAG_LLD_FUNCTION_UPDATE_FUNCTION |	\
		TRX_FLAG_LLD_FUNCTION_UPDATE_PARAMETER)
#define TRX_FLAG_LLD_FUNCTION_DELETE			__UINT64_C(0x10)
	trx_uint64_t	flags;
}
trx_lld_function_t;

typedef struct
{
	trx_uint64_t		triggerdepid;
	trx_uint64_t		triggerid_up;	/* generic trigger */
	trx_lld_trigger_t	*trigger_up;	/* lld-created trigger; (null) if trigger depends on generic trigger */
#define TRX_FLAG_LLD_DEPENDENCY_UNSET			__UINT64_C(0x00)
#define TRX_FLAG_LLD_DEPENDENCY_DISCOVERED		__UINT64_C(0x01)
#define TRX_FLAG_LLD_DEPENDENCY_DELETE			__UINT64_C(0x02)
	trx_uint64_t		flags;
}
trx_lld_dependency_t;

typedef struct
{
	trx_uint64_t	triggertagid;
	char		*tag;
	char		*value;
#define TRX_FLAG_LLD_TAG_UNSET				__UINT64_C(0x00)
#define TRX_FLAG_LLD_TAG_DISCOVERED			__UINT64_C(0x01)
#define TRX_FLAG_LLD_TAG_UPDATE_TAG			__UINT64_C(0x02)
#define TRX_FLAG_LLD_TAG_UPDATE_VALUE			__UINT64_C(0x04)
#define TRX_FLAG_LLD_TAG_UPDATE							\
		(TRX_FLAG_LLD_TAG_UPDATE_TAG | TRX_FLAG_LLD_TAG_UPDATE_VALUE)
#define TRX_FLAG_LLD_TAG_DELETE				__UINT64_C(0x08)
	trx_uint64_t	flags;
}
trx_lld_tag_t;

typedef struct
{
	trx_uint64_t		parent_triggerid;
	trx_uint64_t		itemid;
	trx_lld_trigger_t	*trigger;
}
trx_lld_item_trigger_t;

typedef struct
{
	trx_uint64_t	itemid;
	unsigned char	flags;
}
trx_lld_item_t;

/* a reference to trigger which could be either existing trigger in database or */
/* a just discovered trigger stored in memory                                   */
typedef struct
{
	/* trigger id, 0 for newly discovered triggers */
	trx_uint64_t		triggerid;

	/* trigger data, NULL for non-discovered triggers */
	trx_lld_trigger_t	*trigger;

	/* flags to mark trigger dependencies during trigger dependency validation */
#define TRX_LLD_TRIGGER_DEPENDENCY_NORMAL	0
#define TRX_LLD_TRIGGER_DEPENDENCY_NEW		1
#define TRX_LLD_TRIGGER_DEPENDENCY_DELETE	2

	/* flags used to mark dependencies when trigger reference is use to store dependency links */
	int			flags;
}
trx_lld_trigger_ref_t;

/* a trigger node used to build trigger tree for dependency validation */
typedef struct
{
	/* trigger reference */
	trx_lld_trigger_ref_t	trigger_ref;

	/* the current iteration number, used during dependency validation */
	int			iter_num;

	/* the number of dependents */
	int			parents;

	/* trigger dependency list */
	trx_vector_ptr_t	dependencies;
}
trx_lld_trigger_node_t;

/* a structure to keep information about current iteration during trigger dependencies validation */
typedef struct
{
	/* iteration number */
	int			iter_num;

	/* the dependency (from->to) that should be removed in the case of recursive loop */
	trx_lld_trigger_ref_t	*ref_from;
	trx_lld_trigger_ref_t	*ref_to;
}
trx_lld_trigger_node_iter_t;


static void	lld_tag_free(trx_lld_tag_t *tag)
{
	trx_free(tag->tag);
	trx_free(tag->value);
	trx_free(tag);
}

static void	lld_item_free(trx_lld_item_t *item)
{
	trx_free(item);
}

static void	lld_function_free(trx_lld_function_t *function)
{
	trx_free(function->parameter_orig);
	trx_free(function->parameter);
	trx_free(function->function_orig);
	trx_free(function->function);
	trx_free(function);
}

static void	lld_trigger_prototype_free(trx_lld_trigger_prototype_t *trigger_prototype)
{
	trx_vector_ptr_clear_ext(&trigger_prototype->tags, (trx_clean_func_t)lld_tag_free);
	trx_vector_ptr_destroy(&trigger_prototype->tags);
	trx_vector_ptr_clear_ext(&trigger_prototype->dependencies, trx_ptr_free);
	trx_vector_ptr_destroy(&trigger_prototype->dependencies);
	trx_vector_ptr_clear_ext(&trigger_prototype->functions, (trx_mem_free_func_t)lld_function_free);
	trx_vector_ptr_destroy(&trigger_prototype->functions);
	trx_free(trigger_prototype->opdata);
	trx_free(trigger_prototype->correlation_tag);
	trx_free(trigger_prototype->url);
	trx_free(trigger_prototype->comments);
	trx_free(trigger_prototype->recovery_expression);
	trx_free(trigger_prototype->expression);
	trx_free(trigger_prototype->description);
	trx_free(trigger_prototype);
}

static void	lld_trigger_free(trx_lld_trigger_t *trigger)
{
	trx_vector_ptr_clear_ext(&trigger->tags, (trx_clean_func_t)lld_tag_free);
	trx_vector_ptr_destroy(&trigger->tags);
	trx_vector_ptr_destroy(&trigger->dependents);
	trx_vector_ptr_clear_ext(&trigger->dependencies, trx_ptr_free);
	trx_vector_ptr_destroy(&trigger->dependencies);
	trx_vector_ptr_clear_ext(&trigger->functions, (trx_clean_func_t)lld_function_free);
	trx_vector_ptr_destroy(&trigger->functions);
	trx_free(trigger->opdata_orig);
	trx_free(trigger->opdata);
	trx_free(trigger->correlation_tag_orig);
	trx_free(trigger->correlation_tag);
	trx_free(trigger->url_orig);
	trx_free(trigger->url);
	trx_free(trigger->comments_orig);
	trx_free(trigger->comments);
	trx_free(trigger->recovery_expression_orig);
	trx_free(trigger->recovery_expression);
	trx_free(trigger->expression_orig);
	trx_free(trigger->expression);
	trx_free(trigger->description_orig);
	trx_free(trigger->description);
	trx_free(trigger);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_prototypes_get                                       *
 *                                                                            *
 * Purpose: retrieve trigger prototypes which are inherited from the          *
 *          discovery rule                                                    *
 *                                                                            *
 * Parameters: lld_ruleid         - [IN] discovery rule id                    *
 *             trigger_prototypes - [OUT] sorted list of trigger prototypes   *
 *                                                                            *
 ******************************************************************************/
static void	lld_trigger_prototypes_get(trx_uint64_t lld_ruleid, trx_vector_ptr_t *trigger_prototypes)
{
	DB_RESULT			result;
	DB_ROW				row;
	trx_lld_trigger_prototype_t	*trigger_prototype;

	result = DBselect(
			"select distinct t.triggerid,t.description,t.expression,t.status,t.type,t.priority,t.comments,"
				"t.url,t.recovery_expression,t.recovery_mode,t.correlation_mode,t.correlation_tag,"
				"t.manual_close,t.opdata"
			" from triggers t,functions f,items i,item_discovery id"
			" where t.triggerid=f.triggerid"
				" and f.itemid=i.itemid"
				" and i.itemid=id.itemid"
				" and id.parent_itemid=" TRX_FS_UI64,
			lld_ruleid);

	/* run through trigger prototypes */
	while (NULL != (row = DBfetch(result)))
	{
		trigger_prototype = (trx_lld_trigger_prototype_t *)trx_malloc(NULL, sizeof(trx_lld_trigger_prototype_t));

		TRX_STR2UINT64(trigger_prototype->triggerid, row[0]);
		trigger_prototype->description = trx_strdup(NULL, row[1]);
		trigger_prototype->expression = trx_strdup(NULL, row[2]);
		trigger_prototype->recovery_expression = trx_strdup(NULL, row[8]);
		TRX_STR2UCHAR(trigger_prototype->status, row[3]);
		TRX_STR2UCHAR(trigger_prototype->type, row[4]);
		TRX_STR2UCHAR(trigger_prototype->priority, row[5]);
		TRX_STR2UCHAR(trigger_prototype->recovery_mode, row[9]);
		trigger_prototype->comments = trx_strdup(NULL, row[6]);
		trigger_prototype->url = trx_strdup(NULL, row[7]);
		TRX_STR2UCHAR(trigger_prototype->correlation_mode, row[10]);
		trigger_prototype->correlation_tag = trx_strdup(NULL, row[11]);
		TRX_STR2UCHAR(trigger_prototype->manual_close, row[12]);
		trigger_prototype->opdata = trx_strdup(NULL, row[13]);

		trx_vector_ptr_create(&trigger_prototype->functions);
		trx_vector_ptr_create(&trigger_prototype->dependencies);
		trx_vector_ptr_create(&trigger_prototype->tags);

		trx_vector_ptr_append(trigger_prototypes, trigger_prototype);
	}
	DBfree_result(result);

	trx_vector_ptr_sort(trigger_prototypes, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_triggers_get                                                 *
 *                                                                            *
 * Purpose: retrieve triggers which were created by the specified trigger     *
 *          prototypes                                                        *
 *                                                                            *
 * Parameters: trigger_prototypes - [IN] sorted list of trigger prototypes    *
 *             triggers           - [OUT] sorted list of triggers             *
 *                                                                            *
 ******************************************************************************/
static void	lld_triggers_get(const trx_vector_ptr_t *trigger_prototypes, trx_vector_ptr_t *triggers)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_vector_uint64_t	parent_triggerids;
	char			*sql = NULL;
	size_t			sql_alloc = 256, sql_offset = 0;
	int			i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&parent_triggerids);
	trx_vector_uint64_reserve(&parent_triggerids, trigger_prototypes->values_num);

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		const trx_lld_trigger_prototype_t	*trigger_prototype;

		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		trx_vector_uint64_append(&parent_triggerids, trigger_prototype->triggerid);
	}

	sql = (char *)trx_malloc(sql, sql_alloc);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select td.parent_triggerid,t.triggerid,t.description,t.expression,t.type,t.priority,"
				"t.comments,t.url,t.recovery_expression,t.recovery_mode,t.correlation_mode,"
				"t.correlation_tag,t.manual_close,t.opdata"
			" from triggers t,trigger_discovery td"
			" where t.triggerid=td.triggerid"
				" and");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "td.parent_triggerid",
			parent_triggerids.values, parent_triggerids.values_num);

	trx_vector_uint64_destroy(&parent_triggerids);

	result = DBselect("%s", sql);

	trx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		trx_uint64_t				parent_triggerid;
		const trx_lld_trigger_prototype_t	*trigger_prototype;
		trx_lld_trigger_t			*trigger;
		int					index;

		TRX_STR2UINT64(parent_triggerid, row[0]);

		if (FAIL == (index = trx_vector_ptr_bsearch(trigger_prototypes, &parent_triggerid,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[index];

		trigger = (trx_lld_trigger_t *)trx_malloc(NULL, sizeof(trx_lld_trigger_t));

		TRX_STR2UINT64(trigger->triggerid, row[1]);
		trigger->parent_triggerid = parent_triggerid;
		trigger->description = trx_strdup(NULL, row[2]);
		trigger->description_orig = NULL;
		trigger->expression = trx_strdup(NULL, row[3]);
		trigger->expression_orig = NULL;
		trigger->recovery_expression = trx_strdup(NULL, row[8]);
		trigger->recovery_expression_orig = NULL;

		trigger->flags = TRX_FLAG_LLD_TRIGGER_UNSET;

		if ((unsigned char)atoi(row[4]) != trigger_prototype->type)
			trigger->flags |= TRX_FLAG_LLD_TRIGGER_UPDATE_TYPE;

		if ((unsigned char)atoi(row[5]) != trigger_prototype->priority)
			trigger->flags |= TRX_FLAG_LLD_TRIGGER_UPDATE_PRIORITY;

		if ((unsigned char)atoi(row[9]) != trigger_prototype->recovery_mode)
			trigger->flags |= TRX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_MODE;

		if ((unsigned char)atoi(row[10]) != trigger_prototype->correlation_mode)
			trigger->flags |= TRX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_MODE;

		if ((unsigned char)atoi(row[12]) != trigger_prototype->manual_close)
			trigger->flags |= TRX_FLAG_LLD_TRIGGER_UPDATE_MANUAL_CLOSE;

		trigger->comments = trx_strdup(NULL, row[6]);
		trigger->comments_orig = NULL;
		trigger->url = trx_strdup(NULL, row[7]);
		trigger->url_orig = NULL;
		trigger->correlation_tag = trx_strdup(NULL, row[11]);
		trigger->correlation_tag_orig = NULL;
		trigger->opdata = trx_strdup(NULL, row[13]);
		trigger->opdata_orig = NULL;

		trx_vector_ptr_create(&trigger->functions);
		trx_vector_ptr_create(&trigger->dependencies);
		trx_vector_ptr_create(&trigger->dependents);
		trx_vector_ptr_create(&trigger->tags);

		trx_vector_ptr_append(triggers, trigger);
	}
	DBfree_result(result);

	trx_vector_ptr_sort(triggers, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_functions_get                                                *
 *                                                                            *
 * Purpose: retrieve functions which are used by all triggers in the host of  *
 *          the trigger prototype                                             *
 *                                                                            *
 ******************************************************************************/
static void	lld_functions_get(trx_vector_ptr_t *trigger_prototypes, trx_vector_ptr_t *triggers)
{
	int				i;
	trx_lld_trigger_prototype_t	*trigger_prototype;
	trx_lld_trigger_t		*trigger;
	trx_vector_uint64_t		triggerids;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&triggerids);

	if (NULL != trigger_prototypes)
	{
		for (i = 0; i < trigger_prototypes->values_num; i++)
		{
			trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

			trx_vector_uint64_append(&triggerids, trigger_prototype->triggerid);
		}
	}

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		trx_vector_uint64_append(&triggerids, trigger->triggerid);
	}

	if (0 != triggerids.values_num)
	{
		DB_RESULT		result;
		DB_ROW			row;
		trx_lld_function_t	*function;
		trx_uint64_t		triggerid;
		char			*sql = NULL;
		size_t			sql_alloc = 256, sql_offset = 0;
		int			index;

		trx_vector_uint64_sort(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		sql = (char *)trx_malloc(sql, sql_alloc);

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select functionid,triggerid,itemid,name,parameter"
				" from functions"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid",
				triggerids.values, triggerids.values_num);

		result = DBselect("%s", sql);

		trx_free(sql);

		while (NULL != (row = DBfetch(result)))
		{
			function = (trx_lld_function_t *)trx_malloc(NULL, sizeof(trx_lld_function_t));

			function->index = 0;
			TRX_STR2UINT64(function->functionid, row[0]);
			TRX_STR2UINT64(triggerid, row[1]);
			TRX_STR2UINT64(function->itemid, row[2]);
			function->itemid_orig = 0;
			function->function = trx_strdup(NULL, row[3]);
			function->function_orig = NULL;
			function->parameter = trx_strdup(NULL, row[4]);
			function->parameter_orig = NULL;
			function->flags = TRX_FLAG_LLD_FUNCTION_UNSET;

			if (NULL != trigger_prototypes && FAIL != (index = trx_vector_ptr_bsearch(trigger_prototypes,
					&triggerid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[index];

				trx_vector_ptr_append(&trigger_prototype->functions, function);
			}
			else if (FAIL != (index = trx_vector_ptr_bsearch(triggers, &triggerid,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				trigger = (trx_lld_trigger_t *)triggers->values[index];

				trx_vector_ptr_append(&trigger->functions, function);
			}
			else
			{
				THIS_SHOULD_NEVER_HAPPEN;
				lld_function_free(function);
			}
		}
		DBfree_result(result);

		if (NULL != trigger_prototypes)
		{
			for (i = 0; i < trigger_prototypes->values_num; i++)
			{
				trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

				trx_vector_ptr_sort(&trigger_prototype->functions, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
			}
		}

		for (i = 0; i < triggers->values_num; i++)
		{
			trigger = (trx_lld_trigger_t *)triggers->values[i];

			trx_vector_ptr_sort(&trigger->functions, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		}
	}

	trx_vector_uint64_destroy(&triggerids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_dependencies_get                                             *
 *                                                                            *
 * Purpose: retrieve trigger dependencies                                     *
 *                                                                            *
 ******************************************************************************/
static void	lld_dependencies_get(trx_vector_ptr_t *trigger_prototypes, trx_vector_ptr_t *triggers)
{
	DB_RESULT			result;
	DB_ROW				row;
	trx_lld_trigger_prototype_t	*trigger_prototype;
	trx_lld_trigger_t		*trigger;
	trx_lld_dependency_t		*dependency;
	trx_vector_uint64_t		triggerids;
	trx_uint64_t			triggerid_down;
	char				*sql = NULL;
	size_t				sql_alloc = 256, sql_offset = 0;
	int				i, index;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&triggerids);

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		trx_vector_uint64_append(&triggerids, trigger_prototype->triggerid);
	}

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		trx_vector_uint64_append(&triggerids, trigger->triggerid);
	}

	trx_vector_uint64_sort(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	sql = (char *)trx_malloc(sql, sql_alloc);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select triggerdepid,triggerid_down,triggerid_up"
			" from trigger_depends"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid_down",
			triggerids.values, triggerids.values_num);

	trx_vector_uint64_destroy(&triggerids);

	result = DBselect("%s", sql);

	trx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		dependency = (trx_lld_dependency_t *)trx_malloc(NULL, sizeof(trx_lld_dependency_t));

		TRX_STR2UINT64(dependency->triggerdepid, row[0]);
		TRX_STR2UINT64(triggerid_down, row[1]);
		TRX_STR2UINT64(dependency->triggerid_up, row[2]);
		dependency->trigger_up = NULL;
		dependency->flags = TRX_FLAG_LLD_DEPENDENCY_UNSET;

		if (FAIL != (index = trx_vector_ptr_bsearch(trigger_prototypes, &triggerid_down,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[index];

			trx_vector_ptr_append(&trigger_prototype->dependencies, dependency);
		}
		else if (FAIL != (index = trx_vector_ptr_bsearch(triggers, &triggerid_down,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			trigger = (trx_lld_trigger_t *)triggers->values[index];

			trx_vector_ptr_append(&trigger->dependencies, dependency);
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
			trx_ptr_free(dependency);
		}
	}
	DBfree_result(result);

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		trx_vector_ptr_sort(&trigger_prototype->dependencies, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		trx_vector_ptr_sort(&trigger->dependencies, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_tags_get                                                     *
 *                                                                            *
 * Purpose: retrieve trigger tags                                             *
 *                                                                            *
 ******************************************************************************/
static void	lld_tags_get(trx_vector_ptr_t *trigger_prototypes, trx_vector_ptr_t *triggers)
{
	DB_RESULT			result;
	DB_ROW				row;
	trx_vector_uint64_t		triggerids;
	int				i, index;
	trx_lld_trigger_prototype_t	*trigger_prototype;
	trx_lld_trigger_t		*trigger;
	trx_lld_tag_t			*tag;
	char				*sql = NULL;
	size_t				sql_alloc = 256, sql_offset = 0;
	trx_uint64_t			triggerid;

	trx_vector_uint64_create(&triggerids);

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		trx_vector_uint64_append(&triggerids, trigger_prototype->triggerid);
	}

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		trx_vector_uint64_append(&triggerids, trigger->triggerid);
	}

	trx_vector_uint64_sort(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	sql = (char *)trx_malloc(sql, sql_alloc);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select triggertagid,triggerid,tag,value"
			" from trigger_tag"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid",
			triggerids.values, triggerids.values_num);

	trx_vector_uint64_destroy(&triggerids);

	result = DBselect("%s", sql);

	trx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		tag = (trx_lld_tag_t *)trx_malloc(NULL, sizeof(trx_lld_tag_t));

		TRX_STR2UINT64(tag->triggertagid, row[0]);
		TRX_STR2UINT64(triggerid, row[1]);
		tag->tag = trx_strdup(NULL, row[2]);
		tag->value = trx_strdup(NULL, row[3]);
		tag->flags = TRX_FLAG_LLD_DEPENDENCY_UNSET;

		if (FAIL != (index = trx_vector_ptr_bsearch(trigger_prototypes, &triggerid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[index];

			trx_vector_ptr_append(&trigger_prototype->tags, tag);
		}
		else if (FAIL != (index = trx_vector_ptr_bsearch(triggers, &triggerid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			trigger = (trx_lld_trigger_t *)triggers->values[index];

			trx_vector_ptr_append(&trigger->tags, tag);
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
			trx_ptr_free(tag);
		}

	}
	DBfree_result(result);

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		trx_vector_ptr_sort(&trigger_prototype->tags, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		trx_vector_ptr_sort(&trigger->tags, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_get                                                    *
 *                                                                            *
 * Purpose: returns the list of items which are related to the trigger        *
 *          prototypes                                                        *
 *                                                                            *
 * Parameters: trigger_prototypes - [IN] a vector of trigger prototypes       *
 *             items              - [OUT] sorted list of items                *
 *                                                                            *
 ******************************************************************************/
static void	lld_items_get(trx_vector_ptr_t *trigger_prototypes, trx_vector_ptr_t *items)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_lld_item_t		*item;
	trx_vector_uint64_t	parent_triggerids;
	char			*sql = NULL;
	size_t			sql_alloc = 256, sql_offset = 0;
	int			i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&parent_triggerids);
	trx_vector_uint64_reserve(&parent_triggerids, trigger_prototypes->values_num);

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trx_lld_trigger_prototype_t	*trigger_prototype;

		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		trx_vector_uint64_append(&parent_triggerids, trigger_prototype->triggerid);
	}
	sql = (char *)trx_malloc(sql, sql_alloc);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct i.itemid,i.flags"
			" from items i,functions f"
			" where i.itemid=f.itemid"
				" and");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "f.triggerid",
			parent_triggerids.values, parent_triggerids.values_num);

	trx_vector_uint64_destroy(&parent_triggerids);

	result = DBselect("%s", sql);

	trx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		item = (trx_lld_item_t *)trx_malloc(NULL, sizeof(trx_lld_item_t));

		TRX_STR2UINT64(item->itemid, row[0]);
		TRX_STR2UCHAR(item->flags, row[1]);

		trx_vector_ptr_append(items, item);
	}
	DBfree_result(result);

	trx_vector_ptr_sort(items, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_get                                                  *
 *                                                                            *
 * Purpose: finds already existing trigger, using an item prototype and items *
 *          already created by it                                             *
 *                                                                            *
 * Return value: upon successful completion return pointer to the trigger     *
 *                                                                            *
 ******************************************************************************/
static trx_lld_trigger_t	*lld_trigger_get(trx_uint64_t parent_triggerid, trx_hashset_t *items_triggers,
		const trx_vector_ptr_t *item_links)
{
	int	i;

	for (i = 0; i < item_links->values_num; i++)
	{
		trx_lld_item_trigger_t		*item_trigger, item_trigger_local;
		const trx_lld_item_link_t	*item_link = (trx_lld_item_link_t *)item_links->values[i];

		item_trigger_local.parent_triggerid = parent_triggerid;
		item_trigger_local.itemid = item_link->itemid;

		if (NULL != (item_trigger = (trx_lld_item_trigger_t *)trx_hashset_search(items_triggers, &item_trigger_local)))
			return item_trigger->trigger;
	}

	return NULL;
}

static void	lld_expression_simplify(char **expression, trx_vector_ptr_t *functions, trx_uint64_t *function_index)
{
	size_t			l, r;
	int			index;
	trx_uint64_t		functionid;
	trx_lld_function_t	*function;
	char			buffer[TRX_MAX_UINT64_LEN];

	for (l = 0; '\0' != (*expression)[l]; l++)
	{
		if ('{' != (*expression)[l])
			continue;

		if ('$' == (*expression)[l + 1])
		{
			int	macro_r, context_l, context_r;

			if (SUCCEED == trx_user_macro_parse(*expression + l, &macro_r, &context_l, &context_r))
				l += macro_r;
			else
				l++;

			continue;
		}

		for (r = l + 1; '\0' != (*expression)[r] && '}' != (*expression)[r]; r++)
			;

		if ('}' != (*expression)[r])
			continue;

		/* ... > 0 | {12345} + ... */
		/*           l     r       */

		if (SUCCEED != is_uint64_n(*expression + l + 1, r - l - 1, &functionid))
			continue;

		if (FAIL != (index = trx_vector_ptr_bsearch(functions, &functionid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			function = (trx_lld_function_t *)functions->values[index];

			if (0 == function->index)
				function->index = ++(*function_index);

			trx_snprintf(buffer, sizeof(buffer), TRX_FS_UI64, function->index);

			r--;
			trx_replace_string(expression, l + 1, &r, buffer);
			r++;
		}

		l = r;
	}
}

static void	lld_expressions_simplify(char **expression, char **recovery_expression, trx_vector_ptr_t *functions)
{
	trx_uint64_t	function_index = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() expression:'%s' recovery_expression:'%s'", __func__,
			*expression, *recovery_expression);

	lld_expression_simplify(expression, functions, &function_index);
	lld_expression_simplify(recovery_expression, functions, &function_index);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() expression:'%s' recovery_expression:'%s'", __func__,
			*expression, *recovery_expression);
}

static char	*lld_expression_expand(const char *expression, const trx_vector_ptr_t *functions)
{
	size_t		l, r;
	int		i;
	trx_uint64_t	index;
	char		*buffer = NULL;
	size_t		buffer_alloc = 64, buffer_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() expression:'%s'", __func__, expression);

	buffer = (char *)trx_malloc(buffer, buffer_alloc);

	*buffer = '\0';

	for (l = 0; '\0' != expression[l]; l++)
	{
		trx_chrcpy_alloc(&buffer, &buffer_alloc, &buffer_offset, expression[l]);

		if ('{' != expression[l])
			continue;

		if ('$' == expression[l + 1])
		{
			int	macro_r, context_l, context_r;

			if (SUCCEED == trx_user_macro_parse(expression + l, &macro_r, &context_l, &context_r))
				l += macro_r;
			else
				l++;

			continue;
		}

		for (r = l + 1; '\0' != expression[r] && '}' != expression[r]; r++)
			;

		if ('}' != expression[r])
			continue;

		/* ... > 0 | {1} + ... */
		/*           l r       */

		if (SUCCEED != is_uint64_n(expression + l + 1, r - l - 1, &index))
			continue;

		for (i = 0; i < functions->values_num; i++)
		{
			const trx_lld_function_t	*function = (trx_lld_function_t *)functions->values[i];

			if (function->index != index)
				continue;

			trx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset, TRX_FS_UI64 ":%s(%s)",
					function->itemid, function->function, function->parameter);

			break;
		}

		l = r - 1;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():'%s'", __func__, buffer);

	return buffer;
}

static int	lld_parameter_make(const char *e, char **exp, const struct trx_json_parse *jp_row,
		const trx_vector_ptr_t *lld_macros, char **error)
{
	int	ret;
	size_t	exp_alloc = 0, exp_offset = 0;
	size_t	length;
	char	err[64];

	if (FAIL == trx_function_validate_parameters(e, &length))
	{
		*error = trx_dsprintf(*error, "Invalid parameter \"%s\"", e);
		return FAIL;
	}

	if (FAIL == (ret = substitute_function_lld_param(e, length, 0, exp, &exp_alloc, &exp_offset, jp_row, lld_macros,
			err, sizeof(err))))
	{
		*error = trx_strdup(*error, err);
	}

	return ret;
}

static int	lld_function_make(const trx_lld_function_t *function_proto, trx_vector_ptr_t *functions,
		trx_uint64_t itemid, const struct trx_json_parse *jp_row, const trx_vector_ptr_t *lld_macros,
		char **error)
{
	int			i, ret;
	trx_lld_function_t	*function = NULL;
	char			*proto_parameter = NULL;

	for (i = 0; i < functions->values_num; i++)
	{
		function = (trx_lld_function_t *)functions->values[i];

		if (0 != (function->flags & TRX_FLAG_LLD_FUNCTION_DISCOVERED))
			continue;

		if (function->index == function_proto->index)
			break;
	}

	if (FAIL == (ret = lld_parameter_make(function_proto->parameter, &proto_parameter, jp_row, lld_macros, error)))
		goto clean;

	if (i == functions->values_num)
	{
		function = (trx_lld_function_t *)trx_malloc(NULL, sizeof(trx_lld_function_t));

		function->index = function_proto->index;
		function->functionid = 0;
		function->itemid = itemid;
		function->itemid_orig = 0;
		function->function = trx_strdup(NULL, function_proto->function);
		function->function_orig = NULL;
		function->parameter = proto_parameter;
		proto_parameter = NULL;
		function->parameter_orig = NULL;
		function->flags = TRX_FLAG_LLD_FUNCTION_DISCOVERED;

		trx_vector_ptr_append(functions, function);
	}
	else
	{
		if (function->itemid != itemid)
		{
			function->itemid_orig = function->itemid;
			function->itemid = itemid;
			function->flags |= TRX_FLAG_LLD_FUNCTION_UPDATE_ITEMID;
		}

		if (0 != strcmp(function->function, function_proto->function))
		{
			function->function_orig = function->function;
			function->function = trx_strdup(NULL, function_proto->function);
			function->flags |= TRX_FLAG_LLD_FUNCTION_UPDATE_FUNCTION;
		}

		if (0 != strcmp(function->parameter, proto_parameter))
		{
			function->parameter_orig = function->parameter;
			function->parameter = proto_parameter;
			proto_parameter = NULL;
			function->flags |= TRX_FLAG_LLD_FUNCTION_UPDATE_PARAMETER;
		}

		function->flags |= TRX_FLAG_LLD_FUNCTION_DISCOVERED;
	}
clean:
	trx_free(proto_parameter);

	return ret;
}

static void	lld_functions_delete(trx_vector_ptr_t *functions)
{
	int	i;

	for (i = 0; i < functions->values_num; i++)
	{
		trx_lld_function_t	*function = (trx_lld_function_t *)functions->values[i];

		if (0 != (function->flags & TRX_FLAG_LLD_FUNCTION_DISCOVERED))
			continue;

		function->flags |= TRX_FLAG_LLD_FUNCTION_DELETE;
	}
}

static int	lld_functions_make(const trx_vector_ptr_t *functions_proto, trx_vector_ptr_t *functions,
		const trx_vector_ptr_t *items, const trx_vector_ptr_t *item_links, const struct trx_json_parse *jp_row,
		const trx_vector_ptr_t *lld_macros, char **error)
{
	int				i, index, ret = FAIL;
	const trx_lld_function_t	*function_proto;
	const trx_lld_item_t		*item_proto;
	const trx_lld_item_link_t	*item_link;
	trx_uint64_t			itemid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < functions_proto->values_num; i++)
	{
		function_proto = (trx_lld_function_t *)functions_proto->values[i];

		index = trx_vector_ptr_bsearch(items, &function_proto->itemid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

		if (FAIL == index)
			goto out;

		item_proto = (trx_lld_item_t *)items->values[index];

		if (0 != (item_proto->flags & TRX_FLAG_DISCOVERY_PROTOTYPE))
		{
			index = trx_vector_ptr_bsearch(item_links, &item_proto->itemid,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

			if (FAIL == index)
				goto out;

			item_link = (trx_lld_item_link_t *)item_links->values[index];

			itemid = item_link->itemid;
		}
		else
			itemid = item_proto->itemid;

		if (FAIL == lld_function_make(function_proto, functions, itemid, jp_row, lld_macros, error))
			goto out;
	}

	lld_functions_delete(functions);

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_make                                                 *
 *                                                                            *
 * Purpose: create a trigger based on lld rule and add it to the list         *
 *                                                                            *
 ******************************************************************************/
static void 	lld_trigger_make(const trx_lld_trigger_prototype_t *trigger_prototype, trx_vector_ptr_t *triggers,
		const trx_vector_ptr_t *items, trx_hashset_t *items_triggers, const trx_lld_row_t *lld_row,
		const trx_vector_ptr_t *lld_macros, char **error)
{
	trx_lld_trigger_t		*trigger;
	char				*buffer = NULL, *expression = NULL, *recovery_expression = NULL, err[64];
	char				*err_msg = NULL;
	const char			*operation_msg;
	const struct trx_json_parse	*jp_row = &lld_row->jp_row;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trigger = lld_trigger_get(trigger_prototype->triggerid, items_triggers, &lld_row->item_links);
	operation_msg = NULL != trigger ? "update" : "create";

	expression = trx_strdup(expression, trigger_prototype->expression);
	recovery_expression = trx_strdup(recovery_expression, trigger_prototype->recovery_expression);

	if (SUCCEED != substitute_lld_macros(&expression, jp_row, lld_macros, TRX_MACRO_NUMERIC, err, sizeof(err)) ||
			SUCCEED != substitute_lld_macros(&recovery_expression, jp_row, lld_macros, TRX_MACRO_NUMERIC, err,
					sizeof(err)))
	{
		*error = trx_strdcatf(*error, "Cannot %s trigger: %s.\n", operation_msg, err);
		goto out;
	}

	if (NULL != trigger)
	{
		buffer = trx_strdup(buffer, trigger_prototype->description);
		substitute_lld_macros(&buffer, jp_row, lld_macros, TRX_MACRO_FUNC, NULL, 0);
		trx_lrtrim(buffer, TRX_WHITESPACE);
		if (0 != strcmp(trigger->description, buffer))
		{
			trigger->description_orig = trigger->description;
			trigger->description = buffer;
			buffer = NULL;
			trigger->flags |= TRX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION;
		}

		if (0 != strcmp(trigger->expression, expression))
		{
			trigger->expression_orig = trigger->expression;
			trigger->expression = expression;
			expression = NULL;
			trigger->flags |= TRX_FLAG_LLD_TRIGGER_UPDATE_EXPRESSION;
		}

		if (0 != strcmp(trigger->recovery_expression, recovery_expression))
		{
			trigger->recovery_expression_orig = trigger->recovery_expression;
			trigger->recovery_expression = recovery_expression;
			recovery_expression = NULL;
			trigger->flags |= TRX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_EXPRESSION;
		}

		buffer = trx_strdup(buffer, trigger_prototype->comments);
		substitute_lld_macros(&buffer, jp_row, lld_macros, TRX_MACRO_FUNC, NULL, 0);
		trx_lrtrim(buffer, TRX_WHITESPACE);
		if (0 != strcmp(trigger->comments, buffer))
		{
			trigger->comments_orig = trigger->comments;
			trigger->comments = buffer;
			buffer = NULL;
			trigger->flags |= TRX_FLAG_LLD_TRIGGER_UPDATE_COMMENTS;
		}

		buffer = trx_strdup(buffer, trigger_prototype->url);
		substitute_lld_macros(&buffer, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(buffer, TRX_WHITESPACE);
		if (0 != strcmp(trigger->url, buffer))
		{
			trigger->url_orig = trigger->url;
			trigger->url = buffer;
			buffer = NULL;
			trigger->flags |= TRX_FLAG_LLD_TRIGGER_UPDATE_URL;
		}

		buffer = trx_strdup(buffer, trigger_prototype->correlation_tag);
		substitute_lld_macros(&buffer, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(buffer, TRX_WHITESPACE);
		if (0 != strcmp(trigger->correlation_tag, buffer))
		{
			trigger->correlation_tag_orig = trigger->correlation_tag;
			trigger->correlation_tag = buffer;
			buffer = NULL;
			trigger->flags |= TRX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_TAG;
		}

		buffer = trx_strdup(buffer, trigger_prototype->opdata);
		substitute_lld_macros(&buffer, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(buffer, TRX_WHITESPACE);
		if (0 != strcmp(trigger->opdata, buffer))
		{
			trigger->opdata_orig = trigger->opdata;
			trigger->opdata = buffer;
			buffer = NULL;
			trigger->flags |= TRX_FLAG_LLD_TRIGGER_UPDATE_OPDATA;
		}
	}
	else
	{
		trigger = (trx_lld_trigger_t *)trx_malloc(NULL, sizeof(trx_lld_trigger_t));

		trigger->triggerid = 0;
		trigger->parent_triggerid = trigger_prototype->triggerid;

		trigger->description = trx_strdup(NULL, trigger_prototype->description);
		trigger->description_orig = NULL;
		substitute_lld_macros(&trigger->description, jp_row, lld_macros, TRX_MACRO_FUNC, NULL, 0);
		trx_lrtrim(trigger->description, TRX_WHITESPACE);

		trigger->expression = expression;
		trigger->expression_orig = NULL;
		expression = NULL;

		trigger->recovery_expression = recovery_expression;
		trigger->recovery_expression_orig = NULL;
		recovery_expression = NULL;

		trigger->comments = trx_strdup(NULL, trigger_prototype->comments);
		trigger->comments_orig = NULL;
		substitute_lld_macros(&trigger->comments, jp_row, lld_macros, TRX_MACRO_FUNC, NULL, 0);
		trx_lrtrim(trigger->comments, TRX_WHITESPACE);

		trigger->url = trx_strdup(NULL, trigger_prototype->url);
		trigger->url_orig = NULL;
		substitute_lld_macros(&trigger->url, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(trigger->url, TRX_WHITESPACE);

		trigger->correlation_tag = trx_strdup(NULL, trigger_prototype->correlation_tag);
		trigger->correlation_tag_orig = NULL;
		substitute_lld_macros(&trigger->correlation_tag, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(trigger->correlation_tag, TRX_WHITESPACE);

		trigger->opdata = trx_strdup(NULL, trigger_prototype->opdata);
		trigger->opdata_orig = NULL;
		substitute_lld_macros(&trigger->opdata, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(trigger->opdata, TRX_WHITESPACE);

		trx_vector_ptr_create(&trigger->functions);
		trx_vector_ptr_create(&trigger->dependencies);
		trx_vector_ptr_create(&trigger->dependents);
		trx_vector_ptr_create(&trigger->tags);

		trigger->flags = TRX_FLAG_LLD_TRIGGER_UNSET;

		trx_vector_ptr_append(triggers, trigger);
	}

	trx_free(buffer);

	if (SUCCEED != lld_functions_make(&trigger_prototype->functions, &trigger->functions, items,
			&lld_row->item_links, jp_row, lld_macros, &err_msg))
	{
		if (err_msg)
		{
			*error = trx_strdcatf(*error, "Cannot %s trigger: %s.\n", operation_msg, err_msg);
			trx_free(err_msg);
		}
		goto out;
	}

	trigger->flags |= TRX_FLAG_LLD_TRIGGER_DISCOVERED;
out:
	trx_free(recovery_expression);
	trx_free(expression);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static trx_hash_t	items_triggers_hash_func(const void *data)
{
	const trx_lld_item_trigger_t	*item_trigger = (trx_lld_item_trigger_t *)data;
	trx_hash_t			hash;

	hash = TRX_DEFAULT_UINT64_HASH_FUNC(&item_trigger->parent_triggerid);
	hash = TRX_DEFAULT_UINT64_HASH_ALGO(&item_trigger->itemid, sizeof(trx_uint64_t), hash);

	return hash;
}

static int	items_triggers_compare_func(const void *d1, const void *d2)
{
	const trx_lld_item_trigger_t	*item_trigger1 = (trx_lld_item_trigger_t *)d1, *item_trigger2 = (trx_lld_item_trigger_t *)d2;

	TRX_RETURN_IF_NOT_EQUAL(item_trigger1->parent_triggerid, item_trigger2->parent_triggerid);
	TRX_RETURN_IF_NOT_EQUAL(item_trigger1->itemid, item_trigger2->itemid);

	return 0;
}

static void	lld_triggers_make(const trx_vector_ptr_t *trigger_prototypes, trx_vector_ptr_t *triggers,
		const trx_vector_ptr_t *items, const trx_vector_ptr_t *lld_rows,
		const trx_vector_ptr_t *lld_macro_paths, char **error)
{
	const trx_lld_trigger_prototype_t	*trigger_prototype;
	int					i, j;
	trx_hashset_t				items_triggers;
	trx_lld_trigger_t			*trigger;
	const trx_lld_function_t		*function;
	trx_lld_item_trigger_t			item_trigger;

	/* used for fast search of trigger by item prototype */
	trx_hashset_create(&items_triggers, 512, items_triggers_hash_func, items_triggers_compare_func);

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		for (j = 0; j < trigger->functions.values_num; j++)
		{
			function = (trx_lld_function_t *)trigger->functions.values[j];

			item_trigger.parent_triggerid = trigger->parent_triggerid;
			item_trigger.itemid = function->itemid;
			item_trigger.trigger = trigger;
			trx_hashset_insert(&items_triggers, &item_trigger, sizeof(item_trigger));
		}
	}

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		for (j = 0; j < lld_rows->values_num; j++)
		{
			trx_lld_row_t	*lld_row = (trx_lld_row_t *)lld_rows->values[j];

			lld_trigger_make(trigger_prototype, triggers, items, &items_triggers, lld_row, lld_macro_paths,
					error);
		}
	}

	trx_hashset_destroy(&items_triggers);

	trx_vector_ptr_sort(triggers, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_dependency_make                                      *
 *                                                                            *
 * Purpose: create a trigger dependencies                                     *
 *                                                                            *
 ******************************************************************************/
static void 	lld_trigger_dependency_make(const trx_lld_trigger_prototype_t *trigger_prototype,
		const trx_vector_ptr_t *trigger_prototypes, trx_hashset_t *items_triggers, const trx_lld_row_t *lld_row,
		char **error)
{
	trx_lld_trigger_t			*trigger, *dep_trigger;
	const trx_lld_trigger_prototype_t	*dep_trigger_prototype;
	trx_lld_dependency_t			*dependency;
	trx_uint64_t				triggerid_up;
	int					i, j, index;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (NULL == (trigger = lld_trigger_get(trigger_prototype->triggerid, items_triggers, &lld_row->item_links)))
		goto out;

	for (i = 0; i < trigger_prototype->dependencies.values_num; i++)
	{
		triggerid_up = ((trx_lld_dependency_t *)trigger_prototype->dependencies.values[i])->triggerid_up;

		index = trx_vector_ptr_bsearch(trigger_prototypes, &triggerid_up, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

		if (FAIL != index)
		{
			/* creating trigger dependency based on trigger prototype */

			dep_trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[index];

			dep_trigger = lld_trigger_get(dep_trigger_prototype->triggerid, items_triggers,
					&lld_row->item_links);

			if (NULL != dep_trigger)
			{
				if (0 == dep_trigger->triggerid)
				{
					dependency = (trx_lld_dependency_t *)trx_malloc(NULL, sizeof(trx_lld_dependency_t));

					dependency->triggerdepid = 0;
					dependency->triggerid_up = 0;

					trx_vector_ptr_append(&trigger->dependencies, dependency);
				}
				else
				{
					for (j = 0; j < trigger->dependencies.values_num; j++)
					{
						dependency = (trx_lld_dependency_t *)trigger->dependencies.values[j];

						if (0 != (dependency->flags & TRX_FLAG_LLD_DEPENDENCY_DISCOVERED))
							continue;

						if (dependency->triggerid_up == dep_trigger->triggerid)
							break;
					}

					if (j == trigger->dependencies.values_num)
					{
						dependency = (trx_lld_dependency_t *)trx_malloc(NULL, sizeof(trx_lld_dependency_t));

						dependency->triggerdepid = 0;
						dependency->triggerid_up = dep_trigger->triggerid;

						trx_vector_ptr_append(&trigger->dependencies, dependency);
					}
				}

				trx_vector_ptr_append(&dep_trigger->dependents, trigger);

				dependency->trigger_up = dep_trigger;
				dependency->flags = TRX_FLAG_LLD_DEPENDENCY_DISCOVERED;
			}
			else
			{
				*error = trx_strdcatf(*error, "Cannot create dependency on trigger \"%s\".\n",
						trigger->description);
			}
		}
		else
		{
			/* creating trigger dependency based on generic trigger */

			for (j = 0; j < trigger->dependencies.values_num; j++)
			{
				dependency = (trx_lld_dependency_t *)trigger->dependencies.values[j];

				if (0 != (dependency->flags & TRX_FLAG_LLD_DEPENDENCY_DISCOVERED))
					continue;

				if (dependency->triggerid_up == triggerid_up)
					break;
			}

			if (j == trigger->dependencies.values_num)
			{
				dependency = (trx_lld_dependency_t *)trx_malloc(NULL, sizeof(trx_lld_dependency_t));

				dependency->triggerdepid = 0;
				dependency->triggerid_up = triggerid_up;
				dependency->trigger_up = NULL;

				trx_vector_ptr_append(&trigger->dependencies, dependency);
			}

			dependency->flags = TRX_FLAG_LLD_DEPENDENCY_DISCOVERED;
		}
	}
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	lld_trigger_dependencies_make(const trx_vector_ptr_t *trigger_prototypes, trx_vector_ptr_t *triggers,
		const trx_vector_ptr_t *lld_rows, char **error)
{
	const trx_lld_trigger_prototype_t	*trigger_prototype;
	int				i, j;
	trx_hashset_t			items_triggers;
	trx_lld_trigger_t		*trigger;
	trx_lld_function_t		*function;
	trx_lld_item_trigger_t		item_trigger;
	trx_lld_dependency_t		*dependency;

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		if (0 != trigger_prototype->dependencies.values_num)
			break;
	}

	for (j = 0; j < triggers->values_num; j++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[j];

		if (0 != trigger->dependencies.values_num)
			break;
	}

	/* all trigger prototypes and triggers have no dependencies */
	if (i == trigger_prototypes->values_num && j == triggers->values_num)
		return;

	/* used for fast search of trigger by item prototype */
	trx_hashset_create(&items_triggers, 512, items_triggers_hash_func, items_triggers_compare_func);

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & TRX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		for (j = 0; j < trigger->functions.values_num; j++)
		{
			function = (trx_lld_function_t *)trigger->functions.values[j];

			item_trigger.parent_triggerid = trigger->parent_triggerid;
			item_trigger.itemid = function->itemid;
			item_trigger.trigger = trigger;
			trx_hashset_insert(&items_triggers, &item_trigger, sizeof(item_trigger));
		}
	}

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		for (j = 0; j < lld_rows->values_num; j++)
		{
			trx_lld_row_t	*lld_row = (trx_lld_row_t *)lld_rows->values[j];

			lld_trigger_dependency_make(trigger_prototype, trigger_prototypes,
					&items_triggers, lld_row, error);
		}
	}

	/* marking dependencies which will be deleted */
	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & TRX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		for (j = 0; j < trigger->dependencies.values_num; j++)
		{
			dependency = (trx_lld_dependency_t *)trigger->dependencies.values[j];

			if (0 == (dependency->flags & TRX_FLAG_LLD_DEPENDENCY_DISCOVERED))
				dependency->flags = TRX_FLAG_LLD_DEPENDENCY_DELETE;
		}
	}

	trx_hashset_destroy(&items_triggers);

	trx_vector_ptr_sort(triggers, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_tag_make                                             *
 *                                                                            *
 * Purpose: create a trigger tag                                              *
 *                                                                            *
 ******************************************************************************/
static void 	lld_trigger_tag_make(trx_lld_trigger_prototype_t *trigger_prototype,
		trx_hashset_t *items_triggers, trx_lld_row_t *lld_row, const trx_vector_ptr_t *lld_macro_paths)
{
	trx_lld_trigger_t	*trigger;
	int			i;
	trx_lld_tag_t		*tag_proto, *tag;
	char			*buffer = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (NULL == (trigger = lld_trigger_get(trigger_prototype->triggerid, items_triggers, &lld_row->item_links)))
		goto out;

	for (i = 0; i < trigger_prototype->tags.values_num; i++)
	{
		tag_proto = (trx_lld_tag_t *)trigger_prototype->tags.values[i];

		if (i < trigger->tags.values_num)
		{
			tag = (trx_lld_tag_t *)trigger->tags.values[i];

			buffer = trx_strdup(buffer, tag_proto->tag);
			substitute_lld_macros(&buffer, &lld_row->jp_row, lld_macro_paths, TRX_MACRO_FUNC, NULL, 0);
			trx_lrtrim(buffer, TRX_WHITESPACE);
			if (0 != strcmp(buffer, tag->tag))
			{
				trx_free(tag->tag);
				tag->tag = buffer;
				buffer = NULL;
				tag->flags |= TRX_FLAG_LLD_TAG_UPDATE_TAG;
			}

			buffer = trx_strdup(buffer, tag_proto->value);
			substitute_lld_macros(&buffer, &lld_row->jp_row, lld_macro_paths, TRX_MACRO_FUNC, NULL, 0);
			trx_lrtrim(buffer, TRX_WHITESPACE);
			if (0 != strcmp(buffer, tag->value))
			{
				trx_free(tag->value);
				tag->value = buffer;
				buffer = NULL;
				tag->flags |= TRX_FLAG_LLD_TAG_UPDATE_VALUE;
			}
		}
		else
		{
			tag = (trx_lld_tag_t *)trx_malloc(NULL, sizeof(trx_lld_tag_t));

			tag->triggertagid = 0;

			tag->tag = trx_strdup(NULL, tag_proto->tag);
			substitute_lld_macros(&tag->tag, &lld_row->jp_row, lld_macro_paths, TRX_MACRO_FUNC, NULL, 0);
			trx_lrtrim(tag->tag, TRX_WHITESPACE);

			tag->value = trx_strdup(NULL, tag_proto->value);
			substitute_lld_macros(&tag->value, &lld_row->jp_row, lld_macro_paths, TRX_MACRO_FUNC, NULL, 0);
			trx_lrtrim(tag->value, TRX_WHITESPACE);

			tag->flags = TRX_FLAG_LLD_TAG_UNSET;

			trx_vector_ptr_append(&trigger->tags, tag);

		}

		tag->flags |= TRX_FLAG_LLD_TAG_DISCOVERED;
	}
out:
	trx_free(buffer);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_tags_make                                            *
 *                                                                            *
 * Purpose: create a trigger tags                                             *
 *                                                                            *
 ******************************************************************************/
static void	lld_trigger_tags_make(trx_vector_ptr_t *trigger_prototypes, trx_vector_ptr_t *triggers,
		const trx_vector_ptr_t *lld_rows, const trx_vector_ptr_t *lld_macro_paths)
{
	trx_lld_trigger_prototype_t	*trigger_prototype;
	int				i, j;
	trx_hashset_t			items_triggers;
	trx_lld_trigger_t		*trigger;
	trx_lld_function_t		*function;
	trx_lld_item_trigger_t		item_trigger;
	trx_lld_tag_t			*tag;

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		if (0 != trigger_prototype->tags.values_num)
			break;
	}

	/* trigger prototypes have no tags */
	if (i == trigger_prototypes->values_num)
		return;

	/* used for fast search of trigger by item prototype */
	trx_hashset_create(&items_triggers, 512, items_triggers_hash_func, items_triggers_compare_func);

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & TRX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		for (j = 0; j < trigger->functions.values_num; j++)
		{
			function = (trx_lld_function_t *)trigger->functions.values[j];

			item_trigger.parent_triggerid = trigger->parent_triggerid;
			item_trigger.itemid = function->itemid;
			item_trigger.trigger = trigger;
			trx_hashset_insert(&items_triggers, &item_trigger, sizeof(item_trigger));
		}
	}

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		for (j = 0; j < lld_rows->values_num; j++)
		{
			trx_lld_row_t	*lld_row = (trx_lld_row_t *)lld_rows->values[j];

			lld_trigger_tag_make(trigger_prototype, &items_triggers, lld_row, lld_macro_paths);
		}
	}

	/* marking tags which will be deleted */
	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & TRX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		for (j = 0; j < trigger->tags.values_num; j++)
		{
			tag = (trx_lld_tag_t *)trigger->tags.values[j];

			if (0 == (tag->flags & TRX_FLAG_LLD_TAG_DISCOVERED))
				tag->flags = TRX_FLAG_LLD_TAG_DELETE;
		}
	}

	trx_hashset_destroy(&items_triggers);

	trx_vector_ptr_sort(triggers, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_validate_trigger_field                                       *
 *                                                                            *
 ******************************************************************************/
static void	lld_validate_trigger_field(trx_lld_trigger_t *trigger, char **field, char **field_orig,
		trx_uint64_t flag, size_t field_len, char **error)
{
	if (0 == (trigger->flags & TRX_FLAG_LLD_TRIGGER_DISCOVERED))
		return;

	/* only new triggers or triggers with changed data will be validated */
	if (0 != trigger->triggerid && 0 == (trigger->flags & flag))
		return;

	if (SUCCEED != trx_is_utf8(*field))
	{
		trx_replace_invalid_utf8(*field);
		*error = trx_strdcatf(*error, "Cannot %s trigger: value \"%s\" has invalid UTF-8 sequence.\n",
				(0 != trigger->triggerid ? "update" : "create"), *field);
	}
	else if (trx_strlen_utf8(*field) > field_len)
	{
		*error = trx_strdcatf(*error, "Cannot %s trigger: value \"%s\" is too long.\n",
				(0 != trigger->triggerid ? "update" : "create"), *field);
	}
	else if (TRX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION == flag && '\0' == **field)
	{
		*error = trx_strdcatf(*error, "Cannot %s trigger: name is empty.\n",
				(0 != trigger->triggerid ? "update" : "create"));
	}
	else
		return;

	if (0 != trigger->triggerid)
		lld_field_str_rollback(field, field_orig, &trigger->flags, flag);
	else
		trigger->flags &= ~TRX_FLAG_LLD_TRIGGER_DISCOVERED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_changed                                              *
 *                                                                            *
 * Return value: returns SUCCEED if a trigger description or expression has   *
 *               been changed; FAIL - otherwise                               *
 *                                                                            *
 ******************************************************************************/
static int	lld_trigger_changed(const trx_lld_trigger_t *trigger)
{
	int			i;
	trx_lld_function_t	*function;

	if (0 == trigger->triggerid)
		return SUCCEED;

	if (0 != (trigger->flags & (TRX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION | TRX_FLAG_LLD_TRIGGER_UPDATE_EXPRESSION |
			TRX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_EXPRESSION)))
	{
		return SUCCEED;
	}

	for (i = 0; i < trigger->functions.values_num; i++)
	{
		function = (trx_lld_function_t *)trigger->functions.values[i];

		if (0 == function->functionid)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			return SUCCEED;
		}

		if (0 != (function->flags & TRX_FLAG_LLD_FUNCTION_UPDATE))
			return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_triggers_equal                                               *
 *                                                                            *
 * Return value: returns SUCCEED if descriptions and expressions of           *
 *               the triggers are identical; FAIL - otherwise                 *
 *                                                                            *
 ******************************************************************************/
static int	lld_triggers_equal(const trx_lld_trigger_t *trigger, const trx_lld_trigger_t *trigger_b)
{
	int	ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == strcmp(trigger->description, trigger_b->description))
	{
		char	*expression, *expression_b;

		expression = lld_expression_expand(trigger->expression, &trigger->functions);
		expression_b = lld_expression_expand(trigger_b->expression, &trigger_b->functions);

		if (0 == strcmp(expression, expression_b))
		{
			trx_free(expression);
			trx_free(expression_b);

			expression = lld_expression_expand(trigger->recovery_expression, &trigger->functions);
			expression_b = lld_expression_expand(trigger_b->recovery_expression, &trigger_b->functions);

			if (0 == strcmp(expression, expression_b))
				ret = SUCCEED;
		}

		trx_free(expression);
		trx_free(expression_b);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_triggers_validate                                            *
 *                                                                            *
 * Parameters: triggers - [IN] sorted list of triggers                        *
 *                                                                            *
 ******************************************************************************/
static void	lld_triggers_validate(trx_uint64_t hostid, trx_vector_ptr_t *triggers, char **error)
{
	int			i, j, k;
	trx_lld_trigger_t	*trigger;
	trx_lld_function_t	*function;
	trx_vector_uint64_t	triggerids;
	trx_vector_str_t	descriptions;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&triggerids);
	trx_vector_str_create(&descriptions);

	/* checking a validity of the fields */

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		lld_validate_trigger_field(trigger, &trigger->description, &trigger->description_orig,
				TRX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION, TRIGGER_DESCRIPTION_LEN, error);
		lld_validate_trigger_field(trigger, &trigger->comments, &trigger->comments_orig,
				TRX_FLAG_LLD_TRIGGER_UPDATE_COMMENTS, TRIGGER_COMMENTS_LEN, error);
		lld_validate_trigger_field(trigger, &trigger->url, &trigger->url_orig,
				TRX_FLAG_LLD_TRIGGER_UPDATE_URL, TRIGGER_URL_LEN, error);
		lld_validate_trigger_field(trigger, &trigger->correlation_tag, &trigger->correlation_tag_orig,
				TRX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_TAG, TAG_NAME_LEN, error);
		lld_validate_trigger_field(trigger, &trigger->opdata, &trigger->opdata_orig,
				TRX_FLAG_LLD_TRIGGER_UPDATE_OPDATA, TRIGGER_OPDATA_LEN, error);
	}

	/* checking duplicated triggers in DB */

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & TRX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		if (0 != trigger->triggerid)
		{
			trx_vector_uint64_append(&triggerids, trigger->triggerid);

			if (SUCCEED != lld_trigger_changed(trigger))
				continue;
		}

		trx_vector_str_append(&descriptions, trigger->description);
	}

	if (0 != descriptions.values_num)
	{
		char			*sql = NULL;
		size_t			sql_alloc = 256, sql_offset = 0;
		DB_RESULT		result;
		DB_ROW			row;
		trx_vector_ptr_t	db_triggers;
		trx_lld_trigger_t	*db_trigger;

		trx_vector_ptr_create(&db_triggers);

		trx_vector_str_sort(&descriptions, TRX_DEFAULT_STR_COMPARE_FUNC);
		trx_vector_str_uniq(&descriptions, TRX_DEFAULT_STR_COMPARE_FUNC);

		sql = (char *)trx_malloc(sql, sql_alloc);

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select distinct t.triggerid,t.description,t.expression,t.recovery_expression"
				" from triggers t,functions f,items i"
				" where t.triggerid=f.triggerid"
					" and f.itemid=i.itemid"
					" and i.hostid=" TRX_FS_UI64
					" and",
				hostid);
		DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.description",
				(const char **)descriptions.values, descriptions.values_num);

		if (0 != triggerids.values_num)
		{
			trx_vector_uint64_sort(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and not");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.triggerid",
					triggerids.values, triggerids.values_num);
		}

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			db_trigger = (trx_lld_trigger_t *)trx_malloc(NULL, sizeof(trx_lld_trigger_t));

			TRX_STR2UINT64(db_trigger->triggerid, row[0]);
			db_trigger->description = trx_strdup(NULL, row[1]);
			db_trigger->description_orig = NULL;
			db_trigger->expression = trx_strdup(NULL, row[2]);
			db_trigger->expression_orig = NULL;
			db_trigger->recovery_expression = trx_strdup(NULL, row[3]);
			db_trigger->recovery_expression_orig = NULL;
			db_trigger->comments = NULL;
			db_trigger->comments_orig = NULL;
			db_trigger->url = NULL;
			db_trigger->url_orig = NULL;
			db_trigger->correlation_tag = NULL;
			db_trigger->correlation_tag_orig = NULL;
			db_trigger->opdata = NULL;
			db_trigger->opdata_orig = NULL;
			db_trigger->flags = TRX_FLAG_LLD_TRIGGER_UNSET;

			trx_vector_ptr_create(&db_trigger->functions);
			trx_vector_ptr_create(&db_trigger->dependencies);
			trx_vector_ptr_create(&db_trigger->dependents);
			trx_vector_ptr_create(&db_trigger->tags);

			trx_vector_ptr_append(&db_triggers, db_trigger);
		}
		DBfree_result(result);

		trx_vector_ptr_sort(&db_triggers, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

		lld_functions_get(NULL, &db_triggers);

		for (i = 0; i < db_triggers.values_num; i++)
		{
			db_trigger = (trx_lld_trigger_t *)db_triggers.values[i];

			lld_expressions_simplify(&db_trigger->expression, &db_trigger->recovery_expression,
					&db_trigger->functions);

			for (j = 0; j < triggers->values_num; j++)
			{
				trigger = (trx_lld_trigger_t *)triggers->values[j];

				if (0 == (trigger->flags & TRX_FLAG_LLD_TRIGGER_DISCOVERED))
					continue;

				if (SUCCEED != lld_triggers_equal(trigger, db_trigger))
					continue;

				*error = trx_strdcatf(*error, "Cannot %s trigger: trigger \"%s\" already exists.\n",
						(0 != trigger->triggerid ? "update" : "create"), trigger->description);

				if (0 != trigger->triggerid)
				{
					lld_field_str_rollback(&trigger->description, &trigger->description_orig,
							&trigger->flags, TRX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION);

					lld_field_str_rollback(&trigger->expression, &trigger->expression_orig,
							&trigger->flags, TRX_FLAG_LLD_TRIGGER_UPDATE_EXPRESSION);

					lld_field_str_rollback(&trigger->recovery_expression,
							&trigger->recovery_expression_orig, &trigger->flags,
							TRX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_EXPRESSION);

					for (k = 0; k < trigger->functions.values_num; k++)
					{
						function = (trx_lld_function_t *)trigger->functions.values[k];

						if (0 != function->functionid)
						{
							lld_field_uint64_rollback(&function->itemid,
									&function->itemid_orig,
									&function->flags,
									TRX_FLAG_LLD_FUNCTION_UPDATE_ITEMID);

							lld_field_str_rollback(&function->function,
									&function->function_orig,
									&function->flags,
									TRX_FLAG_LLD_FUNCTION_UPDATE_FUNCTION);

							lld_field_str_rollback(&function->parameter,
									&function->parameter_orig,
									&function->flags,
									TRX_FLAG_LLD_FUNCTION_UPDATE_PARAMETER);

							function->flags &= ~TRX_FLAG_LLD_FUNCTION_DELETE;
						}
						else
							function->flags &= ~TRX_FLAG_LLD_FUNCTION_DISCOVERED;
					}
				}
				else
					trigger->flags &= ~TRX_FLAG_LLD_TRIGGER_DISCOVERED;

				break;	/* only one same trigger can be here */
			}
		}

		trx_vector_ptr_clear_ext(&db_triggers, (trx_clean_func_t)lld_trigger_free);
		trx_vector_ptr_destroy(&db_triggers);

		trx_free(sql);
	}

	trx_vector_str_destroy(&descriptions);
	trx_vector_uint64_destroy(&triggerids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_validate_trigger_tag_field                                   *
 *                                                                            *
 ******************************************************************************/
static void	lld_validate_trigger_tag_field(trx_lld_tag_t *tag, const char *field, trx_uint64_t flag,
		size_t field_len, char **error)
{
	size_t	len;

	if (0 == (tag->flags & TRX_FLAG_LLD_TAG_DISCOVERED))
		return;

	/* only new trigger tags or tags with changed data will be validated */
	if (0 != tag->triggertagid && 0 == (tag->flags & flag))
		return;

	if (SUCCEED != trx_is_utf8(field))
	{
		char	*field_utf8;

		field_utf8 = trx_strdup(NULL, field);
		trx_replace_invalid_utf8(field_utf8);
		*error = trx_strdcatf(*error, "Cannot create trigger tag: value \"%s\" has invalid UTF-8 sequence.\n",
				field_utf8);
		trx_free(field_utf8);
	}
	else if ((len = trx_strlen_utf8(field)) > field_len)
		*error = trx_strdcatf(*error, "Cannot create trigger tag: value \"%s\" is too long.\n", field);
	else if (0 != (flag & TRX_FLAG_LLD_TAG_UPDATE_TAG) && 0 == len)
		*error = trx_strdcatf(*error, "Cannot create trigger tag: empty tag name.\n");
	else
		return;

	if (0 != tag->triggertagid)
		tag->flags = TRX_FLAG_LLD_TAG_DELETE;
	else
		tag->flags &= ~TRX_FLAG_LLD_TAG_DISCOVERED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_tags_validate                                        *
 *                                                                            *
 * Purpose: validate created or updated trigger tags                          *
 *                                                                            *
 ******************************************************************************/
static void	lld_trigger_tags_validate(trx_vector_ptr_t *triggers, char **error)
{
	int			i, j, k;
	trx_lld_trigger_t	*trigger;
	trx_lld_tag_t		*tag, *tag_tmp;

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & TRX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		for (j = 0; j < trigger->tags.values_num; j++)
		{
			tag = (trx_lld_tag_t *)trigger->tags.values[j];

			lld_validate_trigger_tag_field(tag, tag->tag, TRX_FLAG_LLD_TAG_UPDATE_TAG,
					TAG_NAME_LEN, error);
			lld_validate_trigger_tag_field(tag, tag->value, TRX_FLAG_LLD_TAG_UPDATE_VALUE,
					TAG_VALUE_LEN, error);

			if (0 == (tag->flags & TRX_FLAG_LLD_TAG_DISCOVERED))
				continue;

			/* check for duplicated tag,values pairs */
			for (k = 0; k < j; k++)
			{
				tag_tmp = (trx_lld_tag_t *)trigger->tags.values[k];

				if (0 == strcmp(tag->tag, tag_tmp->tag) && 0 == strcmp(tag->value, tag_tmp->value))
				{
					*error = trx_strdcatf(*error, "Cannot create trigger tag: tag \"%s\","
						"\"%s\" already exists.\n", tag->tag, tag->value);

					if (0 != tag->triggertagid)
						tag->flags = TRX_FLAG_LLD_TAG_DELETE;
					else
						tag->flags &= ~TRX_FLAG_LLD_TAG_DISCOVERED;
				}
			}

			/* reset trigger discovery flags for new trigger if tag discovery failed */
			if (0 == trigger->triggerid && 0 == (tag->flags & TRX_FLAG_LLD_TAG_DISCOVERED))
			{
				trigger->flags &= ~TRX_FLAG_LLD_TRIGGER_DISCOVERED;
				break;
			}
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_expression_create                                            *
 *                                                                            *
 * Purpose: transforms the simple trigger expression to the DB format         *
 *                                                                            *
 * Example:                                                                   *
 *                                                                            *
 *     "{1} > 5" => "{84756} > 5"                                             *
 *       ^            ^                                                       *
 *       |            functionid from the database                            *
 *       internal function index                                              *
 *                                                                            *
 ******************************************************************************/
static void	lld_expression_create(char **expression, const trx_vector_ptr_t *functions)
{
	size_t		l, r;
	int		i;
	trx_uint64_t	function_index;
	char		buffer[TRX_MAX_UINT64_LEN];

	treegix_log(LOG_LEVEL_DEBUG, "In %s() expression:'%s'", __func__, *expression);

	for (l = 0; '\0' != (*expression)[l]; l++)
	{
		if ('{' != (*expression)[l])
			continue;

		if ('$' == (*expression)[l + 1])
		{
			int	macro_r, context_l, context_r;

			if (SUCCEED == trx_user_macro_parse(*expression + l, &macro_r, &context_l, &context_r))
				l += macro_r;
			else
				l++;

			continue;
		}

		for (r = l + 1; '\0' != (*expression)[r] && '}' != (*expression)[r]; r++)
			;

		if ('}' != (*expression)[r])
			continue;

		/* ... > 0 | {1} + ... */
		/*           l r       */

		if (SUCCEED != is_uint64_n(*expression + l + 1, r - l - 1, &function_index))
			continue;

		for (i = 0; i < functions->values_num; i++)
		{
			const trx_lld_function_t	*function = (trx_lld_function_t *)functions->values[i];

			if (function->index != function_index)
				continue;

			trx_snprintf(buffer, sizeof(buffer), TRX_FS_UI64, function->functionid);

			r--;
			trx_replace_string(expression, l + 1, &r, buffer);
			r++;

			break;
		}

		l = r;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() expression:'%s'", __func__, *expression);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_triggers_save                                                *
 *                                                                            *
 * Purpose: add or update triggers in database based on discovery rule        *
 *                                                                            *
 * Parameters: hostid            - [IN] parent host id                        *
 *             lld_triggers_save - [IN] trigger prototypes                    *
 *             triggers          - [IN/OUT] triggers to save                  *
 *                                                                            *
 * Return value: SUCCEED - if triggers was successfully saved or saving       *
 *                         was not necessary                                  *
 *               FAIL    - triggers cannot be saved                           *
 *                                                                            *
 ******************************************************************************/
static int	lld_triggers_save(trx_uint64_t hostid, const trx_vector_ptr_t *trigger_prototypes,
		const trx_vector_ptr_t *triggers)
{
	int					ret = SUCCEED, i, j, new_triggers = 0, upd_triggers = 0, new_functions = 0,
						new_dependencies = 0, new_tags = 0, upd_tags = 0;
	const trx_lld_trigger_prototype_t	*trigger_prototype;
	trx_lld_trigger_t			*trigger;
	trx_lld_function_t			*function;
	trx_lld_dependency_t			*dependency;
	trx_lld_tag_t				*tag;
	trx_vector_ptr_t			upd_functions;	/* the ordered list of functions which will be updated */
	trx_vector_uint64_t			del_functionids, del_triggerdepids, del_triggertagids;
	trx_uint64_t				triggerid = 0, functionid = 0, triggerdepid = 0, triggerid_up, triggertagid;
	char					*sql = NULL, *function_esc, *parameter_esc;
	size_t					sql_alloc = 8 * TRX_KIBIBYTE, sql_offset = 0;
	trx_db_insert_t				db_insert, db_insert_tdiscovery, db_insert_tfunctions, db_insert_tdepends,
						db_insert_ttags;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&upd_functions);
	trx_vector_uint64_create(&del_functionids);
	trx_vector_uint64_create(&del_triggerdepids);
	trx_vector_uint64_create(&del_triggertagids);

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & TRX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		if (0 == trigger->triggerid)
			new_triggers++;
		else if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE))
			upd_triggers++;

		for (j = 0; j < trigger->functions.values_num; j++)
		{
			function = (trx_lld_function_t *)trigger->functions.values[j];

			if (0 != (function->flags & TRX_FLAG_LLD_FUNCTION_DELETE))
			{
				trx_vector_uint64_append(&del_functionids, function->functionid);
				continue;
			}

			if (0 == (function->flags & TRX_FLAG_LLD_FUNCTION_DISCOVERED))
				continue;

			if (0 == function->functionid)
				new_functions++;
			else if (0 != (function->flags & TRX_FLAG_LLD_FUNCTION_UPDATE))
				trx_vector_ptr_append(&upd_functions, function);
		}

		for (j = 0; j < trigger->dependencies.values_num; j++)
		{
			dependency = (trx_lld_dependency_t *)trigger->dependencies.values[j];

			if (0 != (dependency->flags & TRX_FLAG_LLD_DEPENDENCY_DELETE))
			{
				trx_vector_uint64_append(&del_triggerdepids, dependency->triggerdepid);
				continue;
			}

			if (0 == (dependency->flags & TRX_FLAG_LLD_DEPENDENCY_DISCOVERED))
				continue;

			if (0 == dependency->triggerdepid)
				new_dependencies++;
		}

		for (j = 0; j < trigger->tags.values_num; j++)
		{
			tag = (trx_lld_tag_t *)trigger->tags.values[j];

			if (0 != (tag->flags & TRX_FLAG_LLD_TAG_DELETE))
			{
				trx_vector_uint64_append(&del_triggertagids, tag->triggertagid);
				continue;
			}

			if (0 == (tag->flags & TRX_FLAG_LLD_TAG_DISCOVERED))
				continue;

			if (0 == tag->triggertagid)
				new_tags++;
			else if (0 != (tag->flags & TRX_FLAG_LLD_TAG_UPDATE))
				upd_tags++;
		}
	}

	if (0 == new_triggers && 0 == new_functions && 0 == new_dependencies && 0 == upd_triggers &&
			0 == upd_functions.values_num && 0 == del_functionids.values_num &&
			0 == del_triggerdepids.values_num && 0 == new_tags && 0 == upd_tags &&
			0 == del_triggertagids.values_num)
	{
		goto out;
	}

	DBbegin();

	if (SUCCEED != DBlock_hostid(hostid))
	{
		/* the host was removed while processing lld rule */
		DBrollback();
		ret = FAIL;
		goto out;
	}

	if (0 != new_triggers)
	{
		triggerid = DBget_maxid_num("triggers", new_triggers);

		trx_db_insert_prepare(&db_insert, "triggers", "triggerid", "description", "expression", "priority",
				"status", "comments", "url", "type", "value", "state", "flags", "recovery_mode",
				"recovery_expression", "correlation_mode", "correlation_tag", "manual_close", "opdata",
				NULL);

		trx_db_insert_prepare(&db_insert_tdiscovery, "trigger_discovery", "triggerid", "parent_triggerid",
				NULL);
	}

	if (0 != new_functions)
	{
		functionid = DBget_maxid_num("functions", new_functions);

		trx_db_insert_prepare(&db_insert_tfunctions, "functions", "functionid", "itemid", "triggerid",
				"name", "parameter", NULL);
	}

	if (0 != new_dependencies)
	{
		triggerdepid = DBget_maxid_num("trigger_depends", new_dependencies);

		trx_db_insert_prepare(&db_insert_tdepends, "trigger_depends", "triggerdepid", "triggerid_down",
				"triggerid_up", NULL);
	}

	if (0 != new_tags)
	{
		triggertagid = DBget_maxid_num("trigger_tag", new_tags);

		trx_db_insert_prepare(&db_insert_ttags, "trigger_tag", "triggertagid", "triggerid", "tag", "value",
				NULL);
	}

	if (0 != upd_triggers || 0 != upd_functions.values_num || 0 != del_functionids.values_num ||
			0 != del_triggerdepids.values_num || 0 != upd_tags || 0 != del_triggertagids.values_num)
	{
		sql = (char *)trx_malloc(sql, sql_alloc);
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	for (i = 0; i < triggers->values_num; i++)
	{
		char	*description_esc, *expression_esc, *comments_esc, *url_esc, *value_esc, *opdata_esc;
		int	index;

		trigger = (trx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & TRX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		index = trx_vector_ptr_bsearch(trigger_prototypes, &trigger->parent_triggerid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes->values[index];

		for (j = 0; j < trigger->functions.values_num; j++)
		{
			function = (trx_lld_function_t *)trigger->functions.values[j];

			if (0 != (function->flags & TRX_FLAG_LLD_FUNCTION_DELETE))
				continue;

			if (0 == (function->flags & TRX_FLAG_LLD_FUNCTION_DISCOVERED))
				continue;

			if (0 == function->functionid)
			{
				trx_db_insert_add_values(&db_insert_tfunctions, functionid, function->itemid,
						(0 == trigger->triggerid ? triggerid : trigger->triggerid),
						function->function, function->parameter);

				function->functionid = functionid++;
			}
		}

		if (0 == trigger->triggerid || 0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_EXPRESSION))
			lld_expression_create(&trigger->expression, &trigger->functions);

		if (0 == trigger->triggerid || 0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_EXPRESSION))
			lld_expression_create(&trigger->recovery_expression, &trigger->functions);

		if (0 == trigger->triggerid)
		{
			trx_db_insert_add_values(&db_insert, triggerid, trigger->description, trigger->expression,
					(int)trigger_prototype->priority, (int)trigger_prototype->status,
					trigger->comments, trigger->url, (int)trigger_prototype->type,
					(int)TRIGGER_VALUE_OK, (int)TRIGGER_STATE_NORMAL,
					(int)TRX_FLAG_DISCOVERY_CREATED, (int)trigger_prototype->recovery_mode,
					trigger->recovery_expression, (int)trigger_prototype->correlation_mode,
					trigger->correlation_tag, (int)trigger_prototype->manual_close,
					trigger->opdata);

			trx_db_insert_add_values(&db_insert_tdiscovery, triggerid, trigger->parent_triggerid);

			trigger->triggerid = triggerid++;
		}
		else if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE))
		{
			const char	*d = "";

			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update triggers set ");

			if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION))
			{
				description_esc = DBdyn_escape_string(trigger->description);
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "description='%s'",
						description_esc);
				trx_free(description_esc);
				d = ",";
			}

			if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_EXPRESSION))
			{
				expression_esc = DBdyn_escape_string(trigger->expression);
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sexpression='%s'", d,
						expression_esc);
				trx_free(expression_esc);
				d = ",";
			}

			if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_EXPRESSION))
			{
				expression_esc = DBdyn_escape_string(trigger->recovery_expression);
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%srecovery_expression='%s'", d,
						expression_esc);
				trx_free(expression_esc);
				d = ",";
			}

			if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_MODE))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%srecovery_mode=%d", d,
						(int)trigger_prototype->recovery_mode);
				d = ",";
			}

			if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_TYPE))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%stype=%d", d,
						(int)trigger_prototype->type);
				d = ",";
			}

			if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_PRIORITY))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%spriority=%d", d,
						(int)trigger_prototype->priority);
				d = ",";
			}

			if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_COMMENTS))
			{
				comments_esc = DBdyn_escape_string(trigger->comments);
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%scomments='%s'", d, comments_esc);
				trx_free(comments_esc);
				d = ",";
			}

			if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_URL))
			{
				url_esc = DBdyn_escape_string(trigger->url);
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%surl='%s'", d, url_esc);
				trx_free(url_esc);
				d = ",";
			}

			if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_MODE))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%scorrelation_mode=%d", d,
						(int)trigger_prototype->correlation_mode);
				d = ",";
			}

			if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_TAG))
			{
				value_esc = DBdyn_escape_string(trigger->correlation_tag);
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%scorrelation_tag='%s'", d,
						value_esc);
				trx_free(value_esc);
				d = ",";
			}

			if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_MANUAL_CLOSE))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%smanual_close=%d", d,
						(int)trigger_prototype->manual_close);
				d = ",";
			}

			if (0 != (trigger->flags & TRX_FLAG_LLD_TRIGGER_UPDATE_OPDATA))
			{
				opdata_esc = DBdyn_escape_string(trigger->opdata);
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sopdata='%s'", d, opdata_esc);
				trx_free(opdata_esc);
			}

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					" where triggerid=" TRX_FS_UI64 ";\n", trigger->triggerid);
		}
	}

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & TRX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		for (j = 0; j < trigger->dependencies.values_num; j++)
		{
			dependency = (trx_lld_dependency_t *)trigger->dependencies.values[j];

			if (0 != (dependency->flags & TRX_FLAG_LLD_DEPENDENCY_DELETE))
				continue;

			if (0 == (dependency->flags & TRX_FLAG_LLD_DEPENDENCY_DISCOVERED))
				continue;

			if (0 == dependency->triggerdepid)
			{
				triggerid_up = (NULL == dependency->trigger_up ? dependency->triggerid_up :
						dependency->trigger_up->triggerid);

				trx_db_insert_add_values(&db_insert_tdepends, triggerdepid, trigger->triggerid,
						triggerid_up);

				dependency->triggerdepid = triggerdepid++;
			}
		}
	}

	/* create/update trigger tags */
	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & TRX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		for (j = 0; j < trigger->tags.values_num; j++)
		{
			char	*value_esc;

			tag = (trx_lld_tag_t *)trigger->tags.values[j];

			if (0 != (tag->flags & TRX_FLAG_LLD_TAG_DELETE))
				continue;

			if (0 == (tag->flags & TRX_FLAG_LLD_TAG_DISCOVERED))
				continue;

			if (0 == tag->triggertagid)
			{
				tag->triggertagid = triggertagid++;
				trx_db_insert_add_values(&db_insert_ttags, tag->triggertagid, trigger->triggerid,
						tag->tag, tag->value);
			}
			else if (0 != (tag->flags & TRX_FLAG_LLD_TAG_UPDATE))
			{
				const char	*d = "";

				trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update trigger_tag set ");

				if (0 != (tag->flags & TRX_FLAG_LLD_TAG_UPDATE_TAG))
				{
					value_esc = DBdyn_escape_string(tag->tag);
					trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "tag='%s'", value_esc);
					trx_free(value_esc);
					d = ",";
				}

				if (0 != (tag->flags & TRX_FLAG_LLD_TAG_UPDATE_VALUE))
				{
					value_esc = DBdyn_escape_string(tag->value);
					trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%svalue='%s'", d, value_esc);
					trx_free(value_esc);
				}

				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
						" where triggertagid=" TRX_FS_UI64 ";\n", tag->triggertagid);
			}
		}
	}

	trx_vector_ptr_sort(&upd_functions, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < upd_functions.values_num; i++)
	{
		const char	*d = "";

		function = (trx_lld_function_t *)upd_functions.values[i];

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update functions set ");

		if (0 != (function->flags & TRX_FLAG_LLD_FUNCTION_UPDATE_ITEMID))
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "itemid=" TRX_FS_UI64,
					function->itemid);
			d = ",";
		}

		if (0 != (function->flags & TRX_FLAG_LLD_FUNCTION_UPDATE_FUNCTION))
		{
			function_esc = DBdyn_escape_string(function->function);
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sname='%s'", d,
					function_esc);
			trx_free(function_esc);
			d = ",";
		}

		if (0 != (function->flags & TRX_FLAG_LLD_FUNCTION_UPDATE_PARAMETER))
		{
			parameter_esc = DBdyn_escape_string(function->parameter);
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sparameter='%s'", d,
					parameter_esc);
			trx_free(parameter_esc);
		}

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				" where functionid=" TRX_FS_UI64 ";\n", function->functionid);
	}

	if (0 != del_functionids.values_num)
	{
		trx_vector_uint64_sort(&del_functionids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from functions where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "functionid",
				del_functionids.values, del_functionids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	if (0 != del_triggerdepids.values_num)
	{
		trx_vector_uint64_sort(&del_triggerdepids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from trigger_depends where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerdepid",
				del_triggerdepids.values, del_triggerdepids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	if (0 != del_triggertagids.values_num)
	{
		trx_vector_uint64_sort(&del_triggertagids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from trigger_tag where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggertagid",
				del_triggertagids.values, del_triggertagids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	if (0 != upd_triggers || 0 != upd_functions.values_num || 0 != del_functionids.values_num ||
			0 != del_triggerdepids.values_num || 0 != upd_tags || 0 != del_triggertagids.values_num)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);
		DBexecute("%s", sql);
		trx_free(sql);
	}

	if (0 != new_triggers)
	{
		trx_db_insert_execute(&db_insert);
		trx_db_insert_clean(&db_insert);

		trx_db_insert_execute(&db_insert_tdiscovery);
		trx_db_insert_clean(&db_insert_tdiscovery);
	}

	if (0 != new_functions)
	{
		trx_db_insert_execute(&db_insert_tfunctions);
		trx_db_insert_clean(&db_insert_tfunctions);
	}

	if (0 != new_dependencies)
	{
		trx_db_insert_execute(&db_insert_tdepends);
		trx_db_insert_clean(&db_insert_tdepends);
	}

	if (0 != new_tags)
	{
		trx_db_insert_execute(&db_insert_ttags);
		trx_db_insert_clean(&db_insert_ttags);
	}

	DBcommit();
out:
	trx_vector_uint64_destroy(&del_triggertagids);
	trx_vector_uint64_destroy(&del_triggerdepids);
	trx_vector_uint64_destroy(&del_functionids);
	trx_vector_ptr_destroy(&upd_functions);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/* hash/comparison functions to support cache/vector lookups by trigger reference */
static trx_hash_t	trx_lld_trigger_ref_hash_func(const void *data)
{
	trx_hash_t			hash;
	const trx_lld_trigger_node_t	*trigger_node = (const trx_lld_trigger_node_t *)data;
	void				*ptr = NULL;

	hash = TRX_DEFAULT_UINT64_HASH_ALGO(&trigger_node->trigger_ref.triggerid,
			sizeof(trigger_node->trigger_ref.triggerid), TRX_DEFAULT_HASH_SEED);

	if (0 == trigger_node->trigger_ref.triggerid)
		ptr = trigger_node->trigger_ref.trigger;

	return TRX_DEFAULT_PTR_HASH_ALGO(&ptr, sizeof(trigger_node->trigger_ref.trigger), hash);
}

static int	trx_lld_trigger_ref_compare_func(const void *d1, const void *d2)
{
	const trx_lld_trigger_node_t	*n1 = (const trx_lld_trigger_node_t *)d1;
	const trx_lld_trigger_node_t	*n2 = (const trx_lld_trigger_node_t *)d2;

	TRX_RETURN_IF_NOT_EQUAL(n1->trigger_ref.triggerid, n2->trigger_ref.triggerid);

	/* Don't check pointer if id matches. If the reference was loaded from database it will not have pointer. */
	if (0 != n1->trigger_ref.triggerid)
		return 0;

	TRX_RETURN_IF_NOT_EQUAL(n1->trigger_ref.trigger, n2->trigger_ref.trigger);

	return 0;
}

/* comparison function to determine trigger dependency validation order */
static int	trx_lld_trigger_node_compare_func(const void *d1, const void *d2)
{
	const trx_lld_trigger_node_t	*n1 = *(const trx_lld_trigger_node_t **)d1;
	const trx_lld_trigger_node_t	*n2 = *(const trx_lld_trigger_node_t **)d2;

	/* sort in ascending order, but ensure that existing triggers are first */
	if (0 != n1->trigger_ref.triggerid && 0 == n2->trigger_ref.triggerid)
		return -1;

	/* give priority to nodes with less parents */
	TRX_RETURN_IF_NOT_EQUAL(n1->parents, n2->parents);

	/* compare ids */
	TRX_RETURN_IF_NOT_EQUAL(n1->trigger_ref.triggerid, n2->trigger_ref.triggerid);

	/* Don't check pointer if id matches. If the reference was loaded from database it will not have pointer. */
	if (0 != n1->trigger_ref.triggerid)
		return 0;

	TRX_RETURN_IF_NOT_EQUAL(n1->trigger_ref.trigger, n2->trigger_ref.trigger);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_cache_append                                         *
 *                                                                            *
 * Purpose: adds a node to trigger cache                                      *
 *                                                                            *
 * Parameters: cache     - [IN] the trigger cache                             *
 *             triggerid - [IN] the trigger id                                *
 *             trigger   - [IN] the trigger data for new triggers             *
 *                                                                            *
 * Return value: the added node                                               *
 *                                                                            *
 ******************************************************************************/
static trx_lld_trigger_node_t	*lld_trigger_cache_append(trx_hashset_t *cache, trx_uint64_t triggerid,
		trx_lld_trigger_t *trigger)
{
	trx_lld_trigger_node_t	node_local;

	node_local.trigger_ref.triggerid = triggerid;
	node_local.trigger_ref.trigger = trigger;
	node_local.trigger_ref.flags = 0;
	node_local.iter_num = 0;
	node_local.parents = 0;

	trx_vector_ptr_create(&node_local.dependencies);

	return (trx_lld_trigger_node_t *)trx_hashset_insert(cache, &node_local, sizeof(node_local));
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_cache_add_trigger_node                               *
 *                                                                            *
 * Purpose: add trigger and all triggers related to it to trigger dependency  *
 *          validation cache.                                                 *
 *                                                                            *
 * Parameters: cache           - [IN] the trigger cache                       *
 *             trigger         - [IN] the trigger to add                      *
 *             triggerids_up   - [OUT] identifiers of generic trigger         *
 *                                     dependents                             *
 *             triggerids_down - [OUT] identifiers of generic trigger         *
 *                                     dependencies                           *
 *                                                                            *
 ******************************************************************************/
static void	lld_trigger_cache_add_trigger_node(trx_hashset_t *cache, trx_lld_trigger_t *trigger,
		trx_vector_uint64_t *triggerids_up, trx_vector_uint64_t *triggerids_down)
{
	trx_lld_trigger_ref_t	*trigger_ref;
	trx_lld_trigger_node_t	*trigger_node, trigger_node_local;
	trx_lld_dependency_t	*dependency;
	int			i;

	trigger_node_local.trigger_ref.triggerid = trigger->triggerid;
	trigger_node_local.trigger_ref.trigger = trigger;

	if (NULL != (trigger_node = (trx_lld_trigger_node_t *)trx_hashset_search(cache, &trigger_node_local)))
		return;

	trigger_node = lld_trigger_cache_append(cache, trigger->triggerid, trigger);

	for (i = 0; i < trigger->dependencies.values_num; i++)
	{
		dependency = (trx_lld_dependency_t *)trigger->dependencies.values[i];

		if (0 == (dependency->flags & TRX_FLAG_LLD_DEPENDENCY_DISCOVERED))
			continue;

		trigger_ref = (trx_lld_trigger_ref_t *)trx_malloc(NULL, sizeof(trx_lld_trigger_ref_t));

		trigger_ref->triggerid = dependency->triggerid_up;
		trigger_ref->trigger = dependency->trigger_up;
		trigger_ref->flags = (0 == dependency->triggerdepid ? TRX_LLD_TRIGGER_DEPENDENCY_NEW :
				TRX_LLD_TRIGGER_DEPENDENCY_NORMAL);

		trx_vector_ptr_append(&trigger_node->dependencies, trigger_ref);

		if (NULL == trigger_ref->trigger)
		{
			trigger_node_local.trigger_ref.triggerid = trigger_ref->triggerid;
			trigger_node_local.trigger_ref.trigger = NULL;

			if (NULL == trx_hashset_search(cache, &trigger_node_local))
			{
				trx_vector_uint64_append(triggerids_up, trigger_ref->triggerid);
				trx_vector_uint64_append(triggerids_down, trigger_ref->triggerid);

				lld_trigger_cache_append(cache, trigger_ref->triggerid, NULL);
			}
		}
	}

	if (0 != trigger->triggerid)
		trx_vector_uint64_append(triggerids_up, trigger->triggerid);

	for (i = 0; i < trigger->dependents.values_num; i++)
	{
		lld_trigger_cache_add_trigger_node(cache, (trx_lld_trigger_t *)trigger->dependents.values[i], triggerids_up,
				triggerids_down);
	}

	for (i = 0; i < trigger->dependencies.values_num; i++)
	{
		dependency = (trx_lld_dependency_t *)trigger->dependencies.values[i];

		if (NULL != dependency->trigger_up)
		{
			lld_trigger_cache_add_trigger_node(cache, dependency->trigger_up, triggerids_up,
					triggerids_down);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_cache_init                                           *
 *                                                                            *
 * Purpose: initializes trigger cache used to perform trigger dependency      *
 *          validation                                                        *
 *                                                                            *
 * Parameters: cache    - [IN] the trigger cache                              *
 *             triggers - [IN] the discovered triggers                        *
 *                                                                            *
 * Comments: Triggers with new dependencies and.all triggers related to them  *
 *           are added to cache.                                              *
 *                                                                            *
 ******************************************************************************/
static void	lld_trigger_cache_init(trx_hashset_t *cache, trx_vector_ptr_t *triggers)
{
	trx_vector_uint64_t	triggerids_up, triggerids_down;
	int			i, j;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset;
	DB_RESULT		result;
	DB_ROW			row;
	trx_lld_trigger_ref_t	*trigger_ref;
	trx_lld_trigger_node_t	*trigger_node, trigger_node_local;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_hashset_create(cache, triggers->values_num, trx_lld_trigger_ref_hash_func,
			trx_lld_trigger_ref_compare_func);

	trx_vector_uint64_create(&triggerids_down);
	trx_vector_uint64_create(&triggerids_up);

	/* add all triggers with new dependencies to trigger cache */
	for (i = 0; i < triggers->values_num; i++)
	{
		trx_lld_trigger_t	*trigger = (trx_lld_trigger_t *)triggers->values[i];

		for (j = 0; j < trigger->dependencies.values_num; j++)
		{
			trx_lld_dependency_t	*dependency = (trx_lld_dependency_t *)trigger->dependencies.values[j];

			if (0 == dependency->triggerdepid)
				break;
		}

		if (j != trigger->dependencies.values_num)
			lld_trigger_cache_add_trigger_node(cache, trigger, &triggerids_up, &triggerids_down);
	}

	/* keep trying to load generic dependents/dependencies until there are nothing to load */
	while (0 != triggerids_up.values_num || 0 != triggerids_down.values_num)
	{
		/* load dependents */
		if (0 != triggerids_down.values_num)
		{
			sql_offset = 0;
			trx_vector_uint64_sort(&triggerids_down, TRX_DEFAULT_UINT64_COMPARE_FUNC);
			trx_vector_uint64_uniq(&triggerids_down, TRX_DEFAULT_UINT64_COMPARE_FUNC);

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"select td.triggerid_down,td.triggerid_up"
					" from trigger_depends td"
						" left join triggers t"
							" on td.triggerid_up=t.triggerid"
					" where t.flags<>%d"
						" and", TRX_FLAG_DISCOVERY_PROTOTYPE);
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "td.triggerid_down",
					triggerids_down.values, triggerids_down.values_num);

			trx_vector_uint64_clear(&triggerids_down);

			result = DBselect("%s", sql);

			while (NULL != (row = DBfetch(result)))
			{
				int			new_node = 0;
				trx_lld_trigger_node_t	*trigger_node_up;

				TRX_STR2UINT64(trigger_node_local.trigger_ref.triggerid, row[1]);

				if (NULL == (trigger_node_up = (trx_lld_trigger_node_t *)trx_hashset_search(cache, &trigger_node_local)))
				{
					trigger_node_up = lld_trigger_cache_append(cache,
							trigger_node_local.trigger_ref.triggerid, NULL);
					new_node = 1;
				}

				TRX_STR2UINT64(trigger_node_local.trigger_ref.triggerid, row[0]);

				if (NULL == (trigger_node = (trx_lld_trigger_node_t *)trx_hashset_search(cache, &trigger_node_local)))
				{
					THIS_SHOULD_NEVER_HAPPEN;
					continue;
				}

				/* check if the dependency is not already registered in cache */
				for (i = 0; i < trigger_node->dependencies.values_num; i++)
				{
					trigger_ref = (trx_lld_trigger_ref_t *)trigger_node->dependencies.values[i];

					/* references to generic triggers will always have valid id value */
					if (trigger_ref->triggerid == trigger_node_up->trigger_ref.triggerid)
						break;
				}

				/* if the dependency was not found - add it */
				if (i == trigger_node->dependencies.values_num)
				{
					trigger_ref = (trx_lld_trigger_ref_t *)trx_malloc(NULL,
							sizeof(trx_lld_trigger_ref_t));

					trigger_ref->triggerid = trigger_node_up->trigger_ref.triggerid;
					trigger_ref->trigger = NULL;
					trigger_ref->flags = TRX_LLD_TRIGGER_DEPENDENCY_NORMAL;

					trx_vector_ptr_append(&trigger_node->dependencies, trigger_ref);

					trigger_node_up->parents++;
				}

				if (1 == new_node)
				{
					/* if the trigger was added to cache, we must check its dependencies */
					trx_vector_uint64_append(&triggerids_up,
							trigger_node_up->trigger_ref.triggerid);
					trx_vector_uint64_append(&triggerids_down,
							trigger_node_up->trigger_ref.triggerid);
				}
			}

			DBfree_result(result);
		}

		/* load dependencies */
		if (0 != triggerids_up.values_num)
		{
			sql_offset = 0;
			trx_vector_uint64_sort(&triggerids_up, TRX_DEFAULT_UINT64_COMPARE_FUNC);
			trx_vector_uint64_uniq(&triggerids_up, TRX_DEFAULT_UINT64_COMPARE_FUNC);

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"select td.triggerid_down"
					" from trigger_depends td"
						" left join triggers t"
							" on t.triggerid=td.triggerid_down"
					" where t.flags<>%d"
						" and", TRX_FLAG_DISCOVERY_PROTOTYPE);
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "td.triggerid_up", triggerids_up.values,
					triggerids_up.values_num);

			trx_vector_uint64_clear(&triggerids_up);

			result = DBselect("%s", sql);

			while (NULL != (row = DBfetch(result)))
			{
				TRX_STR2UINT64(trigger_node_local.trigger_ref.triggerid, row[0]);

				if (NULL != trx_hashset_search(cache, &trigger_node_local))
					continue;

				lld_trigger_cache_append(cache, trigger_node_local.trigger_ref.triggerid, NULL);

				trx_vector_uint64_append(&triggerids_up, trigger_node_local.trigger_ref.triggerid);
				trx_vector_uint64_append(&triggerids_down, trigger_node_local.trigger_ref.triggerid);
			}

			DBfree_result(result);
		}

	}

	trx_free(sql);

	trx_vector_uint64_destroy(&triggerids_up);
	trx_vector_uint64_destroy(&triggerids_down);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_trigger_cache_clean                                          *
 *                                                                            *
 * Purpose: releases resources allocated by trigger cache                     *
 *          validation                                                        *
 *                                                                            *
 * Parameters: cache - [IN] the trigger cache                                 *
 *                                                                            *
 ******************************************************************************/
static void	trx_trigger_cache_clean(trx_hashset_t *cache)
{
	trx_hashset_iter_t	iter;
	trx_lld_trigger_node_t	*trigger_node;

	trx_hashset_iter_reset(cache, &iter);
	while (NULL != (trigger_node = (trx_lld_trigger_node_t *)trx_hashset_iter_next(&iter)))
	{
		trx_vector_ptr_clear_ext(&trigger_node->dependencies, trx_ptr_free);
		trx_vector_ptr_destroy(&trigger_node->dependencies);
	}

	trx_hashset_destroy(cache);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_dependency_delete                                    *
 *                                                                            *
 * Purpose: removes trigger dependency                                        *
 *                                                                            *
 * Parameters: from  - [IN] the reference to dependent trigger                *
 *             to    - [IN] the reference to trigger the from depends on      *
 *             error - [OUT] the error message                                *
 *                                                                            *
 * Comments: If possible (the dependency loop was introduced by discovered    *
 *           dependencies) the last dependency in the loop will be removed.   *
 *           Otherwise (the triggers in database already had dependency loop) *
 *           the last dependency in the loop will be marked as removed,       *
 *           however the dependency in database will be left intact.          *
 *                                                                            *
 ******************************************************************************/
static void	lld_trigger_dependency_delete(trx_lld_trigger_ref_t *from, trx_lld_trigger_ref_t *to, char **error)
{
	trx_lld_trigger_t	*trigger;
	int			i;
	char			*trigger_desc;

	if (TRX_LLD_TRIGGER_DEPENDENCY_NORMAL == to->flags)
	{
		/* When old dependency loop has been detected mark it as deleted to avoid   */
		/* infinite recursion during dependency validation, but don't really delete */
		/* it because only new dependencies can be deleted.                         */

		/* in old dependency loop there are no new triggers, so all involved */
		/* triggers have valid identifiers                                   */
		treegix_log(LOG_LEVEL_CRIT, "existing recursive dependency loop detected for trigger \""
				TRX_FS_UI64 "\"", to->triggerid);
		return;
	}

	trigger = from->trigger;

	/* remove the dependency */
	for (i = 0; i < trigger->dependencies.values_num; i++)
	{
		trx_lld_dependency_t	*dependency = (trx_lld_dependency_t *)trigger->dependencies.values[i];

		if ((NULL != dependency->trigger_up && dependency->trigger_up == to->trigger) ||
				(0 != dependency->triggerid_up && dependency->triggerid_up == to->triggerid))
		{
			trx_free(dependency);
			trx_vector_ptr_remove(&trigger->dependencies, i);

			break;
		}
	}

	if (0 != from->triggerid)
		trigger_desc = trx_dsprintf(NULL, TRX_FS_UI64, from->triggerid);
	else
		trigger_desc = trx_strdup(NULL, from->trigger->description);

	*error = trx_strdcatf(*error, "Cannot create all trigger \"%s\" dependencies:"
			" recursion too deep.\n", trigger_desc);

	trx_free(trigger_desc);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_dependencies_iter                                    *
 *                                                                            *
 * Purpose: iterates through trigger dependencies to find dependency loops    *
 *                                                                            *
 * Parameters: cache         - [IN] the trigger cache                         *
 *             triggers      - [IN] the discovered triggers                   *
 *             trigger_node  - [IN] the trigger to check                      *
 *             iter          - [IN] the dependency iterator                   *
 *             level         - [IN] the dependency level                      *
 *             error         - [OUT] the error message                        *
 *                                                                            *
 * Return value: SUCCEED - the trigger's dependency chain in valid            *
 *               FAIL    - a dependency loop was detected                     *
 *                                                                            *
 * Comments: If the validation fails the offending dependency is removed.     *
 *                                                                            *
 ******************************************************************************/
static int	lld_trigger_dependencies_iter(trx_hashset_t *cache, trx_vector_ptr_t *triggers,
		trx_lld_trigger_node_t *trigger_node, trx_lld_trigger_node_iter_t *iter, int level, char **error)
{
	int				i;
	trx_lld_trigger_ref_t		*trigger_ref;
	trx_lld_trigger_node_t		*trigger_node_up;
	trx_lld_trigger_node_iter_t	child_iter, *piter;

	if (trigger_node->iter_num == iter->iter_num || TRX_TRIGGER_DEPENDENCY_LEVELS_MAX < level)
	{
		/* dependency loop detected, resolve it by deleting corresponding dependency */
		lld_trigger_dependency_delete(iter->ref_from, iter->ref_to, error);

		/* mark the dependency as removed */
		iter->ref_to->flags = TRX_LLD_TRIGGER_DEPENDENCY_DELETE;

		return FAIL;
	}

	trigger_node->iter_num = iter->iter_num;

	for (i = 0; i < trigger_node->dependencies.values_num; i++)
	{
		trigger_ref = (trx_lld_trigger_ref_t *)trigger_node->dependencies.values[i];

		/* skip dependencies marked as deleted */
		if (TRX_LLD_TRIGGER_DEPENDENCY_DELETE == trigger_ref->flags)
			continue;

		if (NULL == (trigger_node_up = (trx_lld_trigger_node_t *)trx_hashset_search(cache, trigger_ref)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		/* Remember last dependency that could be cut.                         */
		/* It should be either a last new dependency or just a last dependency */
		/* if no new dependencies were encountered.                            */
		if (TRX_LLD_TRIGGER_DEPENDENCY_NEW == trigger_ref->flags || NULL == iter->ref_to ||
				TRX_LLD_TRIGGER_DEPENDENCY_NORMAL == iter->ref_to->flags)
		{
			child_iter.ref_from = &trigger_node->trigger_ref;
			child_iter.ref_to = trigger_ref;
			child_iter.iter_num = iter->iter_num;

			piter = &child_iter;
		}
		else
			piter = iter;

		if (FAIL == lld_trigger_dependencies_iter(cache, triggers, trigger_node_up, piter, level + 1, error))
			return FAIL;
	}

	trigger_node->iter_num = 0;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_dependencies_validate                                *
 *                                                                            *
 * Purpose: validate discovered trigger dependencies                          *
 *                                                                            *
 * Parameters: triggers - [IN] the discovered triggers                        *
 *             error    - [OUT] the error message                             *
 *             triggers - [IN] the discovered triggers                        *
 *             trigger  - [IN] the trigger to check                           *
 *             iter     - [IN] the dependency iterator                        *
 *             level    - [IN] the dependency level                           *
 *                                                                            *
 * Comments: During validation the dependency loops will be resolved by       *
 *           removing offending dependencies.                                 *
 *                                                                            *
 ******************************************************************************/
static void	lld_trigger_dependencies_validate(trx_vector_ptr_t *triggers, char **error)
{
	trx_hashset_t			cache;
	trx_hashset_iter_t		iter;
	trx_lld_trigger_node_t		*trigger_node, *trigger_node_up;
	trx_lld_trigger_node_iter_t	node_iter = {0};
	trx_vector_ptr_t		nodes;
	int				i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	lld_trigger_cache_init(&cache, triggers);

	/* Perform dependency validation in the order of trigger ids and starting with parentless triggers. */
	/* This will give some consistency in choosing what dependencies should be deleted in the case of   */
	/* recursion.                                                                                       */
	trx_vector_ptr_create(&nodes);
	trx_vector_ptr_reserve(&nodes, cache.num_data);

	trx_hashset_iter_reset(&cache, &iter);
	while (NULL != (trigger_node = (trx_lld_trigger_node_t *)trx_hashset_iter_next(&iter)))
	{
		for (i = 0; i < trigger_node->dependencies.values_num; i++)
		{
			if (NULL == (trigger_node_up = (trx_lld_trigger_node_t *)trx_hashset_search(&cache,
					trigger_node->dependencies.values[i])))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			trigger_node_up->parents++;
		}
		trx_vector_ptr_append(&nodes, trigger_node);
	}

	trx_vector_ptr_sort(&nodes, trx_lld_trigger_node_compare_func);

	for (i = 0; i < nodes.values_num; i++)
	{
		if (NULL == (trigger_node = (trx_lld_trigger_node_t *)trx_hashset_search(&cache, (trx_lld_trigger_node_t *)nodes.values[i])))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		/* If dependency iterator returns false it means that dependency loop was detected */
		/* (and resolved). In this case we have to validate dependencies for this trigger  */
		/* again.                                                                          */
		do
		{
			node_iter.iter_num++;
			node_iter.ref_from = NULL;
			node_iter.ref_to = NULL;
		}
		while (SUCCEED != lld_trigger_dependencies_iter(&cache, triggers, trigger_node, &node_iter, 0, error));
	}

	trx_vector_ptr_destroy(&nodes);
	trx_trigger_cache_clean(&cache);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_update_triggers                                              *
 *                                                                            *
 * Purpose: add or update triggers for discovered items                       *
 *                                                                            *
 * Return value: SUCCEED - if triggers were successfully added/updated or     *
 *                         adding/updating was not necessary                  *
 *               FAIL    - triggers cannot be added/updated                   *
 *                                                                            *
 ******************************************************************************/
int	lld_update_triggers(trx_uint64_t hostid, trx_uint64_t lld_ruleid, const trx_vector_ptr_t *lld_rows,
		const trx_vector_ptr_t *lld_macro_paths, char **error)
{
	trx_vector_ptr_t		trigger_prototypes;
	trx_vector_ptr_t		triggers;
	trx_vector_ptr_t		items;
	trx_lld_trigger_t		*trigger;
	trx_lld_trigger_prototype_t	*trigger_prototype;
	int				ret = SUCCEED, i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&trigger_prototypes);

	lld_trigger_prototypes_get(lld_ruleid, &trigger_prototypes);

	if (0 == trigger_prototypes.values_num)
		goto out;

	trx_vector_ptr_create(&triggers);	/* list of triggers which were created or will be created or */
						/* updated by the trigger prototype */
	trx_vector_ptr_create(&items);		/* list of items which are related to the trigger prototypes */

	lld_triggers_get(&trigger_prototypes, &triggers);
	lld_functions_get(&trigger_prototypes, &triggers);
	lld_dependencies_get(&trigger_prototypes, &triggers);
	lld_tags_get(&trigger_prototypes, &triggers);
	lld_items_get(&trigger_prototypes, &items);

	/* simplifying trigger expressions */

	for (i = 0; i < trigger_prototypes.values_num; i++)
	{
		trigger_prototype = (trx_lld_trigger_prototype_t *)trigger_prototypes.values[i];

		lld_expressions_simplify(&trigger_prototype->expression, &trigger_prototype->recovery_expression,
				&trigger_prototype->functions);
	}

	for (i = 0; i < triggers.values_num; i++)
	{
		trigger = (trx_lld_trigger_t *)triggers.values[i];

		lld_expressions_simplify(&trigger->expression, &trigger->recovery_expression, &trigger->functions);
	}

	/* making triggers */

	lld_triggers_make(&trigger_prototypes, &triggers, &items, lld_rows, lld_macro_paths, error);
	lld_triggers_validate(hostid, &triggers, error);
	lld_trigger_dependencies_make(&trigger_prototypes, &triggers, lld_rows, error);
	lld_trigger_dependencies_validate(&triggers, error);
	lld_trigger_tags_make(&trigger_prototypes, &triggers, lld_rows, lld_macro_paths);
	lld_trigger_tags_validate(&triggers, error);
	ret = lld_triggers_save(hostid, &trigger_prototypes, &triggers);

	/* cleaning */

	trx_vector_ptr_clear_ext(&items, (trx_mem_free_func_t)lld_item_free);
	trx_vector_ptr_clear_ext(&triggers, (trx_mem_free_func_t)lld_trigger_free);
	trx_vector_ptr_destroy(&items);
	trx_vector_ptr_destroy(&triggers);
out:
	trx_vector_ptr_clear_ext(&trigger_prototypes, (trx_mem_free_func_t)lld_trigger_prototype_free);
	trx_vector_ptr_destroy(&trigger_prototypes);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}
