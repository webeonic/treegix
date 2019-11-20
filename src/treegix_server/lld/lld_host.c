

#include "lld.h"
#include "db.h"
#include "log.h"
#include "trxalgo.h"
#include "trxserver.h"

typedef struct
{
	trx_uint64_t	hostmacroid;
	char		*macro;
	char		*value;
	char		*description;
#define TRX_FLAG_LLD_HOSTMACRO_UPDATE_VALUE		__UINT64_C(0x00000001)
#define TRX_FLAG_LLD_HOSTMACRO_UPDATE_DESCRIPTION	__UINT64_C(0x00000002)
#define TRX_FLAG_LLD_HOSTMACRO_UPDATE									\
		(TRX_FLAG_LLD_HOSTMACRO_UPDATE_VALUE | TRX_FLAG_LLD_HOSTMACRO_UPDATE_DESCRIPTION)
#define TRX_FLAG_LLD_HOSTMACRO_REMOVE			__UINT64_C(0x00000004)
	trx_uint64_t	flags;
}
trx_lld_hostmacro_t;

static void	lld_hostmacro_free(trx_lld_hostmacro_t *hostmacro)
{
	trx_free(hostmacro->macro);
	trx_free(hostmacro->value);
	trx_free(hostmacro->description);
	trx_free(hostmacro);
}

typedef struct
{
	trx_uint64_t	interfaceid;
	trx_uint64_t	parent_interfaceid;
	char		*ip;
	char		*dns;
	char		*port;
	unsigned char	main;
	unsigned char	main_orig;
	unsigned char	type;
	unsigned char	type_orig;
	unsigned char	useip;
	unsigned char	bulk;
#define TRX_FLAG_LLD_INTERFACE_UPDATE_TYPE	__UINT64_C(0x00000001)	/* interface.type field should be updated  */
#define TRX_FLAG_LLD_INTERFACE_UPDATE_MAIN	__UINT64_C(0x00000002)	/* interface.main field should be updated */
#define TRX_FLAG_LLD_INTERFACE_UPDATE_USEIP	__UINT64_C(0x00000004)	/* interface.useip field should be updated */
#define TRX_FLAG_LLD_INTERFACE_UPDATE_IP	__UINT64_C(0x00000008)	/* interface.ip field should be updated */
#define TRX_FLAG_LLD_INTERFACE_UPDATE_DNS	__UINT64_C(0x00000010)	/* interface.dns field should be updated */
#define TRX_FLAG_LLD_INTERFACE_UPDATE_PORT	__UINT64_C(0x00000020)	/* interface.port field should be updated */
#define TRX_FLAG_LLD_INTERFACE_UPDATE_BULK	__UINT64_C(0x00000040)	/* interface.bulk field should be updated */
#define TRX_FLAG_LLD_INTERFACE_UPDATE								\
		(TRX_FLAG_LLD_INTERFACE_UPDATE_TYPE | TRX_FLAG_LLD_INTERFACE_UPDATE_MAIN |	\
		TRX_FLAG_LLD_INTERFACE_UPDATE_USEIP | TRX_FLAG_LLD_INTERFACE_UPDATE_IP |	\
		TRX_FLAG_LLD_INTERFACE_UPDATE_DNS | TRX_FLAG_LLD_INTERFACE_UPDATE_PORT |	\
		TRX_FLAG_LLD_INTERFACE_UPDATE_BULK)
#define TRX_FLAG_LLD_INTERFACE_REMOVE		__UINT64_C(0x00000080)	/* interfaces which should be deleted */
	trx_uint64_t	flags;
}
trx_lld_interface_t;

static void	lld_interface_free(trx_lld_interface_t *interface)
{
	trx_free(interface->port);
	trx_free(interface->dns);
	trx_free(interface->ip);
	trx_free(interface);
}

typedef struct
{
	trx_uint64_t		hostid;
	trx_vector_uint64_t	new_groupids;		/* host groups which should be added */
	trx_vector_uint64_t	lnk_templateids;	/* templates which should be linked */
	trx_vector_uint64_t	del_templateids;	/* templates which should be unlinked */
	trx_vector_ptr_t	new_hostmacros;		/* host macros which should be added, deleted or updated */
	trx_vector_ptr_t	interfaces;
	char			*host_proto;
	char			*host;
	char			*host_orig;
	char			*name;
	char			*name_orig;
	int			lastcheck;
	int			ts_delete;

#define TRX_FLAG_LLD_HOST_DISCOVERED			__UINT64_C(0x00000001)	/* hosts which should be updated or added */
#define TRX_FLAG_LLD_HOST_UPDATE_HOST			__UINT64_C(0x00000002)	/* hosts.host and host_discovery.host fields should be updated */
#define TRX_FLAG_LLD_HOST_UPDATE_NAME			__UINT64_C(0x00000004)	/* hosts.name field should be updated */
#define TRX_FLAG_LLD_HOST_UPDATE_PROXY			__UINT64_C(0x00000008)	/* hosts.proxy_hostid field should be updated */
#define TRX_FLAG_LLD_HOST_UPDATE_IPMI_AUTH		__UINT64_C(0x00000010)	/* hosts.ipmi_authtype field should be updated */
#define TRX_FLAG_LLD_HOST_UPDATE_IPMI_PRIV		__UINT64_C(0x00000020)	/* hosts.ipmi_privilege field should be updated */
#define TRX_FLAG_LLD_HOST_UPDATE_IPMI_USER		__UINT64_C(0x00000040)	/* hosts.ipmi_username field should be updated */
#define TRX_FLAG_LLD_HOST_UPDATE_IPMI_PASS		__UINT64_C(0x00000080)	/* hosts.ipmi_password field should be updated */
#define TRX_FLAG_LLD_HOST_UPDATE_TLS_CONNECT		__UINT64_C(0x00000100)	/* hosts.tls_connect field should be updated */
#define TRX_FLAG_LLD_HOST_UPDATE_TLS_ACCEPT		__UINT64_C(0x00000200)	/* hosts.tls_accept field should be updated */
#define TRX_FLAG_LLD_HOST_UPDATE_TLS_ISSUER		__UINT64_C(0x00000400)	/* hosts.tls_issuer field should be updated */
#define TRX_FLAG_LLD_HOST_UPDATE_TLS_SUBJECT		__UINT64_C(0x00000800)	/* hosts.tls_subject field should be updated */
#define TRX_FLAG_LLD_HOST_UPDATE_TLS_PSK_IDENTITY	__UINT64_C(0x00001000)	/* hosts.tls_psk_identity field should be updated */
#define TRX_FLAG_LLD_HOST_UPDATE_TLS_PSK		__UINT64_C(0x00002000)	/* hosts.tls_psk field should be updated */

#define TRX_FLAG_LLD_HOST_UPDATE									\
		(TRX_FLAG_LLD_HOST_UPDATE_HOST | TRX_FLAG_LLD_HOST_UPDATE_NAME |			\
		TRX_FLAG_LLD_HOST_UPDATE_PROXY | TRX_FLAG_LLD_HOST_UPDATE_IPMI_AUTH |			\
		TRX_FLAG_LLD_HOST_UPDATE_IPMI_PRIV | TRX_FLAG_LLD_HOST_UPDATE_IPMI_USER |		\
		TRX_FLAG_LLD_HOST_UPDATE_IPMI_PASS | TRX_FLAG_LLD_HOST_UPDATE_TLS_CONNECT |		\
		TRX_FLAG_LLD_HOST_UPDATE_TLS_ACCEPT | TRX_FLAG_LLD_HOST_UPDATE_TLS_ISSUER |		\
		TRX_FLAG_LLD_HOST_UPDATE_TLS_SUBJECT | TRX_FLAG_LLD_HOST_UPDATE_TLS_PSK_IDENTITY |	\
		TRX_FLAG_LLD_HOST_UPDATE_TLS_PSK)

	trx_uint64_t		flags;
	char			inventory_mode;
}
trx_lld_host_t;

static void	lld_host_free(trx_lld_host_t *host)
{
	trx_vector_uint64_destroy(&host->new_groupids);
	trx_vector_uint64_destroy(&host->lnk_templateids);
	trx_vector_uint64_destroy(&host->del_templateids);
	trx_vector_ptr_clear_ext(&host->new_hostmacros, (trx_clean_func_t)lld_hostmacro_free);
	trx_vector_ptr_destroy(&host->new_hostmacros);
	trx_vector_ptr_clear_ext(&host->interfaces, (trx_clean_func_t)lld_interface_free);
	trx_vector_ptr_destroy(&host->interfaces);
	trx_free(host->host_proto);
	trx_free(host->host);
	trx_free(host->host_orig);
	trx_free(host->name);
	trx_free(host->name_orig);
	trx_free(host);
}

typedef struct
{
	trx_uint64_t	group_prototypeid;
	char		*name;
}
trx_lld_group_prototype_t;

static void	lld_group_prototype_free(trx_lld_group_prototype_t *group_prototype)
{
	trx_free(group_prototype->name);
	trx_free(group_prototype);
}

typedef struct
{
	trx_uint64_t		groupid;
	trx_uint64_t		group_prototypeid;
	trx_vector_ptr_t	hosts;
	char			*name_proto;
	char			*name;
	char			*name_orig;
	int			lastcheck;
	int			ts_delete;
#define TRX_FLAG_LLD_GROUP_DISCOVERED		__UINT64_C(0x00000001)	/* groups which should be updated or added */
#define TRX_FLAG_LLD_GROUP_UPDATE_NAME		__UINT64_C(0x00000002)	/* groups.name field should be updated */
#define TRX_FLAG_LLD_GROUP_UPDATE		TRX_FLAG_LLD_GROUP_UPDATE_NAME
	trx_uint64_t		flags;
}
trx_lld_group_t;

static void	lld_group_free(trx_lld_group_t *group)
{
	/* trx_vector_ptr_clear_ext(&group->hosts, (trx_clean_func_t)lld_host_free); is not missing here */
	trx_vector_ptr_destroy(&group->hosts);
	trx_free(group->name_proto);
	trx_free(group->name);
	trx_free(group->name_orig);
	trx_free(group);
}

typedef struct
{
	char				*name;
	/* permission pair (usrgrpid, permission) */
	trx_vector_uint64_pair_t	rights;
	/* reference to the inherited rights */
	trx_vector_uint64_pair_t	*prights;
}
trx_lld_group_rights_t;

/******************************************************************************
 *                                                                            *
 * Function: lld_hosts_get                                                    *
 *                                                                            *
 * Purpose: retrieves existing hosts for the specified host prototype         *
 *                                                                            *
 * Parameters: parent_hostid - [IN] host prototype identifier                 *
 *             hosts         - [OUT] list of hosts                            *
 *                                                                            *
 ******************************************************************************/
