

#ifndef TREEGIX_EVALFUNC_H
#define TREEGIX_EVALFUNC_H

int	evaluate_macro_function(char **result, const char *host, const char *key, const char *function,
		const char *parameter);
int	evaluatable_for_notsupported(const char *fn);

#endif
