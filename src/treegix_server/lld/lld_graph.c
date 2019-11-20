

#include "lld.h"
#include "db.h"
#include "log.h"
#include "trxalgo.h"
#include "trxserver.h"

typedef struct
{
	trx_uint64_t		graphid;
	char			*name;
	char			*name_orig;
	trx_uint64_t		ymin_itemid;
	trx_uint64_t		ymax_itemid;
	trx_vector_ptr_t	gitems;
#define TRX_FLAG_LLD_GRAPH_UNSET			__UINT64_C(0x00000000)
#define TRX_FLAG_LLD_GRAPH_DISCOVERED			__UINT64_C(0x00000001)
#define TRX_FLAG_LLD_GRAPH_UPDATE_NAME			__UINT64_C(0x00000002)
#define TRX_FLAG_LLD_GRAPH_UPDATE_WIDTH			__UINT64_C(0x00000004)
#define TRX_FLAG_LLD_GRAPH_UPDATE_HEIGHT		__UINT64_C(0x00000008)
#define TRX_FLAG_LLD_GRAPH_UPDATE_YAXISMIN		__UINT64_C(0x00000010)
#define TRX_FLAG_LLD_GRAPH_UPDATE_YAXISMAX		__UINT64_C(0x00000020)
#define TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_WORK_PERIOD	__UINT64_C(0x00000040)
#define TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_TRIGGERS		__UINT64_C(0x00000080)
#define TRX_FLAG_LLD_GRAPH_UPDATE_GRAPHTYPE		__UINT64_C(0x00000100)
#define TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_LEGEND		__UINT64_C(0x00000200)
#define TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_3D		__UINT64_C(0x00000400)
#define TRX_FLAG_LLD_GRAPH_UPDATE_PERCENT_LEFT		__UINT64_C(0x00000800)
#define TRX_FLAG_LLD_GRAPH_UPDATE_PERCENT_RIGHT		__UINT64_C(0x00001000)
#define TRX_FLAG_LLD_GRAPH_UPDATE_YMIN_TYPE		__UINT64_C(0x00002000)
#define TRX_FLAG_LLD_GRAPH_UPDATE_YMIN_ITEMID		__UINT64_C(0x00004000)
#define TRX_FLAG_LLD_GRAPH_UPDATE_YMAX_TYPE		__UINT64_C(0x00008000)
#define TRX_FLAG_LLD_GRAPH_UPDATE_YMAX_ITEMID		__UINT64_C(0x00010000)
#define TRX_FLAG_LLD_GRAPH_UPDATE									\
		(TRX_FLAG_LLD_GRAPH_UPDATE_NAME | TRX_FLAG_LLD_GRAPH_UPDATE_WIDTH |			\
		TRX_FLAG_LLD_GRAPH_UPDATE_HEIGHT | TRX_FLAG_LLD_GRAPH_UPDATE_YAXISMIN |			\
		TRX_FLAG_LLD_GRAPH_UPDATE_YAXISMAX | TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_WORK_PERIOD |	\
		TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_TRIGGERS | TRX_FLAG_LLD_GRAPH_UPDATE_GRAPHTYPE |		\
		TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_LEGEND | TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_3D |		\
		TRX_FLAG_LLD_GRAPH_UPDATE_PERCENT_LEFT | TRX_FLAG_LLD_GRAPH_UPDATE_PERCENT_RIGHT |	\
		TRX_FLAG_LLD_GRAPH_UPDATE_YMIN_TYPE | TRX_FLAG_LLD_GRAPH_UPDATE_YMIN_ITEMID |		\
		TRX_FLAG_LLD_GRAPH_UPDATE_YMAX_TYPE | TRX_FLAG_LLD_GRAPH_UPDATE_YMAX_ITEMID)
	trx_uint64_t		flags;
}
trx_lld_graph_t;

typedef struct
{
	trx_uint64_t		gitemid;
	trx_uint64_t		itemid;
	char			*color;
	int			sortorder;
	unsigned char		drawtype;
	unsigned char		yaxisside;
	unsigned char		calc_fnc;
	unsigned char		type;
#define TRX_FLAG_LLD_GITEM_UNSET			__UINT64_C(0x0000)
#define TRX_FLAG_LLD_GITEM_DISCOVERED			__UINT64_C(0x0001)
#define TRX_FLAG_LLD_GITEM_UPDATE_ITEMID		__UINT64_C(0x0002)
#define TRX_FLAG_LLD_GITEM_UPDATE_DRAWTYPE		__UINT64_C(0x0004)
#define TRX_FLAG_LLD_GITEM_UPDATE_SORTORDER		__UINT64_C(0x0008)
#define TRX_FLAG_LLD_GITEM_UPDATE_COLOR			__UINT64_C(0x0010)
#define TRX_FLAG_LLD_GITEM_UPDATE_YAXISSIDE		__UINT64_C(0x0020)
#define TRX_FLAG_LLD_GITEM_UPDATE_CALC_FNC		__UINT64_C(0x0040)
#define TRX_FLAG_LLD_GITEM_UPDATE_TYPE			__UINT64_C(0x0080)
#define TRX_FLAG_LLD_GITEM_UPDATE								\
		(TRX_FLAG_LLD_GITEM_UPDATE_ITEMID | TRX_FLAG_LLD_GITEM_UPDATE_DRAWTYPE |	\
		TRX_FLAG_LLD_GITEM_UPDATE_SORTORDER | TRX_FLAG_LLD_GITEM_UPDATE_COLOR |		\
		TRX_FLAG_LLD_GITEM_UPDATE_YAXISSIDE | TRX_FLAG_LLD_GITEM_UPDATE_CALC_FNC |	\
		TRX_FLAG_LLD_GITEM_UPDATE_TYPE)
#define TRX_FLAG_LLD_GITEM_DELETE			__UINT64_C(0x0100)
	trx_uint64_t		flags;
}
trx_lld_gitem_t;