static void	lld_hosts_get(trx_uint64_t parent_hostid, trx_vector_ptr_t *hosts, trx_uint64_t proxy_hostid,
		char ipmi_authtype, unsigned char ipmi_privilege, const char *ipmi_username, const char *ipmi_password,
		unsigned char tls_connect, unsigned char tls_accept, const char *tls_issuer,
		const char *tls_subject, const char *tls_psk_identity, const char *tls_psk)
{
	DB_RESULT	result;
	DB_ROW		row;
	trx_lld_host_t	*host;
	trx_uint64_t	db_proxy_hostid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select hd.hostid,hd.host,hd.lastcheck,hd.ts_delete,h.host,h.name,h.proxy_hostid,"
				"h.ipmi_authtype,h.ipmi_privilege,h.ipmi_username,h.ipmi_password,hi.inventory_mode,"
				"h.tls_connect,h.tls_accept,h.tls_issuer,h.tls_subject,h.tls_psk_identity,h.tls_psk"
			" from host_discovery hd"
				" join hosts h"
					" on hd.hostid=h.hostid"
				" left join host_inventory hi"
					" on hd.hostid=hi.hostid"
			" where hd.parent_hostid=" TRX_FS_UI64,
			parent_hostid);

	while (NULL != (row = DBfetch(result)))
	{
		host = (trx_lld_host_t *)trx_malloc(NULL, sizeof(trx_lld_host_t));

		TRX_STR2UINT64(host->hostid, row[0]);
		host->host_proto = trx_strdup(NULL, row[1]);
		host->lastcheck = atoi(row[2]);
		host->ts_delete = atoi(row[3]);
		host->host = trx_strdup(NULL, row[4]);
		host->host_orig = NULL;
		host->name = trx_strdup(NULL, row[5]);
		host->name_orig = NULL;
		host->flags = 0x00;

		TRX_DBROW2UINT64(db_proxy_hostid, row[6]);
		if (db_proxy_hostid != proxy_hostid)
			host->flags |= TRX_FLAG_LLD_HOST_UPDATE_PROXY;

		if ((char)atoi(row[7]) != ipmi_authtype)
			host->flags |= TRX_FLAG_LLD_HOST_UPDATE_IPMI_AUTH;

		if ((unsigned char)atoi(row[8]) != ipmi_privilege)
			host->flags |= TRX_FLAG_LLD_HOST_UPDATE_IPMI_PRIV;

		if (0 != strcmp(row[9], ipmi_username))
			host->flags |= TRX_FLAG_LLD_HOST_UPDATE_IPMI_USER;

		if (0 != strcmp(row[10], ipmi_password))
			host->flags |= TRX_FLAG_LLD_HOST_UPDATE_IPMI_PASS;

		if (atoi(row[12]) != tls_connect)
			host->flags |= TRX_FLAG_LLD_HOST_UPDATE_TLS_CONNECT;

		if (atoi(row[13]) != tls_accept)
			host->flags |= TRX_FLAG_LLD_HOST_UPDATE_TLS_ACCEPT;

		if (0 != strcmp(tls_issuer, row[14]))
			host->flags |= TRX_FLAG_LLD_HOST_UPDATE_TLS_ISSUER;

		if (0 != strcmp(tls_subject, row[15]))
			host->flags |= TRX_FLAG_LLD_HOST_UPDATE_TLS_SUBJECT;

		if (0 != strcmp(tls_psk_identity, row[16]))
			host->flags |= TRX_FLAG_LLD_HOST_UPDATE_TLS_PSK_IDENTITY;

		if (0 != strcmp(tls_psk, row[17]))
			host->flags |= TRX_FLAG_LLD_HOST_UPDATE_TLS_PSK;

		if (SUCCEED == DBis_null(row[11]))
			host->inventory_mode = HOST_INVENTORY_DISABLED;
		else
			host->inventory_mode = (char)atoi(row[11]);

		trx_vector_uint64_create(&host->new_groupids);
		trx_vector_uint64_create(&host->lnk_templateids);
		trx_vector_uint64_create(&host->del_templateids);
		trx_vector_ptr_create(&host->new_hostmacros);
		trx_vector_ptr_create(&host->interfaces);

		trx_vector_ptr_append(hosts, host);
	}
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_hosts_validate                                               *
 *                                                                            *
 * Parameters: hosts - [IN] list of hosts; should be sorted by hostid         *
 *                                                                            *
 ******************************************************************************/
