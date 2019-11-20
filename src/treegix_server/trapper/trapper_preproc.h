

#ifndef TREEGIX_TRAPPER_PREPROC_H
#define TREEGIX_TRAPPER_PREPROC_H

#include "comms.h"
#include "trxjson.h"

int	trx_trapper_preproc_test(trx_socket_t *sock, const struct trx_json_parse *jp);
int	trx_trapper_preproc_test_run(const struct trx_json_parse *jp, struct trx_json *json, char **error);

#endif