typedef struct
{
	trx_uint64_t	itemid;
	unsigned char	flags;
}
trx_lld_item_t;

static void	lld_item_free(trx_lld_item_t *item)
{
	trx_free(item);
}

static void	lld_items_free(trx_vector_ptr_t *items)
{
	while (0 != items->values_num)
		lld_item_free((trx_lld_item_t *)items->values[--items->values_num]);
}

static void	lld_gitem_free(trx_lld_gitem_t *gitem)
{
	trx_free(gitem->color);
	trx_free(gitem);
}

static void	lld_gitems_free(trx_vector_ptr_t *gitems)
{
	while (0 != gitems->values_num)
		lld_gitem_free((trx_lld_gitem_t *)gitems->values[--gitems->values_num]);
}

static void	lld_graph_free(trx_lld_graph_t *graph)
{
	lld_gitems_free(&graph->gitems);
	trx_vector_ptr_destroy(&graph->gitems);
	trx_free(graph->name_orig);
	trx_free(graph->name);
	trx_free(graph);
}

static void	lld_graphs_free(trx_vector_ptr_t *graphs)
{
	while (0 != graphs->values_num)
		lld_graph_free((trx_lld_graph_t *)graphs->values[--graphs->values_num]);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_graphs_get                                                   *
 *                                                                            *
 * Purpose: retrieve graphs which were created by the specified graph         *
 *          prototype                                                         *
 *                                                                            *
 * Parameters: parent_graphid - [IN] graph prototype identificator            *
 *             graphs         - [OUT] sorted list of graphs                   *
 *                                                                            *
 ******************************************************************************/
static void	lld_graphs_get(trx_uint64_t parent_graphid, trx_vector_ptr_t *graphs, int width, int height,
		double yaxismin, double yaxismax, unsigned char show_work_period, unsigned char show_triggers,
		unsigned char graphtype, unsigned char show_legend, unsigned char show_3d, double percent_left,
		double percent_right, unsigned char ymin_type, unsigned char ymax_type)
{
	DB_RESULT	result;
	DB_ROW		row;
	trx_lld_graph_t	*graph;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select g.graphid,g.name,g.width,g.height,g.yaxismin,g.yaxismax,g.show_work_period,"
				"g.show_triggers,g.graphtype,g.show_legend,g.show_3d,g.percent_left,g.percent_right,"
				"g.ymin_type,g.ymin_itemid,g.ymax_type,g.ymax_itemid"
			" from graphs g,graph_discovery gd"
			" where g.graphid=gd.graphid"
				" and gd.parent_graphid=" TRX_FS_UI64,
			parent_graphid);

	while (NULL != (row = DBfetch(result)))
	{
		graph = (trx_lld_graph_t *)trx_malloc(NULL, sizeof(trx_lld_graph_t));

		TRX_STR2UINT64(graph->graphid, row[0]);
		graph->name = trx_strdup(NULL, row[1]);
		graph->name_orig = NULL;

		graph->flags = TRX_FLAG_LLD_GRAPH_UNSET;

		if (atoi(row[2]) != width)
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_WIDTH;

		if (atoi(row[3]) != height)
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_HEIGHT;

		if (atof(row[4]) != yaxismin)
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_YAXISMIN;

		if (atof(row[5]) != yaxismax)
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_YAXISMAX;

		if ((unsigned char)atoi(row[6]) != show_work_period)
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_WORK_PERIOD;

		if ((unsigned char)atoi(row[7]) != show_triggers)
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_TRIGGERS;

		if ((unsigned char)atoi(row[8]) != graphtype)
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_GRAPHTYPE;

		if ((unsigned char)atoi(row[9]) != show_legend)
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_LEGEND;

		if ((unsigned char)atoi(row[10]) != show_3d)
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_3D;

		if (atof(row[11]) != percent_left)
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_PERCENT_LEFT;

		if (atof(row[12]) != percent_right)
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_PERCENT_RIGHT;

		if ((unsigned char)atoi(row[13]) != ymin_type)
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_YMIN_TYPE;

		TRX_DBROW2UINT64(graph->ymin_itemid, row[14]);

		if ((unsigned char)atoi(row[15]) != ymax_type)
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_YMAX_TYPE;

		TRX_DBROW2UINT64(graph->ymax_itemid, row[16]);

		trx_vector_ptr_create(&graph->gitems);

		trx_vector_ptr_append(graphs, graph);
	}
	DBfree_result(result);

	trx_vector_ptr_sort(graphs, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_gitems_get                                                   *
 *                                                                            *
 * Purpose: retrieve graphs_items which are used by the graph prototype and   *
 *          by selected graphs                                                *
 *                                                                            *
 ******************************************************************************/
static void	lld_gitems_get(trx_uint64_t parent_graphid, trx_vector_ptr_t *gitems_proto,
		trx_vector_ptr_t *graphs)
{
	int			i, index;
	trx_lld_graph_t		*graph;
	trx_lld_gitem_t		*gitem;
	trx_uint64_t		graphid;
	trx_vector_uint64_t	graphids;
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 256, sql_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&graphids);
	trx_vector_uint64_append(&graphids, parent_graphid);

	for (i = 0; i < graphs->values_num; i++)
	{
		graph = (trx_lld_graph_t *)graphs->values[i];

		trx_vector_uint64_append(&graphids, graph->graphid);
	}

	trx_vector_uint64_sort(&graphids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	sql = (char *)trx_malloc(sql, sql_alloc);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select gitemid,graphid,itemid,drawtype,sortorder,color,yaxisside,calc_fnc,type"
			" from graphs_items"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "graphid",
			graphids.values, graphids.values_num);

	result = DBselect("%s", sql);

	trx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		gitem = (trx_lld_gitem_t *)trx_malloc(NULL, sizeof(trx_lld_gitem_t));

		TRX_STR2UINT64(gitem->gitemid, row[0]);
		TRX_STR2UINT64(graphid, row[1]);
		TRX_STR2UINT64(gitem->itemid, row[2]);
		TRX_STR2UCHAR(gitem->drawtype, row[3]);
		gitem->sortorder = atoi(row[4]);
		gitem->color = trx_strdup(NULL, row[5]);
		TRX_STR2UCHAR(gitem->yaxisside, row[6]);
		TRX_STR2UCHAR(gitem->calc_fnc, row[7]);
		TRX_STR2UCHAR(gitem->type, row[8]);

		gitem->flags = TRX_FLAG_LLD_GITEM_UNSET;

		if (graphid == parent_graphid)
		{
			trx_vector_ptr_append(gitems_proto, gitem);
		}
		else if (FAIL != (index = trx_vector_ptr_bsearch(graphs, &graphid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			graph = (trx_lld_graph_t *)graphs->values[index];

			trx_vector_ptr_append(&graph->gitems, gitem);
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
			lld_gitem_free(gitem);
		}
	}
	DBfree_result(result);

	trx_vector_ptr_sort(gitems_proto, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < graphs->values_num; i++)
	{
		graph = (trx_lld_graph_t *)graphs->values[i];

		trx_vector_ptr_sort(&graph->gitems, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	trx_vector_uint64_destroy(&graphids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_get                                                    *
 *                                                                            *
 * Purpose: returns the list of items which are related to the graph          *
 *          prototype                                                         *
 *                                                                            *
 * Parameters: gitems_proto      - [IN] graph prototype's graphs_items        *
 *             ymin_itemid_proto - [IN] graph prototype's ymin_itemid         *
 *             ymax_itemid_proto - [IN] graph prototype's ymax_itemid         *
 *             items             - [OUT] sorted list of items                 *
 *                                                                            *
 ******************************************************************************/
static void	lld_items_get(const trx_vector_ptr_t *gitems_proto, trx_uint64_t ymin_itemid_proto,
		trx_uint64_t ymax_itemid_proto, trx_vector_ptr_t *items)
{
	DB_RESULT		result;
	DB_ROW			row;
	const trx_lld_gitem_t	*gitem;
	trx_lld_item_t		*item;
	trx_vector_uint64_t	itemids;
	int			i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&itemids);

	for (i = 0; i < gitems_proto->values_num; i++)
	{
		gitem = (trx_lld_gitem_t *)gitems_proto->values[i];

		trx_vector_uint64_append(&itemids, gitem->itemid);
	}

	if (0 != ymin_itemid_proto)
		trx_vector_uint64_append(&itemids, ymin_itemid_proto);

	if (0 != ymax_itemid_proto)
		trx_vector_uint64_append(&itemids, ymax_itemid_proto);

	if (0 != itemids.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 256, sql_offset = 0;

		trx_vector_uint64_sort(&itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		sql = (char *)trx_malloc(sql, sql_alloc);

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select itemid,flags"
				" from items"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids.values, itemids.values_num);

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
	}

	trx_vector_uint64_destroy(&itemids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_graph_by_item                                                *
 *                                                                            *
 * Purpose: finds already existing graph, using an item                       *
 *                                                                            *
 * Return value: upon successful completion return pointer to the graph       *
 *                                                                            *
 ******************************************************************************/
static trx_lld_graph_t	*lld_graph_by_item(trx_vector_ptr_t *graphs, trx_uint64_t itemid)
{
	int		i, j;
	trx_lld_graph_t	*graph;
	trx_lld_gitem_t	*gitem;

	for (i = 0; i < graphs->values_num; i++)
	{
		graph = (trx_lld_graph_t *)graphs->values[i];

		if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_DISCOVERED))
			continue;

		for (j = 0; j < graph->gitems.values_num; j++)
		{
			gitem = (trx_lld_gitem_t *)graph->gitems.values[j];

			if (gitem->itemid == itemid)
				return graph;
		}
	}

	return NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_graph_get                                                    *
 *                                                                            *
 * Purpose: finds already existing graph, using an item prototype and items   *
 *          already created by it                                             *
 *                                                                            *
 * Return value: upon successful completion return pointer to the graph       *
 *                                                                            *
 ******************************************************************************/
static trx_lld_graph_t	*lld_graph_get(trx_vector_ptr_t *graphs, const trx_vector_ptr_t *item_links)
{
	int		i;
	trx_lld_graph_t	*graph;

	for (i = 0; i < item_links->values_num; i++)
	{
		const trx_lld_item_link_t	*item_link = (trx_lld_item_link_t *)item_links->values[i];

		if (NULL != (graph = lld_graph_by_item(graphs, item_link->itemid)))
			return graph;
	}

	return NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_get                                                     *
 *                                                                            *
 * Purpose: finds already created item when itemid_proto is an item prototype *
 *          or return itemid_proto as itemid if it's a normal item            *
 *                                                                            *
 * Return value: SUCCEED if item successfully processed, FAIL - otherwise     *
 *                                                                            *
 ******************************************************************************/
static int	lld_item_get(trx_uint64_t itemid_proto, const trx_vector_ptr_t *items,
		const trx_vector_ptr_t *item_links, trx_uint64_t *itemid)
{
	int			index;
	trx_lld_item_t		*item_proto;
	trx_lld_item_link_t	*item_link;

	if (FAIL == (index = trx_vector_ptr_bsearch(items, &itemid_proto, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		return FAIL;

	item_proto = (trx_lld_item_t *)items->values[index];

	if (0 != (item_proto->flags & TRX_FLAG_DISCOVERY_PROTOTYPE))
	{
		index = trx_vector_ptr_bsearch(item_links, &item_proto->itemid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

		if (FAIL == index)
			return FAIL;

		item_link = (trx_lld_item_link_t *)item_links->values[index];

		*itemid = item_link->itemid;
	}
	else
		*itemid = item_proto->itemid;

	return SUCCEED;
}

static int	lld_gitems_make(const trx_vector_ptr_t *gitems_proto, trx_vector_ptr_t *gitems,
		const trx_vector_ptr_t *items, const trx_vector_ptr_t *item_links)
{
	int			i, ret = FAIL;
	const trx_lld_gitem_t	*gitem_proto;
	trx_lld_gitem_t		*gitem;
	trx_uint64_t		itemid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < gitems_proto->values_num; i++)
	{
		gitem_proto = (trx_lld_gitem_t *)gitems_proto->values[i];

		if (SUCCEED != lld_item_get(gitem_proto->itemid, items, item_links, &itemid))
			goto out;

		if (i == gitems->values_num)
		{
			gitem = (trx_lld_gitem_t *)trx_malloc(NULL, sizeof(trx_lld_gitem_t));

			gitem->gitemid = 0;
			gitem->itemid = itemid;
			gitem->drawtype = gitem_proto->drawtype;
			gitem->sortorder = gitem_proto->sortorder;
			gitem->color = trx_strdup(NULL, gitem_proto->color);
			gitem->yaxisside = gitem_proto->yaxisside;
			gitem->calc_fnc = gitem_proto->calc_fnc;
			gitem->type = gitem_proto->type;

			gitem->flags = TRX_FLAG_LLD_GITEM_DISCOVERED;

			trx_vector_ptr_append(gitems, gitem);
		}
		else
		{
			gitem = (trx_lld_gitem_t *)gitems->values[i];

			if (gitem->itemid != itemid)
			{
				gitem->itemid = itemid;
				gitem->flags |= TRX_FLAG_LLD_GITEM_UPDATE_ITEMID;
			}

			if (gitem->drawtype != gitem_proto->drawtype)
			{
				gitem->drawtype = gitem_proto->drawtype;
				gitem->flags |= TRX_FLAG_LLD_GITEM_UPDATE_DRAWTYPE;
			}

			if (gitem->sortorder != gitem_proto->sortorder)
			{
				gitem->sortorder = gitem_proto->sortorder;
				gitem->flags |= TRX_FLAG_LLD_GITEM_UPDATE_SORTORDER;
			}

			if (0 != strcmp(gitem->color, gitem_proto->color))
			{
				gitem->color = trx_strdup(gitem->color, gitem_proto->color);
				gitem->flags |= TRX_FLAG_LLD_GITEM_UPDATE_COLOR;
			}

			if (gitem->yaxisside != gitem_proto->yaxisside)
			{
				gitem->yaxisside = gitem_proto->yaxisside;
				gitem->flags |= TRX_FLAG_LLD_GITEM_UPDATE_YAXISSIDE;
			}

			if (gitem->calc_fnc != gitem_proto->calc_fnc)
			{
				gitem->calc_fnc = gitem_proto->calc_fnc;
				gitem->flags |= TRX_FLAG_LLD_GITEM_UPDATE_CALC_FNC;
			}

			if (gitem->type != gitem_proto->type)
			{
				gitem->type = gitem_proto->type;
				gitem->flags |= TRX_FLAG_LLD_GITEM_UPDATE_TYPE;
			}

			gitem->flags |= TRX_FLAG_LLD_GITEM_DISCOVERED;
		}
	}

	for (; i < gitems->values_num; i++)
	{
		gitem = (trx_lld_gitem_t *)gitems->values[i];

		gitem->flags |= TRX_FLAG_LLD_GITEM_DELETE;
	}

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_graph_make                                                   *
 *                                                                            *
 * Purpose: create a graph based on lld rule and add it to the list           *
 *                                                                            *
 ******************************************************************************/
static void 	lld_graph_make(const trx_vector_ptr_t *gitems_proto, trx_vector_ptr_t *graphs, trx_vector_ptr_t *items,
		const char *name_proto, trx_uint64_t ymin_itemid_proto, trx_uint64_t ymax_itemid_proto,
		const trx_lld_row_t *lld_row, const trx_vector_ptr_t *lld_macro_paths)
{
	trx_lld_graph_t			*graph = NULL;
	char				*buffer = NULL;
	const struct trx_json_parse	*jp_row = &lld_row->jp_row;
	trx_uint64_t			ymin_itemid, ymax_itemid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == ymin_itemid_proto)
		ymin_itemid = 0;
	else if (SUCCEED != lld_item_get(ymin_itemid_proto, items, &lld_row->item_links, &ymin_itemid))
		goto out;

	if (0 == ymax_itemid_proto)
		ymax_itemid = 0;
	else if (SUCCEED != lld_item_get(ymax_itemid_proto, items, &lld_row->item_links, &ymax_itemid))
		goto out;

	if (NULL != (graph = lld_graph_get(graphs, &lld_row->item_links)))
	{
		buffer = trx_strdup(buffer, name_proto);
		substitute_lld_macros(&buffer, jp_row, lld_macro_paths, TRX_MACRO_SIMPLE, NULL, 0);
		trx_lrtrim(buffer, TRX_WHITESPACE);
		if (0 != strcmp(graph->name, buffer))
		{
			graph->name_orig = graph->name;
			graph->name = buffer;
			buffer = NULL;
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_NAME;
		}

		if (graph->ymin_itemid != ymin_itemid)
		{
			graph->ymin_itemid = ymin_itemid;
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_YMIN_ITEMID;
		}

		if (graph->ymax_itemid != ymax_itemid)
		{
			graph->ymax_itemid = ymax_itemid;
			graph->flags |= TRX_FLAG_LLD_GRAPH_UPDATE_YMAX_ITEMID;
		}
	}
	else
	{
		graph = (trx_lld_graph_t *)trx_malloc(NULL, sizeof(trx_lld_graph_t));

		graph->graphid = 0;

		graph->name = trx_strdup(NULL, name_proto);
		graph->name_orig = NULL;
		substitute_lld_macros(&graph->name, jp_row, lld_macro_paths, TRX_MACRO_SIMPLE, NULL, 0);
		trx_lrtrim(graph->name, TRX_WHITESPACE);

		graph->ymin_itemid = ymin_itemid;
		graph->ymax_itemid = ymax_itemid;

		trx_vector_ptr_create(&graph->gitems);

		graph->flags = TRX_FLAG_LLD_GRAPH_UNSET;

		trx_vector_ptr_append(graphs, graph);
	}

	trx_free(buffer);

	if (SUCCEED != lld_gitems_make(gitems_proto, &graph->gitems, items, &lld_row->item_links))
		return;

	graph->flags |= TRX_FLAG_LLD_GRAPH_DISCOVERED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	lld_graphs_make(const trx_vector_ptr_t *gitems_proto, trx_vector_ptr_t *graphs, trx_vector_ptr_t *items,
		const char *name_proto, trx_uint64_t ymin_itemid_proto, trx_uint64_t ymax_itemid_proto,
		const trx_vector_ptr_t *lld_rows, const trx_vector_ptr_t *lld_macro_paths)
{
	int	i;

	for (i = 0; i < lld_rows->values_num; i++)
	{
		trx_lld_row_t	*lld_row = (trx_lld_row_t *)lld_rows->values[i];

		lld_graph_make(gitems_proto, graphs, items, name_proto, ymin_itemid_proto, ymax_itemid_proto, lld_row,
				lld_macro_paths);
	}

	trx_vector_ptr_sort(graphs, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_validate_graph_field                                         *
 *                                                                            *
 ******************************************************************************/
static void	lld_validate_graph_field(trx_lld_graph_t *graph, char **field, char **field_orig, trx_uint64_t flag,
		size_t field_len, char **error)
{
	if (0 == (graph->flags & TRX_FLAG_LLD_GRAPH_DISCOVERED))
		return;

	/* only new graphs or graphs with changed data will be validated */
	if (0 != graph->graphid && 0 == (graph->flags & flag))
		return;

	if (SUCCEED != trx_is_utf8(*field))
	{
		trx_replace_invalid_utf8(*field);
		*error = trx_strdcatf(*error, "Cannot %s graph: value \"%s\" has invalid UTF-8 sequence.\n",
				(0 != graph->graphid ? "update" : "create"), *field);
	}
	else if (trx_strlen_utf8(*field) > field_len)
	{
		*error = trx_strdcatf(*error, "Cannot %s graph: value \"%s\" is too long.\n",
				(0 != graph->graphid ? "update" : "create"), *field);
	}
	else if (TRX_FLAG_LLD_GRAPH_UPDATE_NAME == flag && '\0' == **field)
	{
		*error = trx_strdcatf(*error, "Cannot %s graph: name is empty.\n",
				(0 != graph->graphid ? "update" : "create"));
	}
	else
		return;

	if (0 != graph->graphid)
		lld_field_str_rollback(field, field_orig, &graph->flags, flag);
	else
		graph->flags &= ~TRX_FLAG_LLD_GRAPH_DISCOVERED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_graphs_validate                                              *
 *                                                                            *
 * Parameters: graphs - [IN] sorted list of graphs                            *
 *                                                                            *
 ******************************************************************************/
static void	lld_graphs_validate(trx_uint64_t hostid, trx_vector_ptr_t *graphs, char **error)
{
	int			i, j;
	trx_lld_graph_t		*graph, *graph_b;
	trx_vector_uint64_t	graphids;
	trx_vector_str_t	names;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&graphids);
	trx_vector_str_create(&names);		/* list of graph names */

	/* checking a validity of the fields */

	for (i = 0; i < graphs->values_num; i++)
	{
		graph = (trx_lld_graph_t *)graphs->values[i];

		lld_validate_graph_field(graph, &graph->name, &graph->name_orig,
				TRX_FLAG_LLD_GRAPH_UPDATE_NAME, GRAPH_NAME_LEN, error);
	}

	/* checking duplicated graph names */
	for (i = 0; i < graphs->values_num; i++)
	{
		graph = (trx_lld_graph_t *)graphs->values[i];

		if (0 == (graph->flags & TRX_FLAG_LLD_GRAPH_DISCOVERED))
			continue;

		/* only new graphs or graphs with changed name will be validated */
		if (0 != graph->graphid && 0 == (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_NAME))
			continue;

		for (j = 0; j < graphs->values_num; j++)
		{
			graph_b = (trx_lld_graph_t *)graphs->values[j];

			if (0 == (graph_b->flags & TRX_FLAG_LLD_GRAPH_DISCOVERED) || i == j)
				continue;

			if (0 != strcmp(graph->name, graph_b->name))
				continue;

			*error = trx_strdcatf(*error, "Cannot %s graph:"
						" graph with the same name \"%s\" already exists.\n",
						(0 != graph->graphid ? "update" : "create"), graph->name);

			if (0 != graph->graphid)
			{
				lld_field_str_rollback(&graph->name, &graph->name_orig, &graph->flags,
						TRX_FLAG_LLD_GRAPH_UPDATE_NAME);
			}
			else
				graph->flags &= ~TRX_FLAG_LLD_GRAPH_DISCOVERED;

			break;
		}
	}

	/* checking duplicated graphs in DB */

	for (i = 0; i < graphs->values_num; i++)
	{
		graph = (trx_lld_graph_t *)graphs->values[i];

		if (0 == (graph->flags & TRX_FLAG_LLD_GRAPH_DISCOVERED))
			continue;

		if (0 != graph->graphid)
		{
			trx_vector_uint64_append(&graphids, graph->graphid);

			if (0 == (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_NAME))
				continue;
		}

		trx_vector_str_append(&names, graph->name);
	}

	if (0 != names.values_num)
	{
		DB_RESULT	result;
		DB_ROW		row;
		char		*sql = NULL;
		size_t		sql_alloc = 256, sql_offset = 0;

		sql = (char *)trx_malloc(sql, sql_alloc);

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select g.name"
				" from graphs g,graphs_items gi,items i"
				" where g.graphid=gi.graphid"
					" and gi.itemid=i.itemid"
					" and i.hostid=" TRX_FS_UI64
					" and",
				hostid);
		DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "g.name",
				(const char **)names.values, names.values_num);

		if (0 != graphids.values_num)
		{
			trx_vector_uint64_sort(&graphids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and not");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "g.graphid",
					graphids.values, graphids.values_num);
		}

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			for (i = 0; i < graphs->values_num; i++)
			{
				graph = (trx_lld_graph_t *)graphs->values[i];

				if (0 == (graph->flags & TRX_FLAG_LLD_GRAPH_DISCOVERED))
					continue;

				if (0 == strcmp(graph->name, row[0]))
				{
					*error = trx_strdcatf(*error, "Cannot %s graph:"
							" graph with the same name \"%s\" already exists.\n",
							(0 != graph->graphid ? "update" : "create"), graph->name);

					if (0 != graph->graphid)
					{
						lld_field_str_rollback(&graph->name, &graph->name_orig, &graph->flags,
								TRX_FLAG_LLD_GRAPH_UPDATE_NAME);
					}
					else
						graph->flags &= ~TRX_FLAG_LLD_GRAPH_DISCOVERED;

					continue;
				}
			}
		}
		DBfree_result(result);

		trx_free(sql);
	}

	trx_vector_str_destroy(&names);
	trx_vector_uint64_destroy(&graphids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_graphs_save                                                  *
 *                                                                            *
 * Purpose: add or update graphs in database based on discovery rule          *
 *                                                                            *
 * Return value: SUCCEED - if graphs were successfully saved or saving        *
 *                         was not necessary                                  *
 *               FAIL    - graphs cannot be saved                             *
 *                                                                            *
 ******************************************************************************/
static int	lld_graphs_save(trx_uint64_t hostid, trx_uint64_t parent_graphid, trx_vector_ptr_t *graphs, int width,
		int height, double yaxismin, double yaxismax, unsigned char show_work_period,
		unsigned char show_triggers, unsigned char graphtype, unsigned char show_legend, unsigned char show_3d,
		double percent_left, double percent_right, unsigned char ymin_type, unsigned char ymax_type)
{
	int			ret = SUCCEED, i, j, new_graphs = 0, upd_graphs = 0, new_gitems = 0;
	trx_lld_graph_t		*graph;
	trx_lld_gitem_t		*gitem;
	trx_vector_ptr_t	upd_gitems; 	/* the ordered list of graphs_items which will be updated */
	trx_vector_uint64_t	del_gitemids;

	trx_uint64_t		graphid = 0, gitemid = 0;
	char			*sql = NULL, *name_esc, *color_esc;
	size_t			sql_alloc = 8 * TRX_KIBIBYTE, sql_offset = 0;
	trx_db_insert_t		db_insert, db_insert_gdiscovery, db_insert_gitems;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&upd_gitems);
	trx_vector_uint64_create(&del_gitemids);

	for (i = 0; i < graphs->values_num; i++)
	{
		graph = (trx_lld_graph_t *)graphs->values[i];

		if (0 == (graph->flags & TRX_FLAG_LLD_GRAPH_DISCOVERED))
			continue;

		if (0 == graph->graphid)
			new_graphs++;
		else if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE))
			upd_graphs++;

		for (j = 0; j < graph->gitems.values_num; j++)
		{
			gitem = (trx_lld_gitem_t *)graph->gitems.values[j];

			if (0 != (gitem->flags & TRX_FLAG_LLD_GITEM_DELETE))
			{
				trx_vector_uint64_append(&del_gitemids, gitem->gitemid);
				continue;
			}

			if (0 == (gitem->flags & TRX_FLAG_LLD_GITEM_DISCOVERED))
				continue;

			if (0 == gitem->gitemid)
				new_gitems++;
			else if (0 != (gitem->flags & TRX_FLAG_LLD_GITEM_UPDATE))
				trx_vector_ptr_append(&upd_gitems, gitem);
		}
	}

	if (0 == new_graphs && 0 == new_gitems && 0 == upd_graphs && 0 == upd_gitems.values_num &&
			0 == del_gitemids.values_num)
	{
		goto out;
	}

	DBbegin();

	if (SUCCEED != (ret = DBlock_hostid(hostid)))
	{
		/* the host was removed while processing lld rule */
		DBrollback();
		goto out;
	}

	if (0 != new_graphs)
	{
		graphid = DBget_maxid_num("graphs", new_graphs);

		trx_db_insert_prepare(&db_insert, "graphs", "graphid", "name", "width", "height", "yaxismin",
				"yaxismax", "show_work_period", "show_triggers", "graphtype", "show_legend", "show_3d",
				"percent_left", "percent_right", "ymin_type", "ymin_itemid", "ymax_type",
				"ymax_itemid", "flags", NULL);

		trx_db_insert_prepare(&db_insert_gdiscovery, "graph_discovery", "graphid", "parent_graphid", NULL);
	}

	if (0 != new_gitems)
	{
		gitemid = DBget_maxid_num("graphs_items", new_gitems);

		trx_db_insert_prepare(&db_insert_gitems, "graphs_items", "gitemid", "graphid", "itemid", "drawtype",
				"sortorder", "color", "yaxisside", "calc_fnc", "type", NULL);
	}

	if (0 != upd_graphs || 0 != upd_gitems.values_num || 0 != del_gitemids.values_num)
	{
		sql = (char *)trx_malloc(sql, sql_alloc);
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	for (i = 0; i < graphs->values_num; i++)
	{
		graph = (trx_lld_graph_t *)graphs->values[i];

		if (0 == (graph->flags & TRX_FLAG_LLD_GRAPH_DISCOVERED))
			continue;

		if (0 == graph->graphid)
		{
			trx_db_insert_add_values(&db_insert, graphid, graph->name, width, height, yaxismin, yaxismax,
					(int)show_work_period, (int)show_triggers, (int)graphtype, (int)show_legend,
					(int)show_3d, percent_left, percent_right, (int)ymin_type, graph->ymin_itemid,
					(int)ymax_type, graph->ymax_itemid, (int)TRX_FLAG_DISCOVERY_CREATED);

			trx_db_insert_add_values(&db_insert_gdiscovery, graphid, parent_graphid);

			graph->graphid = graphid++;
		}
		else if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE))
		{
			const char	*d = "";

			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update graphs set ");

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_NAME))
			{
				name_esc = DBdyn_escape_string(graph->name);
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "name='%s'", name_esc);
				trx_free(name_esc);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_WIDTH))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%swidth=%d", d, width);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_HEIGHT))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sheight=%d", d, height);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_YAXISMIN))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%syaxismin=" TRX_FS_DBL, d,
						yaxismin);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_YAXISMAX))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%syaxismax=" TRX_FS_DBL, d,
						yaxismax);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_WORK_PERIOD))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sshow_work_period=%d", d,
						(int)show_work_period);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_TRIGGERS))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sshow_triggers=%d", d,
						(int)show_triggers);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_GRAPHTYPE))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sgraphtype=%d", d,
						(int)graphtype);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_LEGEND))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sshow_legend=%d", d,
						(int)show_legend);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_SHOW_3D))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sshow_3d=%d", d, (int)show_3d);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_PERCENT_LEFT))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%spercent_left=" TRX_FS_DBL, d,
						percent_left);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_PERCENT_RIGHT))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%spercent_right=" TRX_FS_DBL, d,
						percent_right);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_YMIN_TYPE))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%symin_type=%d", d,
						(int)ymin_type);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_YMIN_ITEMID))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%symin_itemid=%s", d,
						DBsql_id_ins(graph->ymin_itemid));
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_YMAX_TYPE))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%symax_type=%d", d,
						(int)ymax_type);
				d = ",";
			}

			if (0 != (graph->flags & TRX_FLAG_LLD_GRAPH_UPDATE_YMAX_ITEMID))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%symax_itemid=%s", d,
						DBsql_id_ins(graph->ymax_itemid));
			}

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where graphid=" TRX_FS_UI64 ";\n",
					graph->graphid);
		}

		for (j = 0; j < graph->gitems.values_num; j++)
		{
			gitem = (trx_lld_gitem_t *)graph->gitems.values[j];

			if (0 != (gitem->flags & TRX_FLAG_LLD_GITEM_DELETE))
				continue;

			if (0 == (gitem->flags & TRX_FLAG_LLD_GITEM_DISCOVERED))
				continue;

			if (0 == gitem->gitemid)
			{
				trx_db_insert_add_values(&db_insert_gitems, gitemid, graph->graphid, gitem->itemid,
						(int)gitem->drawtype, gitem->sortorder, gitem->color,
						(int)gitem->yaxisside, (int)gitem->calc_fnc, (int)gitem->type);

				gitem->gitemid = gitemid++;
			}
		}
	}

	for (i = 0; i < upd_gitems.values_num; i++)
	{
		const char	*d = "";

		gitem = (trx_lld_gitem_t *)upd_gitems.values[i];

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update graphs_items set ");

		if (0 != (gitem->flags & TRX_FLAG_LLD_GITEM_UPDATE_ITEMID))
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "itemid=" TRX_FS_UI64, gitem->itemid);
			d = ",";
		}

		if (0 != (gitem->flags & TRX_FLAG_LLD_GITEM_UPDATE_DRAWTYPE))
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sdrawtype=%d", d, (int)gitem->drawtype);
			d = ",";
		}

		if (0 != (gitem->flags & TRX_FLAG_LLD_GITEM_UPDATE_SORTORDER))
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%ssortorder=%d", d, gitem->sortorder);
			d = ",";
		}

		if (0 != (gitem->flags & TRX_FLAG_LLD_GITEM_UPDATE_COLOR))
		{
			color_esc = DBdyn_escape_string(gitem->color);
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%scolor='%s'", d, color_esc);
			trx_free(color_esc);
			d = ",";
		}

		if (0 != (gitem->flags & TRX_FLAG_LLD_GITEM_UPDATE_YAXISSIDE))
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%syaxisside=%d", d,
					(int)gitem->yaxisside);
			d = ",";
		}

		if (0 != (gitem->flags & TRX_FLAG_LLD_GITEM_UPDATE_CALC_FNC))
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%scalc_fnc=%d", d, (int)gitem->calc_fnc);
			d = ",";
		}

		if (0 != (gitem->flags & TRX_FLAG_LLD_GITEM_UPDATE_TYPE))
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%stype=%d", d, (int)gitem->type);

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where gitemid=" TRX_FS_UI64 ";\n",
				gitem->gitemid);
	}

	if (0 != del_gitemids.values_num)
	{
		trx_vector_uint64_sort(&del_gitemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from graphs_items where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "gitemid",
				del_gitemids.values, del_gitemids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	if (0 != upd_graphs || 0 != upd_gitems.values_num || 0 != del_gitemids.values_num)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);
		DBexecute("%s", sql);
		trx_free(sql);
	}

	if (0 != new_graphs)
	{
		trx_db_insert_execute(&db_insert);
		trx_db_insert_clean(&db_insert);

		trx_db_insert_execute(&db_insert_gdiscovery);
		trx_db_insert_clean(&db_insert_gdiscovery);
	}

	if (0 != new_gitems)
	{
		trx_db_insert_execute(&db_insert_gitems);
		trx_db_insert_clean(&db_insert_gitems);
	}

	DBcommit();
