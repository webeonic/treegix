
#ifndef TREEGIX_TRXTASKS_H
#define TREEGIX_TRXTASKS_H

#include "trxalgo.h"
#include "trxjson.h"

#define TRX_TASK_UPDATE_FREQUENCY	1

#define TRX_REMOTE_COMMAND_TTL		(SEC_PER_MIN * 10)

/* task manager task types */
#define TRX_TM_TASK_UNDEFINED				0
#define TRX_TM_TASK_CLOSE_PROBLEM			1
#define TRX_TM_TASK_REMOTE_COMMAND			2
#define TRX_TM_TASK_REMOTE_COMMAND_RESULT		3
#define TRX_TM_TASK_ACKNOWLEDGE				4
#define TRX_TM_TASK_UPDATE_EVENTNAMES			5
#define TRX_TM_TASK_CHECK_NOW				6

/* task manager task states */
#define TRX_TM_STATUS_NEW			1
#define TRX_TM_STATUS_INPROGRESS		2
#define TRX_TM_STATUS_DONE			3
#define TRX_TM_STATUS_EXPIRED			4

/* the time period after which finished (done/expired) tasks are removed */
#define TRX_TM_CLEANUP_TASK_AGE			SEC_PER_DAY

typedef struct
{
	int		command_type;
	char		*command;
	int		execute_on;
	int		port;
	int		authtype;
	char		*username;
	char		*password;
	char		*publickey;
	char		*privatekey;
	trx_uint64_t	parent_taskid;
	trx_uint64_t	hostid;
	trx_uint64_t	alertid;
}
trx_tm_remote_command_t;

typedef struct
{
	int		status;
	char		*info;
	trx_uint64_t	parent_taskid;
}
trx_tm_remote_command_result_t;

typedef struct
{
	trx_uint64_t	itemid;
}
trx_tm_check_now_t;

typedef struct
{
	/* the task identifier */
	trx_uint64_t	taskid;
	/* the target proxy hostid or 0 if the task must be on server, ignored by proxy */
	trx_uint64_t	proxy_hostid;
	/* the task type (TRX_TM_TASK_* defines) */
	unsigned char	type;
	/* the task status (TRX_TM_STATUS_* defines) */
	unsigned char	status;
	/* the task creation time */
	int		clock;
	/* the task expiration period in seconds */
	int		ttl;

	/* the task data, depending on task type */
	void		*data;
}
trx_tm_task_t;


trx_tm_task_t	*trx_tm_task_create(trx_uint64_t taskid, unsigned char type, unsigned char status, int clock, int ttl,
		trx_uint64_t proxy_hostid);
void	trx_tm_task_clear(trx_tm_task_t *task);
void	trx_tm_task_free(trx_tm_task_t *task);

trx_tm_remote_command_t	*trx_tm_remote_command_create(int commandtype, const char *command, int execute_on, int port,
		int authtype, const char *username, const char *password, const char *publickey, const char *privatekey,
		trx_uint64_t parent_taskid, trx_uint64_t hostid, trx_uint64_t alertid);

trx_tm_remote_command_result_t	*trx_tm_remote_command_result_create(trx_uint64_t parent_taskid, int status,
		const char *error);

trx_tm_check_now_t	*trx_tm_check_now_create(trx_uint64_t itemid);

void	trx_tm_save_tasks(trx_vector_ptr_t *tasks);
int	trx_tm_save_task(trx_tm_task_t *task);

void	trx_tm_get_proxy_tasks(trx_vector_ptr_t *tasks, trx_uint64_t proxy_hostid);
void	trx_tm_update_task_status(trx_vector_ptr_t *tasks, int status);
void	trx_tm_json_serialize_tasks(struct trx_json *json, const trx_vector_ptr_t *tasks);
void	trx_tm_json_deserialize_tasks(const struct trx_json_parse *jp, trx_vector_ptr_t *tasks);

/* separate implementation for proxy and server */
void	trx_tm_get_remote_tasks(trx_vector_ptr_t *tasks, trx_uint64_t proxy_hostid);

#endif
