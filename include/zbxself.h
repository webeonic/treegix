
#ifndef TREEGIX_TRXSELF_H
#define TREEGIX_TRXSELF_H

#define TRX_PROCESS_STATE_IDLE		0
#define TRX_PROCESS_STATE_BUSY		1
#define TRX_PROCESS_STATE_COUNT		2	/* number of process states */

#define TRX_AGGR_FUNC_ONE		0
#define TRX_AGGR_FUNC_AVG		1
#define TRX_AGGR_FUNC_MAX		2
#define TRX_AGGR_FUNC_MIN		3

#define TRX_SELFMON_DELAY		1

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
trx_process_info_t;

int	get_process_type_forks(unsigned char process_type);

#ifndef _WINDOWS
int	init_selfmon_collector(char **error);
void	free_selfmon_collector(void);
void	update_selfmon_counter(unsigned char state);
void	collect_selfmon_stats(void);
void	get_selfmon_stats(unsigned char process_type, unsigned char aggr_func, int process_num,
		unsigned char state, double *value);
int	trx_get_all_process_stats(trx_process_info_t *stats);
void	trx_sleep_loop(int sleeptime);
void	trx_sleep_forever(void);
void	trx_wakeup(void);
int	trx_sleep_get_remainder(void);
#endif

#endif	/* TREEGIX_TRXSELF_H */
