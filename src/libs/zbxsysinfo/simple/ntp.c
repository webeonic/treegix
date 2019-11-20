

#include "common.h"
#include "sysinfo.h"
#include "comms.h"
#include "log.h"
#include "ntp.h"

#define NTP_SCALE		4294967296.0	/* 2^32, of course! */

#define NTP_PACKET_SIZE		48		/* without authentication */
#define NTP_OFFSET_ORIGINATE	24		/* offset of originate timestamp */
#define NTP_OFFSET_TRANSMIT	40		/* offset of transmit timestamp */

#define NTP_VERSION		3		/* the current version */

#define NTP_MODE_CLIENT		3		/* NTP client request */
#define NTP_MODE_SERVER		4		/* NTP server response */

typedef struct
{
	unsigned char	version;
	unsigned char	mode;
	double		transmit;
}
ntp_data;

static void	make_packet(ntp_data *data)
{
	data->version = NTP_VERSION;
	data->mode = NTP_MODE_CLIENT;
	data->transmit = trx_current_time();
}

static void	pack_ntp(const ntp_data *data, unsigned char *request, int length)
{
	/* Pack the essential data into an NTP packet, bypassing struct layout  */
	/* and endian problems. Note that it ignores fields irrelevant to SNTP. */

	int	i, k;
	double	d;

	memset(request, 0, length);

	request[0] = (data->version << 3) | data->mode;

	d = data->transmit / NTP_SCALE;

	for (i = 0; i < 8; i++)
	{
		if ((k = (int)(d *= 256.0)) >= 256)
			k = 255;

		request[NTP_OFFSET_TRANSMIT + i] = k;

		d -= k;
	}
}

static int	unpack_ntp(ntp_data *data, const unsigned char *request, const unsigned char *response, int length)
{
	/* Unpack the essential data from an NTP packet, bypassing struct layout */
	/* and endian problems. Note that it ignores fields irrelevant to SNTP.  */

	int	i, ret = FAIL;
	double	d;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (NTP_PACKET_SIZE != length)
	{
		treegix_log(LOG_LEVEL_DEBUG, "invalid response size: %d", length);
		goto out;
	}

	if (0 != memcmp(response + NTP_OFFSET_ORIGINATE, request + NTP_OFFSET_TRANSMIT, 8))
	{
		treegix_log(LOG_LEVEL_DEBUG, "originate timestamp in the response does not match"
				" transmit timestamp in the request: 0x%04x%04x 0x%04x%04x",
				*(const unsigned int *)&response[NTP_OFFSET_ORIGINATE],
				*(const unsigned int *)&response[NTP_OFFSET_ORIGINATE + 4],
				*(const unsigned int *)&request[NTP_OFFSET_TRANSMIT],
				*(const unsigned int *)&request[NTP_OFFSET_TRANSMIT + 4]);
		goto out;
	}

	data->version = (response[0] >> 3) & 7;

	if (NTP_VERSION != data->version)
	{
		treegix_log(LOG_LEVEL_DEBUG, "invalid NTP version in the response: %d", (int)data->version);
		goto out;
	}

	data->mode = response[0] & 7;

	if (NTP_MODE_SERVER != data->mode)
	{
		treegix_log(LOG_LEVEL_DEBUG, "invalid mode in the response: %d", (int)data->mode);
		goto out;
	}

	if (15 < response[1])
	{
		treegix_log(LOG_LEVEL_DEBUG, "invalid stratum in the response: %d", (int)response[1]);
		goto out;
	}

	d = 0.0;
	for (i = 0; i < 8; i++)
		d = 256.0 * d + response[NTP_OFFSET_TRANSMIT + i];
	data->transmit = d / NTP_SCALE;

	if (0 == data->transmit)
	{
		treegix_log(LOG_LEVEL_DEBUG, "invalid transmit timestamp in the response: " TRX_FS_DBL, data->transmit);
		goto out;
	}

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

int	check_ntp(char *host, unsigned short port, int timeout, int *value_int)
{
	trx_socket_t	s;
	int		ret;
	char		request[NTP_PACKET_SIZE];
	ntp_data	data;

	*value_int = 0;

	if (SUCCEED == (ret = trx_udp_connect(&s, CONFIG_SOURCE_IP, host, port, timeout)))
	{
		make_packet(&data);

		pack_ntp(&data, (unsigned char *)request, sizeof(request));

		if (SUCCEED == (ret = trx_udp_send(&s, request, sizeof(request), timeout)))
		{
			if (SUCCEED == (ret = trx_udp_recv(&s, timeout)))
			{
				*value_int = (SUCCEED == unpack_ntp(&data, (unsigned char *)request,
						(unsigned char *)s.buffer, (int)s.read_bytes));
			}
		}

		trx_udp_close(&s);
	}

	if (FAIL == ret)
		treegix_log(LOG_LEVEL_DEBUG, "NTP check error: %s", trx_socket_strerror());

	return SYSINFO_RET_OK;
}
