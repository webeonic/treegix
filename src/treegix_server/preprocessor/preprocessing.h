

#ifndef TREEGIX_PREPROCESSING_H
#define TREEGIX_PREPROCESSING_H

#include "common.h"
#include "module.h"
#include "dbcache.h"
#include "preproc.h"
#include "trxalgo.h"

#define TRX_IPC_SERVICE_PREPROCESSING	"preprocessing"

#define TRX_IPC_PREPROCESSOR_WORKER		1
#define TRX_IPC_PREPROCESSOR_REQUEST		2
#define TRX_IPC_PREPROCESSOR_RESULT		3
#define TRX_IPC_PREPROCESSOR_QUEUE		4
#define TRX_IPC_PREPROCESSOR_TEST_REQUEST	5
#define TRX_IPC_PREPROCESSOR_TEST_RESULT	6

/* item value data used in preprocessing manager */
typedef struct
{
	trx_uint64_t	itemid;		 /* item id */
	unsigned char	item_value_type; /* item value type */
	AGENT_RESULT	*result;	 /* item value (if any) */
	trx_timespec_t	*ts;		 /* timestamp of a value */
	char		*error;		 /* error message (if any) */
	unsigned char	item_flags;	 /* item flags */
	unsigned char	state;		 /* item state */
}
trx_preproc_item_value_t;

trx_uint32_t	trx_preprocessor_pack_task(unsigned char **data, trx_uint64_t itemid, unsigned char value_type,
		trx_timespec_t *ts, trx_variant_t *value, const trx_vector_ptr_t *history,
		const trx_preproc_op_t *steps, int steps_num);
trx_uint32_t	trx_preprocessor_pack_result(unsigned char **data, trx_variant_t *value,
		const trx_vector_ptr_t *history, char *error);

trx_uint32_t	trx_preprocessor_unpack_value(trx_preproc_item_value_t *value, unsigned char *data);
void	trx_preprocessor_unpack_task(trx_uint64_t *itemid, unsigned char *value_type, trx_timespec_t **ts,
		trx_variant_t *value, trx_vector_ptr_t *history, trx_preproc_op_t **steps,
		int *steps_num, const unsigned char *data);
void	trx_preprocessor_unpack_result(trx_variant_t *value, trx_vector_ptr_t *history, char **error,
		const unsigned char *data);

void	trx_preprocessor_unpack_test_request(unsigned char *value_type, char **value, trx_timespec_t *ts,
		trx_vector_ptr_t *history, trx_preproc_op_t **steps, int *steps_num, const unsigned char *data);

trx_uint32_t	trx_preprocessor_pack_test_result(unsigned char **data, const trx_preproc_result_t *results,
		int results_num, const trx_vector_ptr_t *history, const char *error);

void	trx_preprocessor_unpack_test_result(trx_vector_ptr_t *results, trx_vector_ptr_t *history,
		char **error, const unsigned char *data);

#endif /* TREEGIX_PREPROCESSING_H */
