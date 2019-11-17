

#ifndef TREEGIX_SIGHANDLER_H
#define TREEGIX_SIGHANDLER_H

void	zbx_set_common_signal_handlers(void);
void	zbx_set_child_signal_handler(void);
void 	zbx_set_metric_thread_signal_handler(void);

#endif
