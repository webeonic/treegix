

#ifndef TREEGIX_ACTIONS_H
#define TREEGIX_ACTIONS_H

#include "common.h"
#include "db.h"

#define TRX_ACTION_RECOVERY_NONE	0
#define TRX_ACTION_RECOVERY_OPERATIONS	1

typedef struct
{
	trx_uint64_t	eventid;
	trx_uint64_t	acknowledgeid;
	trx_uint64_t	taskid;
}
trx_ack_task_t;

typedef struct
{
	trx_uint64_t	taskid;
	trx_uint64_t	actionid;
	trx_uint64_t	eventid;
	trx_uint64_t	triggerid;
	trx_uint64_t	acknowledgeid;
}
trx_ack_escalation_t;

int	check_action_condition(const DB_EVENT *event, DB_CONDITION *condition);
void	process_actions(const trx_vector_ptr_t *events, const trx_vector_uint64_pair_t *closed_events);
int	process_actions_by_acknowledgements(const trx_vector_ptr_t *ack_tasks);
void	get_db_actions_info(trx_vector_uint64_t *actionids, trx_vector_ptr_t *actions);
void	free_db_action(DB_ACTION *action);

#endif
