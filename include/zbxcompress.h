
#ifndef TREEGIX_COMPRESS_H
#define TREEGIX_COMPRESS_H

int	trx_compress(const char *in, size_t size_in, char **out, size_t *size_out);
int	trx_uncompress(const char *in, size_t size_in, char *out, size_t *size_out);
const char	*trx_compress_strerror(void);

#endif
