

#ifndef TREEGIX_PREPROCESSING_H
#define TREEGIX_PREPROCESSING_H

#include "common.h"
#include "module.h"
#include "dbcache.h"
#include "preproc.h"
#include "zbxalgo.h"

#define ZBX_IPC_SERVICE_PREPROCESSING	"preprocessing"

#define ZBX_IPC_PREPROCESSOR_WORKER		1
#define ZBX_IPC_PREPROCESSOR_REQUEST		2
#define ZBX_IPC_PREPROCESSOR_RESULT		3
#define ZBX_IPC_PREPROCESSOR_QUEUE		4
#define ZBX_IPC_PREPROCESSOR_TEST_REQUEST	5
#define ZBX_IPC_PREPROCESSOR_TEST_RESULT	6

/* item value data used in preprocessing manager */
typedef struct
{
	zbx_uint64_t	itemid;		 /* item id */
	unsigned char	item_value_type; /* item value type */
	AGENT_RESULT	*result;	 /* item value (if any) */
	zbx_timespec_t	*ts;		 /* timestamp of a value */
	char		*error;		 /* error message (if any) */
	unsigned char	item_flags;	 /* item flags */
	unsigned char	state;		 /* item state */
}
zbx_preproc_item_value_t;

zbx_uint32_t	zbx_preprocessor_pack_task(unsigned char **data, zbx_uint64_t itemid, unsigned char value_type,
		zbx_timespec_t *ts, zbx_variant_t *value, const zbx_vector_ptr_t *history,
		const zbx_preproc_op_t *steps, int steps_num);
zbx_uint32_t	zbx_preprocessor_pack_result(unsigned char **data, zbx_variant_t *value,
		const zbx_vector_ptr_t *history, char *error);

zbx_uint32_t	zbx_preprocessor_unpack_value(zbx_preproc_item_value_t *value, unsigned char *data);
void	zbx_preprocessor_unpack_task(zbx_uint64_t *itemid, unsigned char *value_type, zbx_timespec_t **ts,
		zbx_variant_t *value, zbx_vector_ptr_t *history, zbx_preproc_op_t **steps,
		int *steps_num, const unsigned char *data);
void	zbx_preprocessor_unpack_result(zbx_variant_t *value, zbx_vector_ptr_t *history, char **error,
		const unsigned char *data);

void	zbx_preprocessor_unpack_test_request(unsigned char *value_type, char **value, zbx_timespec_t *ts,
		zbx_vector_ptr_t *history, zbx_preproc_op_t **steps, int *steps_num, const unsigned char *data);

zbx_uint32_t	zbx_preprocessor_pack_test_result(unsigned char **data, const zbx_preproc_result_t *results,
		int results_num, const zbx_vector_ptr_t *history, const char *error);

void	zbx_preprocessor_unpack_test_result(zbx_vector_ptr_t *results, zbx_vector_ptr_t *history,
		char **error, const unsigned char *data);

#endif /* TREEGIX_PREPROCESSING_H */
