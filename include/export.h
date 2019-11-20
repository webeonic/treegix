
#ifndef TREEGIX_EXPORT_H
#define TREEGIX_EXPORT_H

int	trx_is_export_enabled(void);
int	trx_export_init(char **error);

void	trx_problems_export_init(const char *process_name, int process_num);
void	trx_problems_export_write(const char *buf, size_t count);
void	trx_problems_export_flush(void);

void	trx_history_export_init(const char *process_name, int process_num);
void	trx_history_export_write(const char *buf, size_t count);
void	trx_history_export_flush(void);
void	trx_trends_export_write(const char *buf, size_t count);
void	trx_trends_export_flush(void);

#endif
