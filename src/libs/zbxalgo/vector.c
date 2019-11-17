

#include "common.h"
#include "log.h"

#include "zbxalgo.h"
#include "vectorimpl.h"

TRX_VECTOR_IMPL(uint64, zbx_uint64_t)
TRX_PTR_VECTOR_IMPL(str, char *)
TRX_PTR_VECTOR_IMPL(ptr, void *)
TRX_VECTOR_IMPL(ptr_pair, zbx_ptr_pair_t)
TRX_VECTOR_IMPL(uint64_pair, zbx_uint64_pair_t)

void	zbx_ptr_free(void *data)
{
	zbx_free(data);
}

void	zbx_str_free(char *data)
{
	zbx_free(data);
}
