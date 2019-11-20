
#ifndef TREEGIX_SERIALIZE_H
#define TREEGIX_SERIALIZE_H

#include "common.h"

#define trx_serialize_prepare_str(len, str)			\
	str##_len = (NULL != str ? strlen(str) + 1 : 0);	\
	len += str##_len + sizeof(trx_uint32_t)

#define trx_serialize_prepare_str_len(len, str, str_len)	\
	str_len = (NULL != str ? strlen(str) + 1 : 0);		\
	len += str_len + sizeof(trx_uint32_t)

#define trx_serialize_prepare_value(len, value)			\
	len += sizeof(value)

#define trx_serialize_uint64(buffer, value) (memcpy(buffer, &value, sizeof(trx_uint64_t)), sizeof(trx_uint64_t))

#define trx_serialize_int(buffer, value) (memcpy(buffer, (int *)&value, sizeof(int)), sizeof(int))

#define trx_serialize_short(buffer, value) (memcpy(buffer, (short *)&value, sizeof(short)), sizeof(short))

#define trx_serialize_double(buffer, value) (memcpy(buffer, (double *)&value, sizeof(double)), sizeof(double))

#define trx_serialize_char(buffer, value) (*buffer = (char)value, sizeof(char))

#define trx_serialize_str_null(buffer)				\
	(							\
		memset(buffer, 0, sizeof(trx_uint32_t)),	\
		sizeof(trx_uint32_t)				\
	)

#define trx_serialize_str(buffer, value, len)						\
	(										\
		0 == len ? trx_serialize_str_null(buffer) :				\
		(									\
			memcpy(buffer, (trx_uint32_t *)&len, sizeof(trx_uint32_t)),	\
			memcpy(buffer + sizeof(trx_uint32_t), value, len),		\
			len + sizeof(trx_uint32_t)					\
		)									\
	)

#define trx_serialize_value(buffer, value) (memcpy(buffer, &value, sizeof(value)), sizeof(value))

/* deserialization of primitive types */

#define trx_deserialize_uint64(buffer, value) \
	(memcpy(value, buffer, sizeof(trx_uint64_t)), sizeof(trx_uint64_t))

#define trx_deserialize_int(buffer, value) \
	(memcpy(value, buffer, sizeof(int)), sizeof(int))

#define trx_deserialize_short(buffer, value) \
	(memcpy(value, buffer, sizeof(short)), sizeof(short))

#define trx_deserialize_char(buffer, value) \
	(*value = *buffer, sizeof(char))

#define trx_deserialize_double(buffer, value) \
	(memcpy(value, buffer, sizeof(double)), sizeof(double))

#define trx_deserialize_str(buffer, value, value_len)					\
	(										\
			memcpy(&value_len, buffer, sizeof(trx_uint32_t)),		\
			0 < value_len ? (						\
			*value = (char *)trx_malloc(NULL, value_len + 1),			\
			memcpy(*(value), buffer + sizeof(trx_uint32_t), value_len),	\
			(*value)[value_len] = '\0'					\
			) : (*value = NULL, 0),						\
		value_len + sizeof(trx_uint32_t)					\
	)

#define trx_deserialize_str_s(buffer, value, value_len)				\
	(									\
		memcpy(&value_len, buffer, sizeof(trx_uint32_t)),		\
		memcpy(value, buffer + sizeof(trx_uint32_t), value_len),	\
		value[value_len] = '\0',					\
		value_len + sizeof(trx_uint32_t)				\
	)

#define trx_deserialize_str_ptr(buffer, value, value_len)				\
	(										\
		memcpy(&value_len, buffer, sizeof(trx_uint32_t)),			\
		0 < value_len ? (value = (char *)(buffer + sizeof(trx_uint32_t))) :	\
		(value = NULL), value_len + sizeof(trx_uint32_t)			\
	)

#define trx_deserialize_value(buffer, value) \
	(memcpy(value, buffer, sizeof(*value)), sizeof(*value))

/* length prefixed binary data */
#define trx_deserialize_bin(buffer, value, value_len)					\
	(										\
		memcpy(&value_len, buffer, sizeof(trx_uint32_t)),			\
		*value = (void *)trx_malloc(NULL, value_len + sizeof(trx_uint32_t)),	\
		memcpy(*(value), buffer, value_len + sizeof(trx_uint32_t)),		\
		value_len + sizeof(trx_uint32_t)					\
	)
#endif

/* TREEGIX_SERIALIZE_H */
