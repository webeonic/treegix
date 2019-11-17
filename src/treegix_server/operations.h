

#ifndef TREEGIX_OPERATIONS_H
#define TREEGIX_OPERATIONS_H

#include "common.h"
#include "db.h"
#include "zbxalgo.h"

extern int	CONFIG_TIMEOUT;

void	op_template_add(const DB_EVENT *event, zbx_vector_uint64_t *lnk_templateids);
void	op_template_del(const DB_EVENT *event, zbx_vector_uint64_t *del_templateids);
void	op_groups_add(const DB_EVENT *event, zbx_vector_uint64_t *groupids);
void	op_groups_del(const DB_EVENT *event, zbx_vector_uint64_t *groupids);
void	op_host_add(const DB_EVENT *event);
void	op_host_del(const DB_EVENT *event);
void	op_host_enable(const DB_EVENT *event);
void	op_host_disable(const DB_EVENT *event);
void	op_host_inventory_mode(const DB_EVENT *event, int inventory_mode);

#endif
