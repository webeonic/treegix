

#ifndef __trxprometheus_h__
#define __trxprometheus_h__

int	trx_prometheus_pattern(const char *data, const char *filter_data, const char *output,
						char **value, char **err);
int	trx_prometheus_to_json(const char *data, const char *filter_data, char **value, char **err);

int	trx_prometheus_validate_filter(const char *pattern, char **error);
int	trx_prometheus_validate_label(const char *label);

#endif /* __trxprometheus_h__ */
