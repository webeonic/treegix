

package trxlib

/* cspell:disable */

/*
#cgo CFLAGS: -I${SRCDIR}/../../../../../include

#cgo LDFLAGS: -Wl,--start-group
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/treegix_agent/logfiles/libtrxlogfiles.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxcomms/libtrxcomms.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxcommon/libtrxcommon.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxcrypto/libtrxcrypto.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxsys/libtrxsys.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxnix/libtrxnix.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxconf/libtrxconf.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxhttp/libtrxhttp.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxcompress/libtrxcompress.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxregexp/libtrxregexp.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxsysinfo/libtrxagentsysinfo.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxsysinfo/common/libcommonsysinfo.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxsysinfo/simple/libsimplesysinfo.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxsysinfo/linux/libspechostnamesysinfo.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxsysinfo/linux/libspecsysinfo.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxexec/libtrxexec.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxalgo/libtrxalgo.a
#cgo LDFLAGS: ${SRCDIR}/../../../../../src/libs/trxjson/libtrxjson.a
#cgo LDFLAGS: -lz -lpcre -lresolv
#cgo LDFLAGS: -Wl,--end-group

#include "common.h"
#include "sysinfo.h"
#include "comms.h"
#include "../src/treegix_agent/metrics.h"
#include "../src/treegix_agent/logfiles/logfiles.h"

typedef TRX_ACTIVE_METRIC* TRX_ACTIVE_METRIC_LP;
typedef trx_vector_ptr_t * trx_vector_ptr_lp_t;

int CONFIG_MAX_LINES_PER_SECOND = 20;
char *CONFIG_HOSTNAME = NULL;
int	CONFIG_UNSAFE_USER_PARAMETERS= 0;
int	CONFIG_ENABLE_REMOTE_COMMANDS= 0;
int	CONFIG_LOG_REMOTE_COMMANDS= 0;
char	*CONFIG_SOURCE_IP= NULL;

unsigned int	configured_tls_connect_mode = TRX_TCP_SEC_UNENCRYPTED;
unsigned int	configured_tls_accept_modes = TRX_TCP_SEC_UNENCRYPTED;

char *CONFIG_TLS_CONNECT= NULL;
char *CONFIG_TLS_ACCEPT	= NULL;
char *CONFIG_TLS_CA_FILE = NULL;
char *CONFIG_TLS_CRL_FILE = NULL;
char *CONFIG_TLS_SERVER_CERT_ISSUER	= NULL;
char *CONFIG_TLS_SERVER_CERT_SUBJECT = NULL;
char *CONFIG_TLS_CERT_FILE = NULL;
char *CONFIG_TLS_KEY_FILE = NULL;
char *CONFIG_TLS_PSK_IDENTITY = NULL;
char *CONFIG_TLS_PSK_FILE = NULL;

int	CONFIG_PASSIVE_FORKS = 0;
int	CONFIG_ACTIVE_FORKS = 0;

const char	*progname = NULL;
const char	title_message[] = "agent";
const char	syslog_app_name[] = "agent";
const char	*usage_message[] = {};
unsigned char	program_type	= 0x80;
const char	*help_message[] = {};

TRX_METRIC	parameters_agent[] = {NULL};
TRX_METRIC	parameters_specific[] = {NULL};

void trx_on_exit(int ret)
{
}

int	trx_procstat_collector_started(void)
{
	return FAIL;
}

int	trx_procstat_get_util(const char *procname, const char *username, const char *cmdline, trx_uint64_t flags,
		int period, int type, double *value, char **errmsg)
{
	return FAIL;
}

int	get_cpustat(AGENT_RESULT *result, int cpu_num, int state, int mode)
{
	return SYSINFO_RET_FAIL;
}
*/
import "C"

const (
	ItemStateNormal       = 0
	ItemStateNotsupported = 1
)

const (
	Succeed = 0
	Fail    = -1
)
