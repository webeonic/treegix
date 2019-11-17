
#ifndef TREEGIX_COMPRESS_H
#define TREEGIX_COMPRESS_H

int	zbx_compress(const char *in, size_t size_in, char **out, size_t *size_out);
int	zbx_uncompress(const char *in, size_t size_in, char *out, size_t *size_out);
const char	*zbx_compress_strerror(void);

#endif
