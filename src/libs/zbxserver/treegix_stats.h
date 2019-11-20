

#ifndef TREEGIX_TREEGIX_STATS_H_
#define TREEGIX_TREEGIX_STATS_H_

extern int	CONFIG_SERVER_STARTUP_TIME;

void	trx_get_treegix_stats(struct trx_json *json);
void	trx_get_treegix_stats_ext(struct trx_json *json);

#endif /* TREEGIX_TREEGIX_STATS_H_ */
