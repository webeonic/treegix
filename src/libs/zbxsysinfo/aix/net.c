

#include "common.h"
#include "sysinfo.h"
#include "trxjson.h"
#include "log.h"

typedef struct
{
	trx_uint64_t ibytes;
	trx_uint64_t ipackets;
	trx_uint64_t ierr;
	trx_uint64_t obytes;
	trx_uint64_t opackets;
	trx_uint64_t oerr;
	trx_uint64_t colls;
}
net_stat_t;

static int	get_net_stat(const char *if_name, net_stat_t *ns, char **error)
{
#if defined(HAVE_LIBPERFSTAT)
	perfstat_id_t		ps_id;
	perfstat_netinterface_t	ps_netif;

	if (NULL == if_name || '\0' == *if_name)
	{
		*error = trx_strdup(NULL, "Network interface name cannot be empty.");
		return SYSINFO_RET_FAIL;
	}

	strscpy(ps_id.name, if_name);

	if (-1 == perfstat_netinterface(&ps_id, &ps_netif, sizeof(ps_netif), 1))
	{
		*error = trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno));
		return SYSINFO_RET_FAIL;
	}

	ns->ibytes = (trx_uint64_t)ps_netif.ibytes;
	ns->ipackets = (trx_uint64_t)ps_netif.ipackets;
	ns->ierr = (trx_uint64_t)ps_netif.ierrors;

	ns->obytes = (trx_uint64_t)ps_netif.obytes;
	ns->opackets = (trx_uint64_t)ps_netif.opackets;
	ns->oerr = (trx_uint64_t)ps_netif.oerrors;

	ns->colls = (trx_uint64_t)ps_netif.collisions;

	return SYSINFO_RET_OK;
#else
	SET_MSG_RESULT(result, trx_strdup(NULL, "Agent was compiled without support for Perfstat API."));
	return SYSINFO_RET_FAIL;
#endif
}

int	NET_IF_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*if_name, *mode, *error;
	net_stat_t	ns;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (SYSINFO_RET_FAIL == get_net_stat(if_name, &ns, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
		SET_UI64_RESULT(result, ns.ibytes);
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, ns.ipackets);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, ns.ierr);
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}

int	NET_IF_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*if_name, *mode, *error;
	net_stat_t	ns;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (SYSINFO_RET_FAIL == get_net_stat(if_name, &ns, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
		SET_UI64_RESULT(result, ns.obytes);
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, ns.opackets);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, ns.oerr);
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}

int	NET_IF_TOTAL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*if_name, *mode, *error;
	net_stat_t	ns;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (SYSINFO_RET_FAIL == get_net_stat(if_name, &ns, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
		SET_UI64_RESULT(result, ns.ibytes + ns.obytes);
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, ns.ipackets + ns.opackets);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, ns.ierr + ns.oerr);
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}

int	NET_IF_COLLISIONS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*if_name, *error;
	net_stat_t	ns;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if_name = get_rparam(request, 0);

	if (SYSINFO_RET_FAIL == get_net_stat(if_name, &ns, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, ns.colls);

	return SYSINFO_RET_OK;
}

int	NET_IF_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
#if defined(HAVE_LIBPERFSTAT)
	int			rc, i, ret = SYSINFO_RET_FAIL;
	perfstat_id_t		ps_id;
	perfstat_netinterface_t	*ps_netif = NULL;
	struct trx_json		j;

	/* check how many perfstat_netinterface_t structures are available */
	if (-1 == (rc = perfstat_netinterface(NULL, NULL, sizeof(perfstat_netinterface_t), 0)))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	trx_json_initarray(&j, TRX_JSON_STAT_BUF_LEN);

	if (0 == rc)	/* no network interfaces found */
	{
		ret = SYSINFO_RET_OK;
		goto end;
	}

	ps_netif = trx_malloc(ps_netif, rc * sizeof(perfstat_netinterface_t));

	/* set name to first interface */
	strscpy(ps_id.name, FIRST_NETINTERFACE);	/* pseudo-name for the first network interface */

	/* ask to get all the structures available in one call */
	/* return code is number of structures returned */
	if (-1 != (rc = perfstat_netinterface(&ps_id, ps_netif, sizeof(perfstat_netinterface_t), rc)))
		ret = SYSINFO_RET_OK;

	/* collecting of the information for each of the interfaces */
	for (i = 0; i < rc; i++)
	{
		trx_json_addobject(&j, NULL);
		trx_json_addstring(&j, "{#IFNAME}", ps_netif[i].name, TRX_JSON_TYPE_STRING);
		trx_json_close(&j);
	}

	trx_free(ps_netif);
end:
	trx_json_close(&j);

	SET_STR_RESULT(result, strdup(j.buffer));

	trx_json_free(&j);

	return ret;
#else
	SET_MSG_RESULT(result, trx_strdup(NULL, "Agent was compiled without support for Perfstat API."));
	return SYSINFO_RET_FAIL;
#endif
}
