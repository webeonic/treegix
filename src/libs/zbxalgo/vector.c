

#include "common.h"
#include "log.h"

#include "zbxalgo.h"
#include "vectorimpl.h"

ZBX_VECTOR_IMPL(uint64, zbx_uint64_t)
ZBX_PTR_VECTOR_IMPL(str, char *)
ZBX_PTR_VECTOR_IMPL(ptr, void *)
ZBX_VECTOR_IMPL(ptr_pair, zbx_ptr_pair_t)
ZBX_VECTOR_IMPL(uint64_pair, zbx_uint64_pair_t)

void	zbx_ptr_free(void *data)
{
	zbx_free(data);
}

void	zbx_str_free(char *data)
{
	zbx_free(data);
}
