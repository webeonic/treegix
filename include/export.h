
#ifndef TREEGIX_EXPORT_H
#define TREEGIX_EXPORT_H

int	zbx_is_export_enabled(void);
int	zbx_export_init(char **error);

void	zbx_problems_export_init(const char *process_name, int process_num);
void	zbx_problems_export_write(const char *buf, size_t count);
void	zbx_problems_export_flush(void);

void	zbx_history_export_init(const char *process_name, int process_num);
void	zbx_history_export_write(const char *buf, size_t count);
void	zbx_history_export_flush(void);
void	zbx_trends_export_write(const char *buf, size_t count);
void	zbx_trends_export_flush(void);

#endif
