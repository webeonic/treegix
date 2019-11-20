

#include "common.h"

#ifdef HAVE_OPENIPMI

#include "log.h"
#include "trxserialize.h"

#include "ipmi_protocol.h"

trx_uint32_t	trx_ipmi_serialize_request(unsigned char **data, trx_uint64_t objectid, const char *addr,
		unsigned short port, signed char authtype, unsigned char privilege, const char *username,
		const char *password, const char *sensor, int command)
{
	unsigned char	*ptr;
	trx_uint32_t	data_len, addr_len, username_len, password_len, sensor_len;

	addr_len = strlen(addr) + 1;
	username_len = strlen(username) + 1;
	password_len = strlen(password) + 1;
	sensor_len = strlen(sensor) + 1;

	data_len = sizeof(trx_uint64_t) + sizeof(short) + sizeof(char) * 2 + addr_len + username_len + password_len +
			sensor_len + sizeof(trx_uint32_t) * 4 + sizeof(int);

	*data = (unsigned char *)trx_malloc(NULL, data_len);
	ptr = *data;
	ptr += trx_serialize_uint64(ptr, objectid);
	ptr += trx_serialize_str(ptr, addr, addr_len);
	ptr += trx_serialize_short(ptr, port);
	ptr += trx_serialize_char(ptr, authtype);
	ptr += trx_serialize_char(ptr, privilege);
	ptr += trx_serialize_str(ptr, username, username_len);
	ptr += trx_serialize_str(ptr, password, password_len);
	ptr += trx_serialize_str(ptr, sensor, sensor_len);
	(void)trx_serialize_int(ptr, command);

	return data_len;
}

void	trx_ipmi_deserialize_request(const unsigned char *data, trx_uint64_t *objectid, char **addr,
		unsigned short *port, signed char *authtype, unsigned char *privilege, char **username, char **password,
		char **sensor, int *command)
{
	trx_uint32_t	value_len;

	data += trx_deserialize_uint64(data, objectid);
	data += trx_deserialize_str(data, addr, value_len);
	data += trx_deserialize_short(data, port);
	data += trx_deserialize_char(data, authtype);
	data += trx_deserialize_char(data, privilege);
	data += trx_deserialize_str(data, username, value_len);
	data += trx_deserialize_str(data, password, value_len);
	data += trx_deserialize_str(data, sensor, value_len);
	(void)trx_deserialize_int(data, command);
}

void	trx_ipmi_deserialize_request_objectid(const unsigned char *data, trx_uint64_t *objectid)
{
	(void)trx_deserialize_uint64(data, objectid);
}

trx_uint32_t	trx_ipmi_serialize_result(unsigned char **data, const trx_timespec_t *ts, int errcode,
		const char *value)
{
	unsigned char	*ptr;
	trx_uint32_t	data_len, value_len;

	value_len = (NULL != value ? strlen(value)  + 1 : 0);

	data_len = value_len + sizeof(trx_uint32_t) + sizeof(int) * 3;
	*data = (unsigned char *)trx_malloc(NULL, data_len);

	ptr = *data;
	ptr += trx_serialize_int(ptr, ts->sec);
	ptr += trx_serialize_int(ptr, ts->ns);
	ptr += trx_serialize_int(ptr, errcode);
	(void)trx_serialize_str(ptr, value, value_len);

	return data_len;
}

void	trx_ipmi_deserialize_result(const unsigned char *data, trx_timespec_t *ts, int *errcode, char **value)
{
	int	value_len;

	data += trx_deserialize_int(data, &ts->sec);
	data += trx_deserialize_int(data, &ts->ns);
	data += trx_deserialize_int(data, errcode);
	(void)trx_deserialize_str(data, value, value_len);
}

#endif
