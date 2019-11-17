
#ifndef TREEGIX_ZBXSELF_H
#define TREEGIX_ZBXSELF_H

#define ZBX_PROCESS_STATE_IDLE		0
#define ZBX_PROCESS_STATE_BUSY		1
#define ZBX_PROCESS_STATE_COUNT		2	/* number of process states */

#define ZBX_AGGR_FUNC_ONE		0
#define ZBX_AGGR_FUNC_AVG		1
#define ZBX_AGGR_FUNC_MAX		2
#define ZBX_AGGR_FUNC_MIN		3

#define ZBX_SELFMON_DELAY		1

/* the process statistics */
typedef struct
{
	double	busy_max;
	double	busy_min;
	double	busy_avg;
	double	idle_max;
	double	idle_min;
	double	idle_avg;
	int	count;
}
zbx_process_info_t;

int	get_process_type_forks(unsigned char process_type);

#ifndef _WINDOWS
int	init_selfmon_collector(char **error);
void	free_selfmon_collector(void);
void	update_selfmon_counter(unsigned char state);
void	collect_selfmon_stats(void);
void	get_selfmon_stats(unsigned char process_type, unsigned char aggr_func, int process_num,
		unsigned char state, double *value);
int	zbx_get_all_process_stats(zbx_process_info_t *stats);
void	zbx_sleep_loop(int sleeptime);
void	zbx_sleep_forever(void);
void	zbx_wakeup(void);
int	zbx_sleep_get_remainder(void);
#endif

#endif	/* TREEGIX_ZBXSELF_H */