out:
	trx_vector_uint64_destroy(&del_gitemids);
	trx_vector_ptr_destroy(&upd_gitems);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_update_graphs                                                *
 *                                                                            *
 * Purpose: add or update graphs for discovery item                           *
 *                                                                            *
 * Parameters: hostid  - [IN] host identificator from database                *
 *             agent   - [IN] discovery item identificator from database      *
 *             jp_data - [IN] received data                                   *
 *                                                                            *
 * Return value: SUCCEED - if graphs were successfully added/updated or       *
 *                         adding/updating was not necessary                  *
 *               FAIL    - graphs cannot be added/updated                     *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
int	lld_update_graphs(trx_uint64_t hostid, trx_uint64_t lld_ruleid, const trx_vector_ptr_t *lld_rows,
		const trx_vector_ptr_t *lld_macro_paths, char **error)
{
	int			ret = SUCCEED;
	DB_RESULT		result;
	DB_ROW			row;
	trx_vector_ptr_t	graphs;
	trx_vector_ptr_t	gitems_proto;
	trx_vector_ptr_t	items;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&graphs);		/* list of graphs which were created or will be created or */
						/* updated by the graph prototype */
	trx_vector_ptr_create(&gitems_proto);	/* list of graphs_items which are used by the graph prototype */
	trx_vector_ptr_create(&items);		/* list of items which are related to the graph prototype */

	result = DBselect(
			"select distinct g.graphid,g.name,g.width,g.height,g.yaxismin,g.yaxismax,g.show_work_period,"
				"g.show_triggers,g.graphtype,g.show_legend,g.show_3d,g.percent_left,g.percent_right,"
				"g.ymin_type,g.ymin_itemid,g.ymax_type,g.ymax_itemid"
			" from graphs g,graphs_items gi,items i,item_discovery id"
			" where g.graphid=gi.graphid"
				" and gi.itemid=i.itemid"
				" and i.itemid=id.itemid"
				" and id.parent_itemid=" TRX_FS_UI64,
			lld_ruleid);

	while (SUCCEED == ret && NULL != (row = DBfetch(result)))
	{
		trx_uint64_t	parent_graphid, ymin_itemid_proto, ymax_itemid_proto;
		const char	*name_proto;
		int		width, height;
		double		yaxismin, yaxismax, percent_left, percent_right;
		unsigned char	show_work_period, show_triggers, graphtype, show_legend, show_3d,
				ymin_type, ymax_type;

		TRX_STR2UINT64(parent_graphid, row[0]);
		name_proto = row[1];
		width = atoi(row[2]);
		height = atoi(row[3]);
		yaxismin = atof(row[4]);
		yaxismax = atof(row[5]);
		TRX_STR2UCHAR(show_work_period, row[6]);
		TRX_STR2UCHAR(show_triggers, row[7]);
		TRX_STR2UCHAR(graphtype, row[8]);
		TRX_STR2UCHAR(show_legend, row[9]);
		TRX_STR2UCHAR(show_3d, row[10]);
		percent_left = atof(row[11]);
		percent_right = atof(row[12]);
		TRX_STR2UCHAR(ymin_type, row[13]);
		TRX_DBROW2UINT64(ymin_itemid_proto, row[14]);
		TRX_STR2UCHAR(ymax_type, row[15]);
		TRX_DBROW2UINT64(ymax_itemid_proto, row[16]);

		lld_graphs_get(parent_graphid, &graphs, width, height, yaxismin, yaxismax, show_work_period,
				show_triggers, graphtype, show_legend, show_3d, percent_left, percent_right,
				ymin_type, ymax_type);
		lld_gitems_get(parent_graphid, &gitems_proto, &graphs);
		lld_items_get(&gitems_proto, ymin_itemid_proto, ymax_itemid_proto, &items);

		/* making graphs */

		lld_graphs_make(&gitems_proto, &graphs, &items, name_proto, ymin_itemid_proto, ymax_itemid_proto,
				lld_rows, lld_macro_paths);
		lld_graphs_validate(hostid, &graphs, error);
		ret = lld_graphs_save(hostid, parent_graphid, &graphs, width, height, yaxismin, yaxismax,
				show_work_period, show_triggers, graphtype, show_legend, show_3d, percent_left,
				percent_right, ymin_type, ymax_type);

		lld_items_free(&items);
		lld_gitems_free(&gitems_proto);
		lld_graphs_free(&graphs);
	}
	DBfree_result(result);

	trx_vector_ptr_destroy(&items);
	trx_vector_ptr_destroy(&gitems_proto);
	trx_vector_ptr_destroy(&graphs);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}
