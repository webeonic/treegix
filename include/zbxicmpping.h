

#include "common.h"

typedef struct
{
	char	*addr;
	double	min;
	double	sum;
	double	max;
	int	rcv;
	int	cnt;
	char	*status;	/* array of individual response statuses: 1 - valid, 0 - timeout */
}
TRX_FPING_HOST;

typedef enum
{
	ICMPPING = 0,
	ICMPPINGSEC,
	ICMPPINGLOSS
}
icmpping_t;

typedef enum
{
	ICMPPINGSEC_MIN = 0,
	ICMPPINGSEC_AVG,
	ICMPPINGSEC_MAX
}
icmppingsec_type_t;

typedef struct
{
	int			count;
	int			interval;
	int			size;
	int			timeout;
	zbx_uint64_t		itemid;
	char			*addr;
	icmpping_t		icmpping;
	icmppingsec_type_t	type;
}
icmpitem_t;

int	do_ping(TRX_FPING_HOST *hosts, int hosts_count, int count, int interval, int size, int timeout, char *error, int max_error_len);
