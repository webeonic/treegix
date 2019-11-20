

#ifndef TREEGIX_TRXHISTORY_H
#define TREEGIX_TRXHISTORY_H

/* the item history value */
typedef struct
{
	trx_timespec_t	timestamp;
	history_value_t	value;
}
trx_history_record_t;

TRX_VECTOR_DECL(history_record, trx_history_record_t)

void	trx_history_record_vector_clean(trx_vector_history_record_t *vector, int value_type);
void	trx_history_record_vector_destroy(trx_vector_history_record_t *vector, int value_type);
void	trx_history_record_clear(trx_history_record_t *value, int value_type);

int	trx_history_record_compare_asc_func(const trx_history_record_t *d1, const trx_history_record_t *d2);
int	trx_history_record_compare_desc_func(const trx_history_record_t *d1, const trx_history_record_t *d2);

void	trx_history_value2str(char *buffer, size_t size, const history_value_t *value, int value_type);

/* In most cases trx_history_record_vector_destroy() function should be used to free the  */
/* value vector filled by trx_vc_get_value* functions. This define simply better          */
/* mirrors the vector creation function to vector destroying function.                    */
#define trx_history_record_vector_create(vector)	trx_vector_history_record_create(vector)


int	trx_history_init(char **error);
void	trx_history_destroy(void);

int	trx_history_add_values(const trx_vector_ptr_t *values);
int	trx_history_get_values(trx_uint64_t itemid, int value_type, int start, int count, int end,
		trx_vector_history_record_t *values);

int	trx_history_requires_trends(int value_type);


#endif
