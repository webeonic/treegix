

#ifndef __zbxprometheus_h__
#define __zbxprometheus_h__

int	zbx_prometheus_pattern(const char *data, const char *filter_data, const char *output,
						char **value, char **err);
int	zbx_prometheus_to_json(const char *data, const char *filter_data, char **value, char **err);

int	zbx_prometheus_validate_filter(const char *pattern, char **error);
int	zbx_prometheus_validate_label(const char *label);

#endif /* __zbxprometheus_h__ */
