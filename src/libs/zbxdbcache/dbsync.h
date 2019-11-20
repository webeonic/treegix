

#ifndef TREEGIX_DBSYNC_H
#define TREEGIX_DBSYNC_H

#include "common.h"

/* no changes */
#define TRX_DBSYNC_ROW_NONE	0
/*  a new object must be added to configuration cache */
#define TRX_DBSYNC_ROW_ADD	1
/* a cached object must be updated in configuration cache */
#define TRX_DBSYNC_ROW_UPDATE	2
/* a cached object must be removed from configuration cache */
#define TRX_DBSYNC_ROW_REMOVE	3

#define TRX_DBSYNC_UPDATE_HOSTS			__UINT64_C(0x0001)
#define TRX_DBSYNC_UPDATE_ITEMS			__UINT64_C(0x0002)
#define TRX_DBSYNC_UPDATE_FUNCTIONS		__UINT64_C(0x0004)
#define TRX_DBSYNC_UPDATE_TRIGGERS		__UINT64_C(0x0008)
#define TRX_DBSYNC_UPDATE_TRIGGER_DEPENDENCY	__UINT64_C(0x0010)
#define TRX_DBSYNC_UPDATE_HOST_GROUPS		__UINT64_C(0x0020)
#define TRX_DBSYNC_UPDATE_MAINTENANCE_GROUPS	__UINT64_C(0x0040)


#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
#	define TRX_HOST_TLS_OFFSET	4
#else
#	define TRX_HOST_TLS_OFFSET	0
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_dbsync_preproc_row_func_t                                    *
 *                                                                            *
 * Purpose: applies necessary preprocessing before row is compared/used       *
 *                                                                            *
 * Parameter: row - [IN] the row to preprocess                                *
 *                                                                            *
 * Return value: the preprocessed row                                         *
 *                                                                            *
 * Comments: The row preprocessing can be used to expand user macros in       *
 *           some columns.                                                    *
 *                                                                            *
 *                                                                            *
 ******************************************************************************/
typedef char **(*trx_dbsync_preproc_row_func_t)(char **row);

typedef struct
{
	/* a row tag, describing the changes (see TRX_DBSYNC_ROW_* defines) */
	unsigned char	tag;

	/* the identifier of the object represented by the row */
	trx_uint64_t	rowid;

	/* the column values, NULL if the tag is TRX_DBSYNC_ROW_REMOVE */
	char		**row;
}
trx_dbsync_row_t;

struct trx_dbsync
{
	/* the synchronization mode (see TRX_DBSYNC_* defines) */
	unsigned char			mode;

	/* the number of columns in diff */
	int				columns_num;

	/* the current row */
	int				row_index;

	/* the changed rows */
	trx_vector_ptr_t		rows;

	/* the database result set for TRX_DBSYNC_ALL mode */
	DB_RESULT			dbresult;

	/* the row preprocessing function */
	trx_dbsync_preproc_row_func_t	preproc_row_func;

	/* the pre-processed row */
	char				**row;

	/* the preprocessed columns  */
	trx_vector_ptr_t		columns;

	/* statistics */
	trx_uint64_t	add_num;
	trx_uint64_t	update_num;
	trx_uint64_t	remove_num;
};

void	trx_dbsync_init_env(TRX_DC_CONFIG *cache);
void	trx_dbsync_free_env(void);

void	trx_dbsync_init(trx_dbsync_t *sync, unsigned char mode);
void	trx_dbsync_clear(trx_dbsync_t *sync);
int	trx_dbsync_next(trx_dbsync_t *sync, trx_uint64_t *rowid, char ***rows, unsigned char *tag);

int	trx_dbsync_compare_config(trx_dbsync_t *sync);
int	trx_dbsync_compare_autoreg_psk(trx_dbsync_t *sync);
int	trx_dbsync_compare_hosts(trx_dbsync_t *sync);
int	trx_dbsync_compare_host_inventory(trx_dbsync_t *sync);
int	trx_dbsync_compare_host_templates(trx_dbsync_t *sync);
int	trx_dbsync_compare_global_macros(trx_dbsync_t *sync);
int	trx_dbsync_compare_host_macros(trx_dbsync_t *sync);
int	trx_dbsync_compare_interfaces(trx_dbsync_t *sync);
int	trx_dbsync_compare_items(trx_dbsync_t *sync);
int	trx_dbsync_compare_template_items(trx_dbsync_t *sync);
int	trx_dbsync_compare_prototype_items(trx_dbsync_t *sync);
int	trx_dbsync_compare_triggers(trx_dbsync_t *sync);
int	trx_dbsync_compare_trigger_dependency(trx_dbsync_t *sync);
int	trx_dbsync_compare_functions(trx_dbsync_t *sync);
int	trx_dbsync_compare_expressions(trx_dbsync_t *sync);
int	trx_dbsync_compare_actions(trx_dbsync_t *sync);
int	trx_dbsync_compare_action_ops(trx_dbsync_t *sync);
int	trx_dbsync_compare_action_conditions(trx_dbsync_t *sync);
int	trx_dbsync_compare_trigger_tags(trx_dbsync_t *sync);
int	trx_dbsync_compare_host_tags(trx_dbsync_t *sync);
int	trx_dbsync_compare_correlations(trx_dbsync_t *sync);
int	trx_dbsync_compare_corr_conditions(trx_dbsync_t *sync);
int	trx_dbsync_compare_corr_operations(trx_dbsync_t *sync);
int	trx_dbsync_compare_host_groups(trx_dbsync_t *sync);
int	trx_dbsync_compare_item_preprocs(trx_dbsync_t *sync);
int	trx_dbsync_compare_maintenances(trx_dbsync_t *sync);
int	trx_dbsync_compare_maintenance_tags(trx_dbsync_t *sync);
int	trx_dbsync_compare_maintenance_periods(trx_dbsync_t *sync);
int	trx_dbsync_compare_maintenance_groups(trx_dbsync_t *sync);
int	trx_dbsync_compare_maintenance_hosts(trx_dbsync_t *sync);
int	trx_dbsync_compare_host_group_hosts(trx_dbsync_t *sync);

#endif /* BUILD_SRC_LIBS_TRXDBCACHE_DBSYNC_H_ */
