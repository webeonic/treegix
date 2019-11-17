

#ifndef TREEGIX_BASE64_H
#define TREEGIX_BASE64_H

void	str_base64_encode(const char *p_str, char *p_b64str, int in_size);
void	str_base64_encode_dyn(const char *p_str, char **p_b64str, int in_size);
void	str_base64_decode(const char *p_b64str, char *p_str, int maxsize, int *p_out_size);

#endif /* TREEGIX_BASE64_H */
