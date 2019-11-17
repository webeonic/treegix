

#ifndef TREEGIX_TRAPPER_PREPROC_H
#define TREEGIX_TRAPPER_PREPROC_H

#include "comms.h"
#include "zbxjson.h"

int	zbx_trapper_preproc_test(zbx_socket_t *sock, const struct zbx_json_parse *jp);
int	zbx_trapper_preproc_test_run(const struct zbx_json_parse *jp, struct zbx_json *json, char **error);

#endif
