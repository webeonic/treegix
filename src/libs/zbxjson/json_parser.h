

#ifndef TREEGIX_JSON_PARSER_H
#define TREEGIX_JSON_PARSER_H

int	zbx_json_validate(const char *start, char **error);

int	json_parse_value(const char *start, char **error);

#endif
