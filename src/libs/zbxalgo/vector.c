

#include "common.h"
#include "log.h"

#include "trxalgo.h"
#include "vectorimpl.h"

TRX_VECTOR_IMPL(uint64, trx_uint64_t)
TRX_PTR_VECTOR_IMPL(str, char *)
TRX_PTR_VECTOR_IMPL(ptr, void *)
TRX_VECTOR_IMPL(ptr_pair, trx_ptr_pair_t)
TRX_VECTOR_IMPL(uint64_pair, trx_uint64_pair_t)

void	trx_ptr_free(void *data)
{
	trx_free(data);
}

void	trx_str_free(char *data)
{
	trx_free(data);
}
