

#ifndef TREEGIX_LLD_PROTOCOL_H
#define TREEGIX_LLD_PROTOCOL_H

#include "common.h"

#define TRX_IPC_SERVICE_LLD	"lld"

/* LLD -> manager */
#define TRX_IPC_LLD_REGISTER		1000
#define TRX_IPC_LLD_DONE		1001

/* manager -> LLD */
#define TRX_IPC_LLD_TASK		1100

/* manager -> LLD */
#define TRX_IPC_LLD_REQUEST		1200

/* poller -> LLD */
#define TRX_IPC_LLD_QUEUE		1300

zbx_uint32_t	zbx_lld_serialize_item_value(unsigned char **data, zbx_uint64_t itemid, const char *value,
		const zbx_timespec_t *ts, unsigned char meta, zbx_uint64_t lastlogsize, int mtime, const char *error);

void	zbx_lld_deserialize_item_value(const unsigned char *data, zbx_uint64_t *itemid, char **value,
		zbx_timespec_t *ts, unsigned char *meta, zbx_uint64_t *lastlogsize, int *mtime, char **error);

#endif
