

#ifndef TREEGIX_TREEGIX_STATS_H_
#define TREEGIX_TREEGIX_STATS_H_

extern int	CONFIG_SERVER_STARTUP_TIME;

void	zbx_get_treegix_stats(struct zbx_json *json);
void	zbx_get_treegix_stats_ext(struct zbx_json *json);

#endif /* TREEGIX_TREEGIX_STATS_H_ */
