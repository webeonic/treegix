

#ifndef TREEGIX_SIGHANDLER_H
#define TREEGIX_SIGHANDLER_H

void	trx_set_common_signal_handlers(void);
void	trx_set_child_signal_handler(void);
void 	trx_set_metric_thread_signal_handler(void);

#endif
