

#ifndef TREEGIX_HISTORY_H
#define TREEGIX_HISTORY_H

#define TRX_HISTORY_IFACE_SQL		0
#define TRX_HISTORY_IFACE_ELASTIC	1

typedef struct trx_history_iface trx_history_iface_t;

typedef void (*trx_history_destroy_func_t)(struct trx_history_iface *hist);
typedef int (*trx_history_add_values_func_t)(struct trx_history_iface *hist, const trx_vector_ptr_t *history);
typedef int (*trx_history_get_values_func_t)(struct trx_history_iface *hist, trx_uint64_t itemid, int start,
		int count, int end, trx_vector_history_record_t *values);
typedef int (*trx_history_flush_func_t)(struct trx_history_iface *hist);

struct trx_history_iface
{
	unsigned char			value_type;
	unsigned char			requires_trends;
	void				*data;

	trx_history_destroy_func_t	destroy;
	trx_history_add_values_func_t	add_values;
	trx_history_get_values_func_t	get_values;
	trx_history_flush_func_t	flush;
};

/* SQL hist */
int	trx_history_sql_init(trx_history_iface_t *hist, unsigned char value_type, char **error);

/* elastic hist */
int	trx_history_elastic_init(trx_history_iface_t *hist, unsigned char value_type, char **error);

#endif