static void	lld_hosts_validate(trx_vector_ptr_t *hosts, char **error)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			i, j;
	trx_lld_host_t		*host, *host_b;
	trx_vector_uint64_t	hostids;
	trx_vector_str_t	tnames, vnames;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&hostids);
	trx_vector_str_create(&tnames);		/* list of technical host names */
	trx_vector_str_create(&vnames);		/* list of visible host names */

	/* checking a host name validity */
	for (i = 0; i < hosts->values_num; i++)
	{
		char	*ch_error;

		host = (trx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		/* only new hosts or hosts with changed host name will be validated */
		if (0 != host->hostid && 0 == (host->flags & TRX_FLAG_LLD_HOST_UPDATE_HOST))
			continue;

		/* host name is valid? */
		if (SUCCEED == trx_check_hostname(host->host, &ch_error))
			continue;

		*error = trx_strdcatf(*error, "Cannot %s host \"%s\": %s.\n",
				(0 != host->hostid ? "update" : "create"), host->host, ch_error);

		trx_free(ch_error);

		if (0 != host->hostid)
		{
			lld_field_str_rollback(&host->host, &host->host_orig, &host->flags,
					TRX_FLAG_LLD_HOST_UPDATE_HOST);
		}
		else
			host->flags &= ~TRX_FLAG_LLD_HOST_DISCOVERED;
	}

	/* checking a visible host name validity */
	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		/* only new hosts or hosts with changed visible name will be validated */
		if (0 != host->hostid && 0 == (host->flags & TRX_FLAG_LLD_HOST_UPDATE_NAME))
			continue;

		/* visible host name is valid utf8 sequence and has a valid length */
		if (SUCCEED == trx_is_utf8(host->name) && '\0' != *host->name &&
				HOST_NAME_LEN >= trx_strlen_utf8(host->name))
		{
			continue;
		}

		trx_replace_invalid_utf8(host->name);
		*error = trx_strdcatf(*error, "Cannot %s host: invalid visible host name \"%s\".\n",
				(0 != host->hostid ? "update" : "create"), host->name);

		if (0 != host->hostid)
		{
			lld_field_str_rollback(&host->name, &host->name_orig, &host->flags,
					TRX_FLAG_LLD_HOST_UPDATE_NAME);
		}
		else
			host->flags &= ~TRX_FLAG_LLD_HOST_DISCOVERED;
	}

	/* checking duplicated host names */
	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		/* only new hosts or hosts with changed host name will be validated */
		if (0 != host->hostid && 0 == (host->flags & TRX_FLAG_LLD_HOST_UPDATE_HOST))
			continue;

		for (j = 0; j < hosts->values_num; j++)
		{
			host_b = (trx_lld_host_t *)hosts->values[j];

			if (0 == (host_b->flags & TRX_FLAG_LLD_HOST_DISCOVERED) || i == j)
				continue;

			if (0 != strcmp(host->host, host_b->host))
				continue;

			*error = trx_strdcatf(*error, "Cannot %s host:"
					" host with the same name \"%s\" already exists.\n",
					(0 != host->hostid ? "update" : "create"), host->host);

			if (0 != host->hostid)
			{
				lld_field_str_rollback(&host->host, &host->host_orig, &host->flags,
						TRX_FLAG_LLD_HOST_UPDATE_HOST);
			}
			else
				host->flags &= ~TRX_FLAG_LLD_HOST_DISCOVERED;
		}
	}

	/* checking duplicated visible host names */
	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		/* only new hosts or hosts with changed visible name will be validated */
		if (0 != host->hostid && 0 == (host->flags & TRX_FLAG_LLD_HOST_UPDATE_NAME))
			continue;

		for (j = 0; j < hosts->values_num; j++)
		{
			host_b = (trx_lld_host_t *)hosts->values[j];

			if (0 == (host_b->flags & TRX_FLAG_LLD_HOST_DISCOVERED) || i == j)
				continue;

			if (0 != strcmp(host->name, host_b->name))
				continue;

			*error = trx_strdcatf(*error, "Cannot %s host:"
					" host with the same visible name \"%s\" already exists.\n",
					(0 != host->hostid ? "update" : "create"), host->name);

			if (0 != host->hostid)
			{
				lld_field_str_rollback(&host->name, &host->name_orig, &host->flags,
						TRX_FLAG_LLD_HOST_UPDATE_NAME);
			}
			else
				host->flags &= ~TRX_FLAG_LLD_HOST_DISCOVERED;
		}
	}

	/* checking duplicated host names and visible host names in DB */

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		if (0 != host->hostid)
			trx_vector_uint64_append(&hostids, host->hostid);

		if (0 == host->hostid || 0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_HOST))
			trx_vector_str_append(&tnames, host->host);

		if (0 == host->hostid || 0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_NAME))
			trx_vector_str_append(&vnames, host->name);
	}

	if (0 != tnames.values_num || 0 != vnames.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select host,name"
				" from hosts"
				" where status in (%d,%d,%d)"
					" and flags<>%d"
					" and",
				HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, HOST_STATUS_TEMPLATE,
				TRX_FLAG_DISCOVERY_PROTOTYPE);

		if (0 != tnames.values_num && 0 != vnames.values_num)
			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " (");

		if (0 != tnames.values_num)
		{
			DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "host",
					(const char **)tnames.values, tnames.values_num);
		}

		if (0 != tnames.values_num && 0 != vnames.values_num)
			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " or");

		if (0 != vnames.values_num)
		{
			DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "name",
					(const char **)vnames.values, vnames.values_num);
		}

		if (0 != tnames.values_num && 0 != vnames.values_num)
			trx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');

		if (0 != hostids.values_num)
		{
			trx_vector_uint64_sort(&hostids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and not");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
					hostids.values, hostids.values_num);
		}

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			for (i = 0; i < hosts->values_num; i++)
			{
				host = (trx_lld_host_t *)hosts->values[i];

				if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
					continue;

				if (0 == strcmp(host->host, row[0]))
				{
					*error = trx_strdcatf(*error, "Cannot %s host:"
							" host with the same name \"%s\" already exists.\n",
							(0 != host->hostid ? "update" : "create"), host->host);

					if (0 != host->hostid)
					{
						lld_field_str_rollback(&host->host, &host->host_orig, &host->flags,
								TRX_FLAG_LLD_HOST_UPDATE_HOST);
					}
					else
						host->flags &= ~TRX_FLAG_LLD_HOST_DISCOVERED;
				}

				if (0 == strcmp(host->name, row[1]))
				{
					*error = trx_strdcatf(*error, "Cannot %s host:"
							" host with the same visible name \"%s\" already exists.\n",
							(0 != host->hostid ? "update" : "create"), host->name);

					if (0 != host->hostid)
					{
						lld_field_str_rollback(&host->name, &host->name_orig, &host->flags,
								TRX_FLAG_LLD_HOST_UPDATE_NAME);
					}
					else
						host->flags &= ~TRX_FLAG_LLD_HOST_DISCOVERED;
				}
			}
		}
		DBfree_result(result);

		trx_free(sql);
	}

	trx_vector_str_destroy(&vnames);
	trx_vector_str_destroy(&tnames);
	trx_vector_uint64_destroy(&hostids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static trx_lld_host_t	*lld_host_make(trx_vector_ptr_t *hosts, const char *host_proto, const char *name_proto,
		const struct trx_json_parse *jp_row, const trx_vector_ptr_t *lld_macros)
{
	char		*buffer = NULL;
	int		i;
	trx_lld_host_t	*host = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		if (0 != (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		buffer = trx_strdup(buffer, host->host_proto);
		substitute_lld_macros(&buffer, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(buffer, TRX_WHITESPACE);

		if (0 == strcmp(host->host, buffer))
			break;
	}

	if (i == hosts->values_num)	/* no host found */
	{
		host = (trx_lld_host_t *)trx_malloc(NULL, sizeof(trx_lld_host_t));

		host->hostid = 0;
		host->host_proto = NULL;
		host->lastcheck = 0;
		host->ts_delete = 0;
		host->host = trx_strdup(NULL, host_proto);
		host->host_orig = NULL;
		substitute_lld_macros(&host->host, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(host->host, TRX_WHITESPACE);
		host->name = trx_strdup(NULL, name_proto);
		substitute_lld_macros(&host->name, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(host->name, TRX_WHITESPACE);
		host->name_orig = NULL;
		trx_vector_uint64_create(&host->new_groupids);
		trx_vector_uint64_create(&host->lnk_templateids);
		trx_vector_uint64_create(&host->del_templateids);
		trx_vector_ptr_create(&host->new_hostmacros);
		trx_vector_ptr_create(&host->interfaces);
		host->flags = TRX_FLAG_LLD_HOST_DISCOVERED;

		trx_vector_ptr_append(hosts, host);
	}
	else
	{
		/* host technical name */
		if (0 != strcmp(host->host_proto, host_proto))	/* the new host prototype differs */
		{
			host->host_orig = host->host;
			host->host = trx_strdup(NULL, host_proto);
			substitute_lld_macros(&host->host, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
			trx_lrtrim(host->host, TRX_WHITESPACE);
			host->flags |= TRX_FLAG_LLD_HOST_UPDATE_HOST;
		}

		/* host visible name */
		buffer = trx_strdup(buffer, name_proto);
		substitute_lld_macros(&buffer, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(buffer, TRX_WHITESPACE);
		if (0 != strcmp(host->name, buffer))
		{
			host->name_orig = host->name;
			host->name = buffer;
			buffer = NULL;
			host->flags |= TRX_FLAG_LLD_HOST_UPDATE_NAME;
		}

		host->flags |= TRX_FLAG_LLD_HOST_DISCOVERED;
	}

	trx_free(buffer);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __func__, (void *)host);

	return host;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_simple_groups_get                                            *
 *                                                                            *
 * Purpose: retrieve list of host groups which should be present on the each  *
 *          discovered host                                                   *
 *                                                                            *
 * Parameters: parent_hostid - [IN] host prototype identifier                 *
 *             groupids      - [OUT] sorted list of host groups               *
 *                                                                            *
 ******************************************************************************/
static void	lld_simple_groups_get(trx_uint64_t parent_hostid, trx_vector_uint64_t *groupids)
{
	DB_RESULT	result;
	DB_ROW		row;
	trx_uint64_t	groupid;

	result = DBselect(
			"select groupid"
			" from group_prototype"
			" where groupid is not null"
				" and hostid=" TRX_FS_UI64,
			parent_hostid);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(groupid, row[0]);
		trx_vector_uint64_append(groupids, groupid);
	}
	DBfree_result(result);

	trx_vector_uint64_sort(groupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_hostgroups_make                                              *
 *                                                                            *
 * Parameters: groupids         - [IN] sorted list of host group ids which    *
 *                                     should be present on the each          *
 *                                     discovered host (Groups)               *
 *             hosts            - [IN/OUT] list of hosts                      *
 *                                         should be sorted by hostid         *
 *             groups           - [IN]  list of host groups (Group prototypes)*
 *             del_hostgroupids - [OUT] sorted list of host groups which      *
 *                                      should be deleted                     *
 *                                                                            *
 ******************************************************************************/
static void	lld_hostgroups_make(const trx_vector_uint64_t *groupids, trx_vector_ptr_t *hosts,
		const trx_vector_ptr_t *groups, trx_vector_uint64_t *del_hostgroupids)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			i, j;
	trx_vector_uint64_t	hostids;
	trx_uint64_t		hostgroupid, hostid, groupid;
	trx_lld_host_t		*host;
	const trx_lld_group_t	*group;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&hostids);

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		trx_vector_uint64_reserve(&host->new_groupids, groupids->values_num);
		for (j = 0; j < groupids->values_num; j++)
			trx_vector_uint64_append(&host->new_groupids, groupids->values[j]);

		if (0 != host->hostid)
			trx_vector_uint64_append(&hostids, host->hostid);
	}

	for (i = 0; i < groups->values_num; i++)
	{
		group = (trx_lld_group_t *)groups->values[i];

		if (0 == (group->flags & TRX_FLAG_LLD_GROUP_DISCOVERED) || 0 == group->groupid)
			continue;

		for (j = 0; j < group->hosts.values_num; j++)
		{
			host = (trx_lld_host_t *)group->hosts.values[j];

			trx_vector_uint64_append(&host->new_groupids, group->groupid);
		}
	}

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];
		trx_vector_uint64_sort(&host->new_groupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	}

	if (0 != hostids.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select hostid,groupid,hostgroupid"
				" from hosts_groups"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids.values, hostids.values_num);

		result = DBselect("%s", sql);

		trx_free(sql);

		while (NULL != (row = DBfetch(result)))
		{
			TRX_STR2UINT64(hostid, row[0]);
			TRX_STR2UINT64(groupid, row[1]);

			if (FAIL == (i = trx_vector_ptr_bsearch(hosts, &hostid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			host = (trx_lld_host_t *)hosts->values[i];

			if (FAIL == (i = trx_vector_uint64_bsearch(&host->new_groupids, groupid,
					TRX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				/* host groups which should be unlinked */
				TRX_STR2UINT64(hostgroupid, row[2]);
				trx_vector_uint64_append(del_hostgroupids, hostgroupid);
			}
			else
			{
				/* host groups which are already added */
				trx_vector_uint64_remove(&host->new_groupids, i);
			}
		}
		DBfree_result(result);

		trx_vector_uint64_sort(del_hostgroupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	}

	trx_vector_uint64_destroy(&hostids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_group_prototypes_get                                         *
 *                                                                            *
 * Purpose: retrieve list of group prototypes                                 *
 *                                                                            *
 * Parameters: parent_hostid    - [IN] host prototype identifier              *
 *             group_prototypes - [OUT] sorted list of group prototypes       *
 *                                                                            *
 ******************************************************************************/
static void	lld_group_prototypes_get(trx_uint64_t parent_hostid, trx_vector_ptr_t *group_prototypes)
{
	DB_RESULT			result;
	DB_ROW				row;
	trx_lld_group_prototype_t	*group_prototype;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select group_prototypeid,name"
			" from group_prototype"
			" where groupid is null"
				" and hostid=" TRX_FS_UI64,
			parent_hostid);

	while (NULL != (row = DBfetch(result)))
	{
		group_prototype = (trx_lld_group_prototype_t *)trx_malloc(NULL, sizeof(trx_lld_group_prototype_t));

		TRX_STR2UINT64(group_prototype->group_prototypeid, row[0]);
		group_prototype->name = trx_strdup(NULL, row[1]);

		trx_vector_ptr_append(group_prototypes, group_prototype);
	}
	DBfree_result(result);

	trx_vector_ptr_sort(group_prototypes, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_groups_get                                                   *
 *                                                                            *
 * Purpose: retrieves existing groups for the specified host prototype        *
 *                                                                            *
 * Parameters: parent_hostid - [IN] host prototype identifier                 *
 *             groups        - [OUT] list of groups                           *
 *                                                                            *
 ******************************************************************************/
static void	lld_groups_get(trx_uint64_t parent_hostid, trx_vector_ptr_t *groups)
{
	DB_RESULT	result;
	DB_ROW		row;
	trx_lld_group_t	*group;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select gd.groupid,gp.group_prototypeid,gd.name,gd.lastcheck,gd.ts_delete,g.name"
			" from group_prototype gp,group_discovery gd"
				" join hstgrp g"
					" on gd.groupid=g.groupid"
			" where gp.group_prototypeid=gd.parent_group_prototypeid"
				" and gp.hostid=" TRX_FS_UI64,
			parent_hostid);

	while (NULL != (row = DBfetch(result)))
	{
		group = (trx_lld_group_t *)trx_malloc(NULL, sizeof(trx_lld_group_t));

		TRX_STR2UINT64(group->groupid, row[0]);
		TRX_STR2UINT64(group->group_prototypeid, row[1]);
		trx_vector_ptr_create(&group->hosts);
		group->name_proto = trx_strdup(NULL, row[2]);
		group->lastcheck = atoi(row[3]);
		group->ts_delete = atoi(row[4]);
		group->name = trx_strdup(NULL, row[5]);
		group->name_orig = NULL;
		group->flags = 0x00;

		trx_vector_ptr_append(groups, group);
	}
	DBfree_result(result);

	trx_vector_ptr_sort(groups, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_group_make                                                   *
 *                                                                            *
 ******************************************************************************/
static trx_lld_group_t	*lld_group_make(trx_vector_ptr_t *groups, trx_uint64_t group_prototypeid,
		const char *name_proto, const struct trx_json_parse *jp_row, const trx_vector_ptr_t *lld_macros)
{
	char		*buffer = NULL;
	int		i;
	trx_lld_group_t	*group = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < groups->values_num; i++)
	{
		group = (trx_lld_group_t *)groups->values[i];

		if (group->group_prototypeid != group_prototypeid)
			continue;

		if (0 != (group->flags & TRX_FLAG_LLD_GROUP_DISCOVERED))
			continue;

		buffer = trx_strdup(buffer, group->name_proto);
		substitute_lld_macros(&buffer, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(buffer, TRX_WHITESPACE);

		if (0 == strcmp(group->name, buffer))
			break;
	}

	if (i == groups->values_num)	/* no group found */
	{
		/* trying to find an already existing group */

		buffer = trx_strdup(buffer, name_proto);
		substitute_lld_macros(&buffer, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(buffer, TRX_WHITESPACE);

		for (i = 0; i < groups->values_num; i++)
		{
			group = (trx_lld_group_t *)groups->values[i];

			if (group->group_prototypeid != group_prototypeid)
				continue;

			if (0 == (group->flags & TRX_FLAG_LLD_GROUP_DISCOVERED))
				continue;

			if (0 == strcmp(group->name, buffer))
				goto out;
		}

		/* otherwise create a new group */

		group = (trx_lld_group_t *)trx_malloc(NULL, sizeof(trx_lld_group_t));

		group->groupid = 0;
		group->group_prototypeid = group_prototypeid;
		trx_vector_ptr_create(&group->hosts);
		group->name_proto = NULL;
		group->name = trx_strdup(NULL, name_proto);
		substitute_lld_macros(&group->name, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(group->name, TRX_WHITESPACE);
		group->name_orig = NULL;
		group->lastcheck = 0;
		group->ts_delete = 0;
		group->flags = 0x00;
		group->flags = TRX_FLAG_LLD_GROUP_DISCOVERED;

		trx_vector_ptr_append(groups, group);
	}
	else
	{
		/* update an already existing group */

		/* group name */
		buffer = trx_strdup(buffer, name_proto);
		substitute_lld_macros(&buffer, jp_row, lld_macros, TRX_MACRO_ANY, NULL, 0);
		trx_lrtrim(buffer, TRX_WHITESPACE);
		if (0 != strcmp(group->name, buffer))
		{
			group->name_orig = group->name;
			group->name = buffer;
			buffer = NULL;
			group->flags |= TRX_FLAG_LLD_GROUP_UPDATE_NAME;
		}

		group->flags |= TRX_FLAG_LLD_GROUP_DISCOVERED;
	}
out:
	trx_free(buffer);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __func__, (void *)group);

	return group;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_groups_make                                                  *
 *                                                                            *
 ******************************************************************************/
static void	lld_groups_make(trx_lld_host_t *host, trx_vector_ptr_t *groups, const trx_vector_ptr_t *group_prototypes,
		const struct trx_json_parse *jp_row, const trx_vector_ptr_t *lld_macros)
{
	int	i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < group_prototypes->values_num; i++)
	{
		const trx_lld_group_prototype_t	*group_prototype;
		trx_lld_group_t			*group;

		group_prototype = (trx_lld_group_prototype_t *)group_prototypes->values[i];

		group = lld_group_make(groups, group_prototype->group_prototypeid, group_prototype->name, jp_row,
				lld_macros);

		trx_vector_ptr_append(&group->hosts, host);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_validate_group_name                                          *
 *                                                                            *
 * Purpose: validate group name                                               *
 *                                                                            *
 * Return value: SUCCEED - the group name is valid                            *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	lld_validate_group_name(const char *name)
{
	/* group name cannot be empty */
	if ('\0' == *name)
		return FAIL;

	/* group name must contain valid utf8 characters */
	if (SUCCEED != trx_is_utf8(name))
		return FAIL;

	/* group name cannot exceed field limits */
	if (GROUP_NAME_LEN < trx_strlen_utf8(name))
		return FAIL;

	/* group name cannot contain trailing and leading slashes (/) */
	if ('/' == *name || '/' == name[strlen(name) - 1])
		return FAIL;

	/* group name cannot contain several slashes (/) in a row */
	if (NULL != strstr(name, "//"))
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_groups_validate                                              *
 *                                                                            *
 * Parameters: groups - [IN] list of groups; should be sorted by groupid      *
 *                                                                            *
 ******************************************************************************/
static void	lld_groups_validate(trx_vector_ptr_t *groups, char **error)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			i, j;
	trx_lld_group_t		*group, *group_b;
	trx_vector_uint64_t	groupids;
	trx_vector_str_t	names;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&groupids);
	trx_vector_str_create(&names);		/* list of group names */

	/* checking a group name validity */
	for (i = 0; i < groups->values_num; i++)
	{
		group = (trx_lld_group_t *)groups->values[i];

		if (0 == (group->flags & TRX_FLAG_LLD_GROUP_DISCOVERED))
			continue;

		/* only new groups or groups with changed group name will be validated */
		if (0 != group->groupid && 0 == (group->flags & TRX_FLAG_LLD_GROUP_UPDATE_NAME))
			continue;

		if (SUCCEED == lld_validate_group_name(group->name))
			continue;

		trx_replace_invalid_utf8(group->name);
		*error = trx_strdcatf(*error, "Cannot %s group: invalid group name \"%s\".\n",
				(0 != group->groupid ? "update" : "create"), group->name);

		if (0 != group->groupid)
		{
			lld_field_str_rollback(&group->name, &group->name_orig, &group->flags,
					TRX_FLAG_LLD_GROUP_UPDATE_NAME);
		}
		else
			group->flags &= ~TRX_FLAG_LLD_GROUP_DISCOVERED;
	}

	/* checking duplicated group names */
	for (i = 0; i < groups->values_num; i++)
	{
		group = (trx_lld_group_t *)groups->values[i];

		if (0 == (group->flags & TRX_FLAG_LLD_GROUP_DISCOVERED))
			continue;

		/* only new groups or groups with changed group name will be validated */
		if (0 != group->groupid && 0 == (group->flags & TRX_FLAG_LLD_GROUP_UPDATE_NAME))
			continue;

		for (j = 0; j < groups->values_num; j++)
		{
			group_b = (trx_lld_group_t *)groups->values[j];

			if (0 == (group_b->flags & TRX_FLAG_LLD_GROUP_DISCOVERED) || i == j)
				continue;

			if (0 != strcmp(group->name, group_b->name))
				continue;

			*error = trx_strdcatf(*error, "Cannot %s group:"
					" group with the same name \"%s\" already exists.\n",
					(0 != group->groupid ? "update" : "create"), group->name);

			if (0 != group->groupid)
			{
				lld_field_str_rollback(&group->name, &group->name_orig, &group->flags,
						TRX_FLAG_LLD_GROUP_UPDATE_NAME);
			}
			else
				group->flags &= ~TRX_FLAG_LLD_GROUP_DISCOVERED;
		}
	}

	/* checking duplicated group names and group names in DB */

	for (i = 0; i < groups->values_num; i++)
	{
		group = (trx_lld_group_t *)groups->values[i];

		if (0 == (group->flags & TRX_FLAG_LLD_GROUP_DISCOVERED))
			continue;

		if (0 != group->groupid)
			trx_vector_uint64_append(&groupids, group->groupid);

		if (0 == group->groupid || 0 != (group->flags & TRX_FLAG_LLD_GROUP_UPDATE_NAME))
			trx_vector_str_append(&names, group->name);
	}

	if (0 != names.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select name from hstgrp where");
		DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "name",
				(const char **)names.values, names.values_num);

		if (0 != groupids.values_num)
		{
			trx_vector_uint64_sort(&groupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and not");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid",
					groupids.values, groupids.values_num);
		}

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			for (i = 0; i < groups->values_num; i++)
			{
				group = (trx_lld_group_t *)groups->values[i];

				if (0 == (group->flags & TRX_FLAG_LLD_GROUP_DISCOVERED))
					continue;

				if (0 == strcmp(group->name, row[0]))
				{
					*error = trx_strdcatf(*error, "Cannot %s group:"
							" group with the same name \"%s\" already exists.\n",
							(0 != group->groupid ? "update" : "create"), group->name);

					if (0 != group->groupid)
					{
						lld_field_str_rollback(&group->name, &group->name_orig, &group->flags,
								TRX_FLAG_LLD_GROUP_UPDATE_NAME);
					}
					else
						group->flags &= ~TRX_FLAG_LLD_GROUP_DISCOVERED;
				}
			}
		}
		DBfree_result(result);

		trx_free(sql);
	}

	trx_vector_str_destroy(&names);
	trx_vector_uint64_destroy(&groupids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_group_rights_compare                                         *
 *                                                                            *
 * Purpose: sorting function to sort group rights vector by name              *
 *                                                                            *
 ******************************************************************************/
static int	lld_group_rights_compare(const void *d1, const void *d2)
{
	const trx_lld_group_rights_t	*r1 = *(const trx_lld_group_rights_t **)d1;
	const trx_lld_group_rights_t	*r2 = *(const trx_lld_group_rights_t **)d2;

	return strcmp(r1->name, r2->name);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_group_rights_append                                          *
 *                                                                            *
 * Purpose: append a new item to group rights vector                          *
 *                                                                            *
 * Return value: Index of the added item.                                     *
 *                                                                            *
 ******************************************************************************/
static int	lld_group_rights_append(trx_vector_ptr_t *group_rights, const char *name)
{
	trx_lld_group_rights_t	*rights;

	rights = (trx_lld_group_rights_t *)trx_malloc(NULL, sizeof(trx_lld_group_rights_t));
	rights->name = trx_strdup(NULL, name);
	trx_vector_uint64_pair_create(&rights->rights);
	rights->prights = NULL;

	trx_vector_ptr_append(group_rights, rights);

	return group_rights->values_num - 1;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_group_rights_free                                            *
 *                                                                            *
 * PUrpose: frees group rights data                                           *
 *                                                                            *
 ******************************************************************************/
static void	lld_group_rights_free(trx_lld_group_rights_t *rights)
{
	trx_free(rights->name);
	trx_vector_uint64_pair_destroy(&rights->rights);
	trx_free(rights);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_groups_save_rights                                           *
 *                                                                            *
 * Parameters: groups - [IN] list of new groups                               *
 *                                                                            *
 ******************************************************************************/
static void	lld_groups_save_rights(trx_vector_ptr_t *groups)
{
	int			i, j;
	DB_ROW			row;
	DB_RESULT		result;
	char			*ptr, *name, *sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0, offset;
	trx_lld_group_t		*group;
	trx_vector_str_t	group_names;
	trx_vector_ptr_t	group_rights;
	trx_db_insert_t		db_insert;
	trx_lld_group_rights_t	*rights, rights_local, *parent_rights;
	trx_uint64_pair_t	pair;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_str_create(&group_names);
	trx_vector_ptr_create(&group_rights);

	/* make a list of direct parent group names and a list of new group rights */
	for (i = 0; i < groups->values_num; i++)
	{
		group = (trx_lld_group_t *)groups->values[i];

		if (NULL == (ptr = strrchr(group->name, '/')))
			continue;

		lld_group_rights_append(&group_rights, group->name);

		name = trx_strdup(NULL, group->name);
		name[ptr - group->name] = '\0';

		if (FAIL != trx_vector_str_search(&group_names, name, TRX_DEFAULT_STR_COMPARE_FUNC))
		{
			trx_free(name);
			continue;
		}

		trx_vector_str_append(&group_names, name);
	}

	if (0 == group_names.values_num)
		goto out;

	/* read the parent group rights */

	trx_db_insert_prepare(&db_insert, "rights", "rightid", "id", "permission", "groupid", NULL);
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select g.name,r.permission,r.groupid from hstgrp g,rights r"
				" where r.id=g.groupid"
				" and");

	DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "g.name", (const char **)group_names.values,
			group_names.values_num);
	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		rights_local.name = row[0];
		if (FAIL == (i = trx_vector_ptr_search(&group_rights, &rights_local, lld_group_rights_compare)))
			i = lld_group_rights_append(&group_rights, row[0]);

		rights = (trx_lld_group_rights_t *)group_rights.values[i];
		rights->prights = &rights->rights;

		TRX_STR2UINT64(pair.first, row[2]);
		pair.second = atoi(row[1]);

		trx_vector_uint64_pair_append(&rights->rights, pair);
	}
	DBfree_result(result);

	trx_vector_ptr_sort(&group_rights, lld_group_rights_compare);

	/* assign rights for the new groups */
	for (i = 0; i < group_rights.values_num; i++)
	{
		rights = (trx_lld_group_rights_t *)group_rights.values[i];

		if (NULL != rights->prights)
			continue;

		if (NULL == (ptr = strrchr(rights->name, '/')))
			continue;

		offset = ptr - rights->name;

		for (j = 0; j < i; j++)
		{
			parent_rights = (trx_lld_group_rights_t *)group_rights.values[j];

			if (strlen(parent_rights->name) != offset)
				continue;

			if (0 != strncmp(parent_rights->name, rights->name, offset))
				continue;

			rights->prights = parent_rights->prights;
			break;
		}
	}

	/* save rights for the new groups */
	for (i = 0; i < groups->values_num; i++)
	{
		group = (trx_lld_group_t *)groups->values[i];

		rights_local.name = group->name;
		if (FAIL == (j = trx_vector_ptr_bsearch(&group_rights, &rights_local, lld_group_rights_compare)))
			continue;

		rights = (trx_lld_group_rights_t *)group_rights.values[j];

		if (NULL == rights->prights)
			continue;

		for (j = 0; j < rights->prights->values_num; j++)
		{
			trx_db_insert_add_values(&db_insert, __UINT64_C(0), group->groupid,
					(int)rights->prights->values[j].second, rights->prights->values[j].first);
		}
	}

	trx_db_insert_autoincrement(&db_insert, "rightid");
	trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);

	trx_free(sql);
	trx_vector_ptr_clear_ext(&group_rights, (trx_clean_func_t)lld_group_rights_free);
	trx_vector_str_clear_ext(&group_names, trx_str_free);
out:
	trx_vector_ptr_destroy(&group_rights);
	trx_vector_str_destroy(&group_names);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_groups_save                                                  *
 *                                                                            *
 * Parameters: groups           - [IN/OUT] list of groups; should be sorted   *
 *                                         by groupid                         *
 *             group_prototypes - [IN] list of group prototypes; should be    *
 *                                     sorted by group_prototypeid            *
 *                                                                            *
 ******************************************************************************/
static void	lld_groups_save(trx_vector_ptr_t *groups, const trx_vector_ptr_t *group_prototypes)
{
	int				i, j, new_groups_num = 0, upd_groups_num = 0;
	trx_lld_group_t			*group;
	const trx_lld_group_prototype_t	*group_prototype;
	trx_lld_host_t			*host;
	trx_uint64_t			groupid = 0;
	char				*sql = NULL, *name_esc, *name_proto_esc;
	size_t				sql_alloc = 0, sql_offset = 0;
	trx_db_insert_t			db_insert, db_insert_gdiscovery;
	trx_vector_ptr_t		new_groups;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < groups->values_num; i++)
	{
		group = (trx_lld_group_t *)groups->values[i];

		if (0 == (group->flags & TRX_FLAG_LLD_GROUP_DISCOVERED))
			continue;

		if (0 == group->groupid)
			new_groups_num++;
		else if (0 != (group->flags & TRX_FLAG_LLD_GROUP_UPDATE))
			upd_groups_num++;
	}

	if (0 == new_groups_num && 0 == upd_groups_num)
		goto out;

	DBbegin();

	if (0 != new_groups_num)
	{
		groupid = DBget_maxid_num("hstgrp", new_groups_num);

		trx_db_insert_prepare(&db_insert, "hstgrp", "groupid", "name", "flags", NULL);

		trx_db_insert_prepare(&db_insert_gdiscovery, "group_discovery", "groupid", "parent_group_prototypeid",
				"name", NULL);

		trx_vector_ptr_create(&new_groups);
	}

	if (0 != upd_groups_num)
	{
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	for (i = 0; i < groups->values_num; i++)
	{
		group = (trx_lld_group_t *)groups->values[i];

		if (0 == (group->flags & TRX_FLAG_LLD_GROUP_DISCOVERED))
			continue;

		if (0 == group->groupid)
		{
			group->groupid = groupid++;

			trx_db_insert_add_values(&db_insert, group->groupid, group->name,
					(int)TRX_FLAG_DISCOVERY_CREATED);

			if (FAIL != (j = trx_vector_ptr_bsearch(group_prototypes, &group->group_prototypeid,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				group_prototype = (trx_lld_group_prototype_t *)group_prototypes->values[j];

				trx_db_insert_add_values(&db_insert_gdiscovery, group->groupid,
						group->group_prototypeid, group_prototype->name);
			}
			else
				THIS_SHOULD_NEVER_HAPPEN;

			for (j = 0; j < group->hosts.values_num; j++)
			{
				host = (trx_lld_host_t *)group->hosts.values[j];

				/* hosts will be linked to a new host groups */
				trx_vector_uint64_append(&host->new_groupids, group->groupid);
			}

			trx_vector_ptr_append(&new_groups, group);
		}
		else
		{
			if (0 != (group->flags & TRX_FLAG_LLD_GROUP_UPDATE))
			{
				trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update hstgrp set ");
				if (0 != (group->flags & TRX_FLAG_LLD_GROUP_UPDATE_NAME))
				{
					name_esc = DBdyn_escape_string(group->name);

					trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "name='%s'", name_esc);

					trx_free(name_esc);
				}
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
						" where groupid=" TRX_FS_UI64 ";\n", group->groupid);
			}

			if (0 != (group->flags & TRX_FLAG_LLD_GROUP_UPDATE_NAME))
			{
				if (FAIL != (j = trx_vector_ptr_bsearch(group_prototypes, &group->group_prototypeid,
						TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
				{
					group_prototype = (trx_lld_group_prototype_t *)group_prototypes->values[j];

					name_proto_esc = DBdyn_escape_string(group_prototype->name);

					trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
							"update group_discovery"
							" set name='%s'"
							" where groupid=" TRX_FS_UI64 ";\n",
							name_proto_esc, group->groupid);

					trx_free(name_proto_esc);
				}
				else
					THIS_SHOULD_NEVER_HAPPEN;
			}
		}
	}

	if (0 != upd_groups_num)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);
		DBexecute("%s", sql);
		trx_free(sql);
	}

	if (0 != new_groups_num)
	{
		trx_db_insert_execute(&db_insert);
		trx_db_insert_clean(&db_insert);

		trx_db_insert_execute(&db_insert_gdiscovery);
		trx_db_insert_clean(&db_insert_gdiscovery);

		lld_groups_save_rights(&new_groups);
		trx_vector_ptr_destroy(&new_groups);
	}

	DBcommit();
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_hostmacros_get                                               *
 *                                                                            *
 * Purpose: retrieve list of host macros which should be present on the each  *
 *          discovered host                                                   *
 *                                                                            *
 * Parameters: hostmacros - [OUT] list of host macros                         *
 *                                                                            *
 ******************************************************************************/
static void	lld_hostmacros_get(trx_uint64_t lld_ruleid, trx_vector_ptr_t *hostmacros)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_lld_hostmacro_t	*hostmacro;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select hm.macro,hm.value,hm.description"
			" from hostmacro hm,items i"
			" where hm.hostid=i.hostid"
				" and i.itemid=" TRX_FS_UI64,
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		hostmacro = (trx_lld_hostmacro_t *)trx_malloc(NULL, sizeof(trx_lld_hostmacro_t));

		hostmacro->macro = trx_strdup(NULL, row[0]);
		hostmacro->value = trx_strdup(NULL, row[1]);
		hostmacro->description = trx_strdup(NULL, row[2]);

		trx_vector_ptr_append(hostmacros, hostmacro);
	}
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_hostmacro_make                                               *
 *                                                                            *
 ******************************************************************************/
static void	lld_hostmacro_make(trx_vector_ptr_t *hostmacros, trx_uint64_t hostmacroid, const char *macro,
		const char *value, const char *description)
{
	trx_lld_hostmacro_t	*hostmacro;
	int			i;

	for (i = 0; i < hostmacros->values_num; i++)
	{
		hostmacro = (trx_lld_hostmacro_t *)hostmacros->values[i];

		/* check if host macro has already been added */
		if (0 == hostmacro->hostmacroid && 0 == strcmp(hostmacro->macro, macro))
		{
			hostmacro->hostmacroid = hostmacroid;
			if (0 != strcmp(hostmacro->value, value))
				hostmacro->flags |= TRX_FLAG_LLD_HOSTMACRO_UPDATE_VALUE;
			if (0 != strcmp(hostmacro->description, description))
				hostmacro->flags |= TRX_FLAG_LLD_HOSTMACRO_UPDATE_DESCRIPTION;
			return;
		}
	}

	/* host macro is present on the host but not in new list, it should be removed */
	hostmacro = (trx_lld_hostmacro_t *)trx_malloc(NULL, sizeof(trx_lld_hostmacro_t));
	hostmacro->hostmacroid = hostmacroid;
	hostmacro->macro = NULL;
	hostmacro->value = NULL;
	hostmacro->description = NULL;
	hostmacro->flags = TRX_FLAG_LLD_HOSTMACRO_REMOVE;

	trx_vector_ptr_append(hostmacros, hostmacro);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_hostmacros_make                                              *
 *                                                                            *
 * Parameters: hostmacros       - [IN] list of host macros which              *
 *                                     should be present on the each          *
 *                                     discovered host                        *
 *             hosts            - [IN/OUT] list of hosts                      *
 *                                         should be sorted by hostid         *
 *             del_hostmacroids - [OUT] list of host macros which should be   *
 *                                      deleted                               *
 *                                                                            *
 ******************************************************************************/
static void	lld_hostmacros_make(const trx_vector_ptr_t *hostmacros, trx_vector_ptr_t *hosts)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			i, j;
	trx_vector_uint64_t	hostids;
	trx_uint64_t		hostmacroid, hostid;
	trx_lld_host_t		*host;
	trx_lld_hostmacro_t	*hostmacro = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&hostids);

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		trx_vector_ptr_reserve(&host->new_hostmacros, hostmacros->values_num);
		for (j = 0; j < hostmacros->values_num; j++)
		{
			hostmacro = (trx_lld_hostmacro_t *)trx_malloc(NULL, sizeof(trx_lld_hostmacro_t));

			hostmacro->hostmacroid = 0;
			hostmacro->macro = trx_strdup(NULL, ((trx_lld_hostmacro_t *)hostmacros->values[j])->macro);
			hostmacro->value = trx_strdup(NULL, ((trx_lld_hostmacro_t *)hostmacros->values[j])->value);
			hostmacro->description = trx_strdup(NULL,
					((trx_lld_hostmacro_t *)hostmacros->values[j])->description);
			hostmacro->flags = 0x00;

			trx_vector_ptr_append(&host->new_hostmacros, hostmacro);
		}

		if (0 != host->hostid)
			trx_vector_uint64_append(&hostids, host->hostid);
	}

	if (0 != hostids.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select hostmacroid,hostid,macro,value,description"
				" from hostmacro"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids.values, hostids.values_num);

		result = DBselect("%s", sql);

		trx_free(sql);

		while (NULL != (row = DBfetch(result)))
		{
			TRX_STR2UINT64(hostid, row[1]);

			if (FAIL == (i = trx_vector_ptr_bsearch(hosts, &hostid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			host = (trx_lld_host_t *)hosts->values[i];

			TRX_STR2UINT64(hostmacroid, row[0]);

			lld_hostmacro_make(&host->new_hostmacros, hostmacroid, row[2], row[3], row[4]);
		}
		DBfree_result(result);
	}

	trx_vector_uint64_destroy(&hostids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_templates_make                                               *
 *                                                                            *
 * Purpose: gets templates from a host prototype                              *
 *                                                                            *
 * Parameters: parent_hostid - [IN] host prototype identifier                 *
 *             hosts         - [IN/OUT] list of hosts                         *
 *                                      should be sorted by hostid            *
 *                                                                            *
 ******************************************************************************/
static void	lld_templates_make(trx_uint64_t parent_hostid, trx_vector_ptr_t *hosts)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_vector_uint64_t	templateids, hostids;
	trx_uint64_t		templateid, hostid;
	trx_lld_host_t		*host;
	int			i, j;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&templateids);
	trx_vector_uint64_create(&hostids);

	/* select templates which should be linked */

	result = DBselect("select templateid from hosts_templates where hostid=" TRX_FS_UI64, parent_hostid);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(templateid, row[0]);
		trx_vector_uint64_append(&templateids, templateid);
	}
	DBfree_result(result);

	trx_vector_uint64_sort(&templateids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	/* select list of already created hosts */

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		trx_vector_uint64_reserve(&host->lnk_templateids, templateids.values_num);
		for (j = 0; j < templateids.values_num; j++)
			trx_vector_uint64_append(&host->lnk_templateids, templateids.values[j]);

		if (0 != host->hostid)
			trx_vector_uint64_append(&hostids, host->hostid);
	}

	if (0 != hostids.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		/* select already linked temlates */

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select hostid,templateid"
				" from hosts_templates"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids.values, hostids.values_num);

		result = DBselect("%s", sql);

		trx_free(sql);

		while (NULL != (row = DBfetch(result)))
		{
			TRX_STR2UINT64(hostid, row[0]);
			TRX_STR2UINT64(templateid, row[1]);

			if (FAIL == (i = trx_vector_ptr_bsearch(hosts, &hostid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			host = (trx_lld_host_t *)hosts->values[i];

			if (FAIL == (i = trx_vector_uint64_bsearch(&host->lnk_templateids, templateid,
					TRX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				/* templates which should be unlinked */
				trx_vector_uint64_append(&host->del_templateids, templateid);
			}
			else
			{
				/* templates which are already linked */
				trx_vector_uint64_remove(&host->lnk_templateids, i);
			}
		}
		DBfree_result(result);

		for (i = 0; i < hosts->values_num; i++)
		{
			host = (trx_lld_host_t *)hosts->values[i];

			if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
				continue;

			trx_vector_uint64_sort(&host->del_templateids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
		}
	}

	trx_vector_uint64_destroy(&hostids);
	trx_vector_uint64_destroy(&templateids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_hosts_save                                                   *
 *                                                                            *
 * Parameters: hosts            - [IN] list of hosts;                         *
 *                                     should be sorted by hostid             *
 *             status           - [IN] initial host status                    *
 *             del_hostgroupids - [IN] host groups which should be deleted    *
 *                                                                            *
 ******************************************************************************/
static void	lld_hosts_save(trx_uint64_t parent_hostid, trx_vector_ptr_t *hosts, const char *host_proto,
		trx_uint64_t proxy_hostid, char ipmi_authtype, unsigned char ipmi_privilege, const char *ipmi_username,
		const char *ipmi_password, unsigned char status, char inventory_mode, unsigned char tls_connect,
		unsigned char tls_accept, const char *tls_issuer, const char *tls_subject, const char *tls_psk_identity,
		const char *tls_psk, const trx_vector_uint64_t *del_hostgroupids)
{
	int			i, j, new_hosts = 0, new_host_inventories = 0, upd_hosts = 0, new_hostgroups = 0,
				new_hostmacros = 0, upd_hostmacros = 0, new_interfaces = 0, upd_interfaces = 0;
	trx_lld_host_t		*host;
	trx_lld_hostmacro_t	*hostmacro;
	trx_lld_interface_t	*interface;
	trx_vector_uint64_t	upd_host_inventory_hostids, del_host_inventory_hostids, del_interfaceids,
				del_hostmacroids;
	trx_uint64_t		hostid = 0, hostgroupid = 0, hostmacroid = 0, interfaceid = 0;
	char			*sql1 = NULL, *sql2 = NULL, *value_esc;
	size_t			sql1_alloc = 0, sql1_offset = 0,
				sql2_alloc = 0, sql2_offset = 0;
	trx_db_insert_t		db_insert, db_insert_hdiscovery, db_insert_hinventory, db_insert_hgroups,
				db_insert_hmacro, db_insert_interface, db_insert_idiscovery;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&upd_host_inventory_hostids);
	trx_vector_uint64_create(&del_host_inventory_hostids);
	trx_vector_uint64_create(&del_interfaceids);
	trx_vector_uint64_create(&del_hostmacroids);

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		if (0 == host->hostid)
		{
			new_hosts++;
			if (HOST_INVENTORY_DISABLED != inventory_mode)
				new_host_inventories++;
		}
		else
		{
			if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE))
				upd_hosts++;

			if (host->inventory_mode != inventory_mode)
			{
				if (HOST_INVENTORY_DISABLED == inventory_mode)
					trx_vector_uint64_append(&del_host_inventory_hostids, host->hostid);
				else if (HOST_INVENTORY_DISABLED == host->inventory_mode)
					new_host_inventories++;
				else
					trx_vector_uint64_append(&upd_host_inventory_hostids, host->hostid);
			}
		}

		new_hostgroups += host->new_groupids.values_num;

		for (j = 0; j < host->interfaces.values_num; j++)
		{
			interface = (trx_lld_interface_t *)host->interfaces.values[j];

			if (0 == interface->interfaceid)
				new_interfaces++;
			else if (0 != (interface->flags & TRX_FLAG_LLD_INTERFACE_UPDATE))
				upd_interfaces++;
			else if (0 != (interface->flags & TRX_FLAG_LLD_INTERFACE_REMOVE))
				trx_vector_uint64_append(&del_interfaceids, interface->interfaceid);
		}

		for (j = 0; j < host->new_hostmacros.values_num; j++)
		{
			hostmacro = (trx_lld_hostmacro_t *)host->new_hostmacros.values[j];

			if (0 == hostmacro->hostmacroid)
				new_hostmacros++;
			else if (0 != (hostmacro->flags & TRX_FLAG_LLD_HOSTMACRO_UPDATE))
				upd_hostmacros++;
			else if (0 != (hostmacro->flags & TRX_FLAG_LLD_HOSTMACRO_REMOVE))
				trx_vector_uint64_append(&del_hostmacroids, hostmacro->hostmacroid);
		}
	}

	if (0 == new_hosts && 0 == new_host_inventories && 0 == upd_hosts && 0 == upd_interfaces &&
			0 == upd_hostmacros && 0 == new_hostgroups && 0 == new_hostmacros && 0 == new_interfaces &&
			0 == del_hostgroupids->values_num && 0 == del_hostmacroids.values_num &&
			0 == upd_host_inventory_hostids.values_num && 0 == del_host_inventory_hostids.values_num &&
			0 == del_interfaceids.values_num)
	{
		goto out;
	}

	DBbegin();

	if (0 != new_hosts)
	{
		hostid = DBget_maxid_num("hosts", new_hosts);

		trx_db_insert_prepare(&db_insert, "hosts", "hostid", "host", "name", "proxy_hostid", "ipmi_authtype",
				"ipmi_privilege", "ipmi_username", "ipmi_password", "status", "flags", "tls_connect",
				"tls_accept", "tls_issuer", "tls_subject", "tls_psk_identity", "tls_psk", NULL);

		trx_db_insert_prepare(&db_insert_hdiscovery, "host_discovery", "hostid", "parent_hostid", "host", NULL);
	}

	if (0 != new_host_inventories)
	{
		trx_db_insert_prepare(&db_insert_hinventory, "host_inventory", "hostid", "inventory_mode", NULL);
	}

	if (0 != upd_hosts || 0 != upd_interfaces || 0 != upd_hostmacros)
	{
		DBbegin_multiple_update(&sql1, &sql1_alloc, &sql1_offset);
	}

	if (0 != new_hostgroups)
	{
		hostgroupid = DBget_maxid_num("hosts_groups", new_hostgroups);

		trx_db_insert_prepare(&db_insert_hgroups, "hosts_groups", "hostgroupid", "hostid", "groupid", NULL);
	}

	if (0 != new_hostmacros)
	{
		hostmacroid = DBget_maxid_num("hostmacro", new_hostmacros);

		trx_db_insert_prepare(&db_insert_hmacro, "hostmacro", "hostmacroid", "hostid", "macro", "value",
				"description", NULL);
	}

	if (0 != new_interfaces)
	{
		interfaceid = DBget_maxid_num("interface", new_interfaces);

		trx_db_insert_prepare(&db_insert_interface, "interface", "interfaceid", "hostid", "type", "main",
				"useip", "ip", "dns", "port", "bulk", NULL);

		trx_db_insert_prepare(&db_insert_idiscovery, "interface_discovery", "interfaceid",
				"parent_interfaceid", NULL);
	}

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		if (0 == host->hostid)
		{
			host->hostid = hostid++;

			trx_db_insert_add_values(&db_insert, host->hostid, host->host, host->name, proxy_hostid,
					(int)ipmi_authtype, (int)ipmi_privilege, ipmi_username, ipmi_password,
					(int)status, (int)TRX_FLAG_DISCOVERY_CREATED, (int)tls_connect,
					(int)tls_accept, tls_issuer, tls_subject, tls_psk_identity, tls_psk);

			trx_db_insert_add_values(&db_insert_hdiscovery, host->hostid, parent_hostid, host_proto);

			if (HOST_INVENTORY_DISABLED != inventory_mode)
				trx_db_insert_add_values(&db_insert_hinventory, host->hostid, (int)inventory_mode);
		}
		else
		{
			if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE))
			{
				const char	*d = "";

				trx_strcpy_alloc(&sql1, &sql1_alloc, &sql1_offset, "update hosts set ");
				if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_HOST))
				{
					value_esc = DBdyn_escape_string(host->host);

					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, "host='%s'", value_esc);
					d = ",";

					trx_free(value_esc);
				}
				if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_NAME))
				{
					value_esc = DBdyn_escape_string(host->name);

					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
							"%sname='%s'", d, value_esc);
					d = ",";

					trx_free(value_esc);
				}
				if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_PROXY))
				{
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
							"%sproxy_hostid=%s", d, DBsql_id_ins(proxy_hostid));
					d = ",";
				}
				if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_IPMI_AUTH))
				{
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
							"%sipmi_authtype=%d", d, (int)ipmi_authtype);
					d = ",";
				}
				if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_IPMI_PRIV))
				{
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
							"%sipmi_privilege=%d", d, (int)ipmi_privilege);
					d = ",";
				}
				if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_IPMI_USER))
				{
					value_esc = DBdyn_escape_string(ipmi_username);

					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
							"%sipmi_username='%s'", d, value_esc);
					d = ",";

					trx_free(value_esc);
				}
				if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_IPMI_PASS))
				{
					value_esc = DBdyn_escape_string(ipmi_password);

					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
							"%sipmi_password='%s'", d, value_esc);
					d = ",";

					trx_free(value_esc);
				}
				if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_TLS_CONNECT))
				{
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
							"%stls_connect=%d", d, tls_connect);
					d = ",";
				}
				if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_TLS_ACCEPT))
				{
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
							"%stls_accept=%d", d, tls_accept);
					d = ",";
				}
				if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_TLS_ISSUER))
				{
					value_esc = DBdyn_escape_string(tls_issuer);

					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
							"%stls_issuer='%s'", d, value_esc);
					d = ",";

					trx_free(value_esc);
				}
				if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_TLS_SUBJECT))
				{
					value_esc = DBdyn_escape_string(tls_subject);

					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
							"%stls_subject='%s'", d, value_esc);
					d = ",";

					trx_free(value_esc);
				}
				if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_TLS_PSK_IDENTITY))
				{
					value_esc = DBdyn_escape_string(tls_psk_identity);

					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
							"%stls_psk_identity='%s'", d, value_esc);
					d = ",";

					trx_free(value_esc);
				}
				if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_TLS_PSK))
				{
					value_esc = DBdyn_escape_string(tls_psk);

					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
							"%stls_psk='%s'", d, value_esc);

					trx_free(value_esc);
				}
				trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, " where hostid=" TRX_FS_UI64 ";\n",
						host->hostid);
			}

			if (host->inventory_mode != inventory_mode && HOST_INVENTORY_DISABLED == host->inventory_mode)
				trx_db_insert_add_values(&db_insert_hinventory, host->hostid, (int)inventory_mode);

			if (0 != (host->flags & TRX_FLAG_LLD_HOST_UPDATE_HOST))
			{
				value_esc = DBdyn_escape_string(host_proto);

				trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
						"update host_discovery"
						" set host='%s'"
						" where hostid=" TRX_FS_UI64 ";\n",
						value_esc, host->hostid);

				trx_free(value_esc);
			}
		}

		for (j = 0; j < host->interfaces.values_num; j++)
		{
			interface = (trx_lld_interface_t *)host->interfaces.values[j];

			if (0 == interface->interfaceid)
			{
				interface->interfaceid = interfaceid++;

				trx_db_insert_add_values(&db_insert_interface, interface->interfaceid, host->hostid,
						(int)interface->type, (int)interface->main, (int)interface->useip,
						interface->ip, interface->dns, interface->port, (int)interface->bulk);

				trx_db_insert_add_values(&db_insert_idiscovery, interface->interfaceid,
						interface->parent_interfaceid);
			}
			else if (0 != (interface->flags & TRX_FLAG_LLD_INTERFACE_UPDATE))
			{
				const char	*d = "";

				trx_strcpy_alloc(&sql1, &sql1_alloc, &sql1_offset, "update interface set ");
				if (0 != (interface->flags & TRX_FLAG_LLD_INTERFACE_UPDATE_TYPE))
				{
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, "type=%d",
							(int)interface->type);
					d = ",";
				}
				if (0 != (interface->flags & TRX_FLAG_LLD_INTERFACE_UPDATE_MAIN))
				{
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, "%smain=%d",
							d, (int)interface->main);
					d = ",";
				}
				if (0 != (interface->flags & TRX_FLAG_LLD_INTERFACE_UPDATE_USEIP))
				{
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, "%suseip=%d",
							d, (int)interface->useip);
					d = ",";
				}
				if (0 != (interface->flags & TRX_FLAG_LLD_INTERFACE_UPDATE_IP))
				{
					value_esc = DBdyn_escape_string(interface->ip);
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, "%sip='%s'", d, value_esc);
					trx_free(value_esc);
					d = ",";
				}
				if (0 != (interface->flags & TRX_FLAG_LLD_INTERFACE_UPDATE_DNS))
				{
					value_esc = DBdyn_escape_string(interface->dns);
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, "%sdns='%s'", d, value_esc);
					trx_free(value_esc);
					d = ",";
				}
				if (0 != (interface->flags & TRX_FLAG_LLD_INTERFACE_UPDATE_PORT))
				{
					value_esc = DBdyn_escape_string(interface->port);
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, "%sport='%s'",
							d, value_esc);
					trx_free(value_esc);
					d = ",";
				}
				if (0 != (interface->flags & TRX_FLAG_LLD_INTERFACE_UPDATE_BULK))
				{
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, "%sbulk=%d",
							d, (int)interface->bulk);
				}
				trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
						" where interfaceid=" TRX_FS_UI64 ";\n", interface->interfaceid);
			}
		}

		for (j = 0; j < host->new_groupids.values_num; j++)
		{
			trx_db_insert_add_values(&db_insert_hgroups, hostgroupid++, host->hostid,
					host->new_groupids.values[j]);
		}

		for (j = 0; j < host->new_hostmacros.values_num; j++)
		{
			hostmacro = (trx_lld_hostmacro_t *)host->new_hostmacros.values[j];

			if (0 == hostmacro->hostmacroid)
			{
				trx_db_insert_add_values(&db_insert_hmacro, hostmacroid++, host->hostid,
						hostmacro->macro, hostmacro->value, hostmacro->description);
			}
			else if (0 != (hostmacro->flags & TRX_FLAG_LLD_HOSTMACRO_UPDATE))
			{
				const char	*d = "";

				trx_strcpy_alloc(&sql1, &sql1_alloc, &sql1_offset, "update hostmacro set ");
				if (0 != (hostmacro->flags & TRX_FLAG_LLD_HOSTMACRO_UPDATE_VALUE))
				{
					value_esc = DBdyn_escape_string(hostmacro->value);
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, "value='%s'", value_esc);
					trx_free(value_esc);
					d = ",";
				}
				if (0 != (hostmacro->flags & TRX_FLAG_LLD_HOSTMACRO_UPDATE_DESCRIPTION))
				{
					value_esc = DBdyn_escape_string(hostmacro->description);
					trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, "%sdescription='%s'",
							d, value_esc);
					trx_free(value_esc);
				}
				trx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
						" where hostmacroid=" TRX_FS_UI64 ";\n", hostmacro->hostmacroid);
			}
		}
	}

	if (0 != new_hosts)
	{
		trx_db_insert_execute(&db_insert);
		trx_db_insert_clean(&db_insert);

		trx_db_insert_execute(&db_insert_hdiscovery);
		trx_db_insert_clean(&db_insert_hdiscovery);
	}

	if (0 != new_host_inventories)
	{
		trx_db_insert_execute(&db_insert_hinventory);
		trx_db_insert_clean(&db_insert_hinventory);
	}

	if (0 != new_hostgroups)
	{
		trx_db_insert_execute(&db_insert_hgroups);
		trx_db_insert_clean(&db_insert_hgroups);
	}

	if (0 != new_hostmacros)
	{
		trx_db_insert_execute(&db_insert_hmacro);
		trx_db_insert_clean(&db_insert_hmacro);
	}

	if (0 != new_interfaces)
	{
		trx_db_insert_execute(&db_insert_interface);
		trx_db_insert_clean(&db_insert_interface);

		trx_db_insert_execute(&db_insert_idiscovery);
		trx_db_insert_clean(&db_insert_idiscovery);
	}

	if (0 != upd_hosts || 0 != upd_interfaces || 0 != upd_hostmacros)
	{
		DBend_multiple_update(&sql1, &sql1_alloc, &sql1_offset);
		DBexecute("%s", sql1);
		trx_free(sql1);
	}

	if (0 != del_hostgroupids->values_num || 0 != del_hostmacroids.values_num ||
			0 != upd_host_inventory_hostids.values_num || 0 != del_host_inventory_hostids.values_num ||
			0 != del_interfaceids.values_num)
	{
		DBbegin_multiple_update(&sql2, &sql2_alloc, &sql2_offset);

		if (0 != del_hostgroupids->values_num)
		{
			trx_strcpy_alloc(&sql2, &sql2_alloc, &sql2_offset, "delete from hosts_groups where");
			DBadd_condition_alloc(&sql2, &sql2_alloc, &sql2_offset, "hostgroupid",
					del_hostgroupids->values, del_hostgroupids->values_num);
			trx_strcpy_alloc(&sql2, &sql2_alloc, &sql2_offset, ";\n");
		}

		if (0 != del_hostmacroids.values_num)
		{
			trx_vector_uint64_sort(&del_hostmacroids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
			trx_strcpy_alloc(&sql2, &sql2_alloc, &sql2_offset, "delete from hostmacro where");
			DBadd_condition_alloc(&sql2, &sql2_alloc, &sql2_offset, "hostmacroid",
					del_hostmacroids.values, del_hostmacroids.values_num);
			trx_strcpy_alloc(&sql2, &sql2_alloc, &sql2_offset, ";\n");
		}

		if (0 != upd_host_inventory_hostids.values_num)
		{
			trx_snprintf_alloc(&sql2, &sql2_alloc, &sql2_offset,
					"update host_inventory set inventory_mode=%d where", (int)inventory_mode);
			DBadd_condition_alloc(&sql2, &sql2_alloc, &sql2_offset, "hostid",
					upd_host_inventory_hostids.values, upd_host_inventory_hostids.values_num);
			trx_strcpy_alloc(&sql2, &sql2_alloc, &sql2_offset, ";\n");
		}

		if (0 != del_host_inventory_hostids.values_num)
		{
			trx_strcpy_alloc(&sql2, &sql2_alloc, &sql2_offset, "delete from host_inventory where");
			DBadd_condition_alloc(&sql2, &sql2_alloc, &sql2_offset, "hostid",
					del_host_inventory_hostids.values, del_host_inventory_hostids.values_num);
			trx_strcpy_alloc(&sql2, &sql2_alloc, &sql2_offset, ";\n");
		}

		if (0 != del_interfaceids.values_num)
		{
			trx_strcpy_alloc(&sql2, &sql2_alloc, &sql2_offset, "delete from interface where");
			DBadd_condition_alloc(&sql2, &sql2_alloc, &sql2_offset, "interfaceid",
					del_interfaceids.values, del_interfaceids.values_num);
			trx_strcpy_alloc(&sql2, &sql2_alloc, &sql2_offset, ";\n");
		}

		DBend_multiple_update(&sql2, &sql2_alloc, &sql2_offset);
		DBexecute("%s", sql2);
		trx_free(sql2);
	}

	DBcommit();
out:
	trx_vector_uint64_destroy(&del_interfaceids);
	trx_vector_uint64_destroy(&del_hostmacroids);
	trx_vector_uint64_destroy(&del_host_inventory_hostids);
	trx_vector_uint64_destroy(&upd_host_inventory_hostids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_templates_link                                               *
 *                                                                            *
 ******************************************************************************/
static void	lld_templates_link(const trx_vector_ptr_t *hosts, char **error)
{
	int		i;
	trx_lld_host_t	*host;
	char		*err;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		if (0 != host->del_templateids.values_num)
		{
			if (SUCCEED != DBdelete_template_elements(host->hostid, &host->del_templateids, &err))
			{
				*error = trx_strdcatf(*error, "Cannot unlink template: %s.\n", err);
				trx_free(err);
			}
		}

		if (0 != host->lnk_templateids.values_num)
		{
			if (SUCCEED != DBcopy_template_elements(host->hostid, &host->lnk_templateids, &err))
			{
				*error = trx_strdcatf(*error, "Cannot link template(s) %s.\n", err);
				trx_free(err);
			}
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_hosts_remove                                                 *
 *                                                                            *
 * Purpose: updates host_discovery.lastcheck and host_discovery.ts_delete     *
 *          fields; removes lost resources                                    *
 *                                                                            *
 ******************************************************************************/
static void	lld_hosts_remove(const trx_vector_ptr_t *hosts, int lifetime, int lastcheck)
{
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	const trx_lld_host_t	*host;
	trx_vector_uint64_t	del_hostids, lc_hostids, ts_hostids;
	int			i;

	if (0 == hosts->values_num)
		return;

	trx_vector_uint64_create(&del_hostids);
	trx_vector_uint64_create(&lc_hostids);
	trx_vector_uint64_create(&ts_hostids);

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		if (0 == host->hostid)
			continue;

		if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
		{
			int	ts_delete = lld_end_of_life(host->lastcheck, lifetime);

			if (lastcheck > ts_delete)
			{
				trx_vector_uint64_append(&del_hostids, host->hostid);
			}
			else if (host->ts_delete != ts_delete)
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
						"update host_discovery"
						" set ts_delete=%d"
						" where hostid=" TRX_FS_UI64 ";\n",
						ts_delete, host->hostid);
			}
		}
		else
		{
			trx_vector_uint64_append(&lc_hostids, host->hostid);
			if (0 != host->ts_delete)
				trx_vector_uint64_append(&ts_hostids, host->hostid);
		}
	}

	if (0 != lc_hostids.values_num)
	{
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update host_discovery set lastcheck=%d where",
				lastcheck);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
				lc_hostids.values, lc_hostids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	if (0 != ts_hostids.values_num)
	{
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update host_discovery set ts_delete=0 where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
				ts_hostids.values, ts_hostids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	if (16 < sql_offset)	/* in ORACLE always present begin..end; */
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		DBbegin();

		DBexecute("%s", sql);

		DBcommit();
	}

	trx_free(sql);

	if (0 != del_hostids.values_num)
	{
		trx_vector_uint64_sort(&del_hostids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		DBbegin();

		DBdelete_hosts(&del_hostids);

		DBcommit();
	}

	trx_vector_uint64_destroy(&ts_hostids);
	trx_vector_uint64_destroy(&lc_hostids);
	trx_vector_uint64_destroy(&del_hostids);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_groups_remove                                                *
 *                                                                            *
 * Purpose: updates group_discovery.lastcheck and group_discovery.ts_delete   *
 *          fields; removes lost resources                                    *
 *                                                                            *
 ******************************************************************************/
static void	lld_groups_remove(const trx_vector_ptr_t *groups, int lifetime, int lastcheck)
{
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	const trx_lld_group_t	*group;
	trx_vector_uint64_t	del_groupids, lc_groupids, ts_groupids;
	int			i;

	if (0 == groups->values_num)
		return;

	trx_vector_uint64_create(&del_groupids);
	trx_vector_uint64_create(&lc_groupids);
	trx_vector_uint64_create(&ts_groupids);

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	for (i = 0; i < groups->values_num; i++)
	{
		group = (trx_lld_group_t *)groups->values[i];

		if (0 == group->groupid)
			continue;

		if (0 == (group->flags & TRX_FLAG_LLD_GROUP_DISCOVERED))
		{
			int	ts_delete = lld_end_of_life(group->lastcheck, lifetime);

			if (lastcheck > ts_delete)
			{
				trx_vector_uint64_append(&del_groupids, group->groupid);
			}
			else if (group->ts_delete != ts_delete)
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
						"update group_discovery"
						" set ts_delete=%d"
						" where groupid=" TRX_FS_UI64 ";\n",
						ts_delete, group->groupid);
			}
		}
		else
		{
			trx_vector_uint64_append(&lc_groupids, group->groupid);
			if (0 != group->ts_delete)
				trx_vector_uint64_append(&ts_groupids, group->groupid);
		}
	}

	if (0 != lc_groupids.values_num)
	{
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update group_discovery set lastcheck=%d where",
				lastcheck);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid",
				lc_groupids.values, lc_groupids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	if (0 != ts_groupids.values_num)
	{
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update group_discovery set ts_delete=0 where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid",
				ts_groupids.values, ts_groupids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	if (16 < sql_offset)	/* in ORACLE always present begin..end; */
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		DBbegin();

		DBexecute("%s", sql);

		DBcommit();
	}

	trx_free(sql);

	if (0 != del_groupids.values_num)
	{
		trx_vector_uint64_sort(&del_groupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		DBbegin();

		DBdelete_groups(&del_groupids);

		DBcommit();
	}

	trx_vector_uint64_destroy(&ts_groupids);
	trx_vector_uint64_destroy(&lc_groupids);
	trx_vector_uint64_destroy(&del_groupids);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_interfaces_get                                               *
 *                                                                            *
 * Purpose: retrieves list of interfaces from the lld rule's host             *
 *                                                                            *
 ******************************************************************************/
static void	lld_interfaces_get(trx_uint64_t lld_ruleid, trx_vector_ptr_t *interfaces)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_lld_interface_t	*interface;

	result = DBselect(
			"select hi.interfaceid,hi.type,hi.main,hi.useip,hi.ip,hi.dns,hi.port,hi.bulk"
			" from interface hi,items i"
			" where hi.hostid=i.hostid"
				" and i.itemid=" TRX_FS_UI64,
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		interface = (trx_lld_interface_t *)trx_malloc(NULL, sizeof(trx_lld_interface_t));

		TRX_STR2UINT64(interface->interfaceid, row[0]);
		interface->type = (unsigned char)atoi(row[1]);
		interface->main = (unsigned char)atoi(row[2]);
		interface->useip = (unsigned char)atoi(row[3]);
		interface->ip = trx_strdup(NULL, row[4]);
		interface->dns = trx_strdup(NULL, row[5]);
		interface->port = trx_strdup(NULL, row[6]);
		interface->bulk = (unsigned char)atoi(row[7]);

		trx_vector_ptr_append(interfaces, interface);
	}
	DBfree_result(result);

	trx_vector_ptr_sort(interfaces, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_interface_make                                               *
 *                                                                            *
 ******************************************************************************/
static void	lld_interface_make(trx_vector_ptr_t *interfaces, trx_uint64_t parent_interfaceid,
		trx_uint64_t interfaceid, unsigned char type, unsigned char main, unsigned char useip, const char *ip,
		const char *dns, const char *port, unsigned char bulk)
{
	trx_lld_interface_t	*interface = NULL;
	int			i;

	for (i = 0; i < interfaces->values_num; i++)
	{
		interface = (trx_lld_interface_t *)interfaces->values[i];

		if (0 != interface->interfaceid)
			continue;

		if (interface->parent_interfaceid == parent_interfaceid)
			break;
	}

	if (i == interfaces->values_num)
	{
		/* interface which should be deleted */
		interface = (trx_lld_interface_t *)trx_malloc(NULL, sizeof(trx_lld_interface_t));

		interface->interfaceid = interfaceid;
		interface->parent_interfaceid = 0;
		interface->type = type;
		interface->main = main;
		interface->useip = 0;
		interface->ip = NULL;
		interface->dns = NULL;
		interface->port = NULL;
		interface->bulk = SNMP_BULK_ENABLED;
		interface->flags = TRX_FLAG_LLD_INTERFACE_REMOVE;

		trx_vector_ptr_append(interfaces, interface);
	}
	else
	{
		/* interface which are already added */
		if (interface->type != type)
		{
			interface->type_orig = type;
			interface->flags |= TRX_FLAG_LLD_INTERFACE_UPDATE_TYPE;
		}
		if (interface->main != main)
		{
			interface->main_orig = main;
			interface->flags |= TRX_FLAG_LLD_INTERFACE_UPDATE_MAIN;
		}
		if (interface->useip != useip)
			interface->flags |= TRX_FLAG_LLD_INTERFACE_UPDATE_USEIP;
		if (0 != strcmp(interface->ip, ip))
			interface->flags |= TRX_FLAG_LLD_INTERFACE_UPDATE_IP;
		if (0 != strcmp(interface->dns, dns))
			interface->flags |= TRX_FLAG_LLD_INTERFACE_UPDATE_DNS;
		if (0 != strcmp(interface->port, port))
			interface->flags |= TRX_FLAG_LLD_INTERFACE_UPDATE_PORT;
		if (interface->bulk != bulk)
			interface->flags |= TRX_FLAG_LLD_INTERFACE_UPDATE_BULK;
	}

	interface->interfaceid = interfaceid;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_interfaces_make                                              *
 *                                                                            *
 * Parameters: interfaces - [IN] sorted list of interfaces which              *
 *                               should be present on the each                *
 *                               discovered host                              *
 *             hosts      - [IN/OUT] sorted list of hosts                     *
 *                                                                            *
 ******************************************************************************/
static void	lld_interfaces_make(const trx_vector_ptr_t *interfaces, trx_vector_ptr_t *hosts)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			i, j;
	trx_vector_uint64_t	hostids;
	trx_uint64_t		parent_interfaceid, hostid, interfaceid;
	trx_lld_host_t		*host;
	trx_lld_interface_t	*new_interface, *interface;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&hostids);

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & TRX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		trx_vector_ptr_reserve(&host->interfaces, interfaces->values_num);
		for (j = 0; j < interfaces->values_num; j++)
		{
			interface = (trx_lld_interface_t *)interfaces->values[j];

			new_interface = (trx_lld_interface_t *)trx_malloc(NULL, sizeof(trx_lld_interface_t));

			new_interface->interfaceid = 0;
			new_interface->parent_interfaceid = interface->interfaceid;
			new_interface->type = interface->type;
			new_interface->main = interface->main;
			new_interface->useip = interface->useip;
			new_interface->ip = trx_strdup(NULL, interface->ip);
			new_interface->dns = trx_strdup(NULL, interface->dns);
			new_interface->port = trx_strdup(NULL, interface->port);
			new_interface->bulk = interface->bulk;
			new_interface->flags = 0x00;

			trx_vector_ptr_append(&host->interfaces, new_interface);
		}

		if (0 != host->hostid)
			trx_vector_uint64_append(&hostids, host->hostid);
	}

	if (0 != hostids.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select hi.hostid,id.parent_interfaceid,hi.interfaceid,hi.type,hi.main,hi.useip,hi.ip,"
					"hi.dns,hi.port,hi.bulk"
				" from interface hi"
					" left join interface_discovery id"
						" on hi.interfaceid=id.interfaceid"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hi.hostid", hostids.values, hostids.values_num);

		result = DBselect("%s", sql);

		trx_free(sql);

		while (NULL != (row = DBfetch(result)))
		{
			TRX_STR2UINT64(hostid, row[0]);
			TRX_DBROW2UINT64(parent_interfaceid, row[1]);
			TRX_DBROW2UINT64(interfaceid, row[2]);

			if (FAIL == (i = trx_vector_ptr_bsearch(hosts, &hostid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			host = (trx_lld_host_t *)hosts->values[i];

			lld_interface_make(&host->interfaces, parent_interfaceid, interfaceid,
					(unsigned char)atoi(row[3]), (unsigned char)atoi(row[4]),
					(unsigned char)atoi(row[5]), row[6], row[7], row[8],
					(unsigned char)atoi(row[9]));
		}
		DBfree_result(result);
	}

	trx_vector_uint64_destroy(&hostids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: another_main_interface_exists                                    *
 *                                                                            *
 * Return value: SUCCEED if interface with same type exists in the list of    *
 *               interfaces; FAIL - otherwise                                 *
 *                                                                            *
 * Comments: interfaces with TRX_FLAG_LLD_INTERFACE_REMOVE flag are ignored   *
 *           auxiliary function for lld_interfaces_validate()                 *
 *                                                                            *
 ******************************************************************************/
static int	another_main_interface_exists(const trx_vector_ptr_t *interfaces, const trx_lld_interface_t *interface)
{
	const trx_lld_interface_t	*interface_b;
	int				i;

	for (i = 0; i < interfaces->values_num; i++)
	{
		interface_b = (trx_lld_interface_t *)interfaces->values[i];

		if (interface_b == interface)
			continue;

		if (0 != (interface_b->flags & TRX_FLAG_LLD_INTERFACE_REMOVE))
			continue;

		if (interface_b->type != interface->type)
			continue;

		if (1 == interface_b->main)
			return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_interfaces_validate                                          *
 *                                                                            *
 * Parameters: hosts - [IN/OUT] list of hosts                                 *
 *                                                                            *
 ******************************************************************************/
static void	lld_interfaces_validate(trx_vector_ptr_t *hosts, char **error)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			i, j;
	trx_vector_uint64_t	interfaceids;
	trx_uint64_t		interfaceid;
	trx_lld_host_t		*host;
	trx_lld_interface_t	*interface;
	unsigned char		type;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* validate changed types */

	trx_vector_uint64_create(&interfaceids);

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		for (j = 0; j < host->interfaces.values_num; j++)
		{
			interface = (trx_lld_interface_t *)host->interfaces.values[j];

			if (0 == (interface->flags & TRX_FLAG_LLD_INTERFACE_UPDATE_TYPE))
				continue;

			trx_vector_uint64_append(&interfaceids, interface->interfaceid);
		}
	}

	if (0 != interfaceids.values_num)
	{
		trx_vector_uint64_sort(&interfaceids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select interfaceid,type from items where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "interfaceid",
				interfaceids.values, interfaceids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " group by interfaceid,type");

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			type = get_interface_type_by_item_type((unsigned char)atoi(row[1]));

			if (type != INTERFACE_TYPE_ANY && type != INTERFACE_TYPE_UNKNOWN)
			{
				TRX_STR2UINT64(interfaceid, row[0]);

				for (i = 0; i < hosts->values_num; i++)
				{
					host = (trx_lld_host_t *)hosts->values[i];

					for (j = 0; j < host->interfaces.values_num; j++)
					{
						interface = (trx_lld_interface_t *)host->interfaces.values[j];

						if (0 == (interface->flags & TRX_FLAG_LLD_INTERFACE_UPDATE_TYPE))
							continue;

						if (interface->interfaceid != interfaceid)
							continue;

						*error = trx_strdcatf(*error,
								"Cannot update \"%s\" interface on host \"%s\":"
								" the interface is used by items.\n",
								trx_interface_type_string(interface->type_orig),
								host->host);

						/* return an original interface type and drop the correspond flag */
						interface->type = interface->type_orig;
						interface->flags &= ~TRX_FLAG_LLD_INTERFACE_UPDATE_TYPE;
					}
				}
			}
		}
		DBfree_result(result);
	}

	/* validate interfaces which should be deleted */

	trx_vector_uint64_clear(&interfaceids);

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (trx_lld_host_t *)hosts->values[i];

		for (j = 0; j < host->interfaces.values_num; j++)
		{
			interface = (trx_lld_interface_t *)host->interfaces.values[j];

			if (0 == (interface->flags & TRX_FLAG_LLD_INTERFACE_REMOVE))
				continue;

			trx_vector_uint64_append(&interfaceids, interface->interfaceid);
		}
	}

	if (0 != interfaceids.values_num)
	{
		trx_vector_uint64_sort(&interfaceids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		sql_offset = 0;
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select interfaceid from items where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "interfaceid",
				interfaceids.values, interfaceids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " group by interfaceid");

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			TRX_STR2UINT64(interfaceid, row[0]);

			for (i = 0; i < hosts->values_num; i++)
			{
				host = (trx_lld_host_t *)hosts->values[i];

				for (j = 0; j < host->interfaces.values_num; j++)
				{
					interface = (trx_lld_interface_t *)host->interfaces.values[j];

					if (0 == (interface->flags & TRX_FLAG_LLD_INTERFACE_REMOVE))
						continue;

					if (interface->interfaceid != interfaceid)
						continue;

					*error = trx_strdcatf(*error, "Cannot delete \"%s\" interface on host \"%s\":"
							" the interface is used by items.\n",
							trx_interface_type_string(interface->type), host->host);

					/* drop the correspond flag */
					interface->flags &= ~TRX_FLAG_LLD_INTERFACE_REMOVE;

					if (SUCCEED == another_main_interface_exists(&host->interfaces, interface))
					{
						if (1 == interface->main)
						{
							/* drop main flag */
							interface->main_orig = interface->main;
							interface->main = 0;
							interface->flags |= TRX_FLAG_LLD_INTERFACE_UPDATE_MAIN;
						}
					}
					else if (1 != interface->main)
					{
						/* set main flag */
						interface->main_orig = interface->main;
						interface->main = 1;
						interface->flags |= TRX_FLAG_LLD_INTERFACE_UPDATE_MAIN;
					}
				}
			}
		}
		DBfree_result(result);
	}

	trx_vector_uint64_destroy(&interfaceids);

	trx_free(sql);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_update_hosts                                                 *
 *                                                                            *
 * Purpose: add or update low-level discovered hosts                          *
 *                                                                            *
 ******************************************************************************/
void	lld_update_hosts(trx_uint64_t lld_ruleid, const trx_vector_ptr_t *lld_rows,
		const trx_vector_ptr_t *lld_macro_paths, char **error, int lifetime, int lastcheck)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_vector_ptr_t	hosts, group_prototypes, groups, interfaces, hostmacros;
	trx_vector_uint64_t	groupids;		/* list of host groups which should be added */
	trx_vector_uint64_t	del_hostgroupids;	/* list of host groups which should be deleted */
	trx_uint64_t		proxy_hostid;
	char			*ipmi_username = NULL, *ipmi_password, *tls_issuer, *tls_subject, *tls_psk_identity,
				*tls_psk;
	char			ipmi_authtype, inventory_mode;
	unsigned char		ipmi_privilege, tls_connect, tls_accept;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select h.proxy_hostid,h.ipmi_authtype,h.ipmi_privilege,h.ipmi_username,h.ipmi_password,"
				"h.tls_connect,h.tls_accept,h.tls_issuer,h.tls_subject,h.tls_psk_identity,h.tls_psk"
			" from hosts h,items i"
			" where h.hostid=i.hostid"
				" and i.itemid=" TRX_FS_UI64,
			lld_ruleid);

	if (NULL != (row = DBfetch(result)))
	{
		TRX_DBROW2UINT64(proxy_hostid, row[0]);
		ipmi_authtype = (char)atoi(row[1]);
		TRX_STR2UCHAR(ipmi_privilege, row[2]);
		ipmi_username = trx_strdup(NULL, row[3]);
		ipmi_password = trx_strdup(NULL, row[4]);

		TRX_STR2UCHAR(tls_connect, row[5]);
		TRX_STR2UCHAR(tls_accept, row[6]);
		tls_issuer = trx_strdup(NULL, row[7]);
		tls_subject = trx_strdup(NULL, row[8]);
		tls_psk_identity = trx_strdup(NULL, row[9]);
		tls_psk = trx_strdup(NULL, row[10]);
	}
	DBfree_result(result);

	if (NULL == row)
	{
		*error = trx_strdcatf(*error, "Cannot process host prototypes: a parent host not found.\n");
		return;
	}

	trx_vector_ptr_create(&hosts);
	trx_vector_uint64_create(&groupids);
	trx_vector_ptr_create(&group_prototypes);
	trx_vector_ptr_create(&groups);
	trx_vector_uint64_create(&del_hostgroupids);
	trx_vector_ptr_create(&interfaces);
	trx_vector_ptr_create(&hostmacros);

	lld_interfaces_get(lld_ruleid, &interfaces);
	lld_hostmacros_get(lld_ruleid, &hostmacros);

	result = DBselect(
			"select h.hostid,h.host,h.name,h.status,hi.inventory_mode"
			" from hosts h,host_discovery hd"
				" left join host_inventory hi"
					" on hd.hostid=hi.hostid"
			" where h.hostid=hd.hostid"
				" and hd.parent_itemid=" TRX_FS_UI64,
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		trx_uint64_t	parent_hostid;
		const char	*host_proto, *name_proto;
		trx_lld_host_t	*host;
		unsigned char	status;
		int		i;

		TRX_STR2UINT64(parent_hostid, row[0]);
		host_proto = row[1];
		name_proto = row[2];
		status = (unsigned char)atoi(row[3]);
		if (SUCCEED == DBis_null(row[4]))
			inventory_mode = HOST_INVENTORY_DISABLED;
		else
			inventory_mode = (char)atoi(row[4]);

		lld_hosts_get(parent_hostid, &hosts, proxy_hostid, ipmi_authtype, ipmi_privilege, ipmi_username,
				ipmi_password, tls_connect, tls_accept, tls_issuer, tls_subject,
				tls_psk_identity, tls_psk);

		lld_simple_groups_get(parent_hostid, &groupids);

		lld_group_prototypes_get(parent_hostid, &group_prototypes);
		lld_groups_get(parent_hostid, &groups);

		for (i = 0; i < lld_rows->values_num; i++)
		{
			const trx_lld_row_t	*lld_row = (trx_lld_row_t *)lld_rows->values[i];

			host = lld_host_make(&hosts, host_proto, name_proto, &lld_row->jp_row, lld_macro_paths);
			lld_groups_make(host, &groups, &group_prototypes, &lld_row->jp_row, lld_macro_paths);
		}

		trx_vector_ptr_sort(&hosts, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

		lld_groups_validate(&groups, error);
		lld_hosts_validate(&hosts, error);

		lld_interfaces_make(&interfaces, &hosts);
		lld_interfaces_validate(&hosts, error);

		lld_hostgroups_make(&groupids, &hosts, &groups, &del_hostgroupids);
		lld_templates_make(parent_hostid, &hosts);

		lld_hostmacros_make(&hostmacros, &hosts);

		lld_groups_save(&groups, &group_prototypes);
		lld_hosts_save(parent_hostid, &hosts, host_proto, proxy_hostid, ipmi_authtype, ipmi_privilege,
				ipmi_username, ipmi_password, status, inventory_mode, tls_connect, tls_accept,
				tls_issuer, tls_subject, tls_psk_identity, tls_psk, &del_hostgroupids);

		/* linking of the templates */
		lld_templates_link(&hosts, error);

		lld_hosts_remove(&hosts, lifetime, lastcheck);
		lld_groups_remove(&groups, lifetime, lastcheck);

		trx_vector_ptr_clear_ext(&groups, (trx_clean_func_t)lld_group_free);
		trx_vector_ptr_clear_ext(&group_prototypes, (trx_clean_func_t)lld_group_prototype_free);
		trx_vector_ptr_clear_ext(&hosts, (trx_clean_func_t)lld_host_free);

		trx_vector_uint64_clear(&groupids);
		trx_vector_uint64_clear(&del_hostgroupids);
	}
	DBfree_result(result);

	trx_vector_ptr_clear_ext(&hostmacros, (trx_clean_func_t)lld_hostmacro_free);
	trx_vector_ptr_clear_ext(&interfaces, (trx_clean_func_t)lld_interface_free);

	trx_vector_ptr_destroy(&hostmacros);
	trx_vector_ptr_destroy(&interfaces);
	trx_vector_uint64_destroy(&del_hostgroupids);
	trx_vector_ptr_destroy(&groups);
	trx_vector_ptr_destroy(&group_prototypes);
	trx_vector_uint64_destroy(&groupids);
	trx_vector_ptr_destroy(&hosts);

	trx_free(tls_psk);
	trx_free(tls_psk_identity);
	trx_free(tls_subject);
	trx_free(tls_issuer);
	trx_free(ipmi_password);
	trx_free(ipmi_username);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
