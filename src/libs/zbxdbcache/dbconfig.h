

#ifndef TREEGIX_DBCONFIG_H
#define TREEGIX_DBCONFIG_H

#ifndef TRX_DBCONFIG_IMPL
#	error This header must be used by configuration cache implementation
#endif

typedef struct
{
	trx_uint64_t		triggerid;
	const char		*description;
	const char		*expression;
	const char		*recovery_expression;
	const char		*error;
	const char		*correlation_tag;
	const char		*opdata;
	int			lastchange;
	int			nextcheck;		/* time of next trigger recalculation,    */
							/* valid for triggers with time functions */
	unsigned char		topoindex;
	unsigned char		priority;
	unsigned char		type;
	unsigned char		value;
	unsigned char		state;
	unsigned char		locked;
	unsigned char		status;
	unsigned char		functional;		/* see TRIGGER_FUNCTIONAL_* defines      */
	unsigned char		recovery_mode;		/* see TRIGGER_RECOVERY_MODE_* defines   */
	unsigned char		correlation_mode;	/* see TRX_TRIGGER_CORRELATION_* defines */
	unsigned char		timer;			/* see TRX_TRIGGER_TIMER_* defines       */

	trx_vector_ptr_t	tags;
}
TRX_DC_TRIGGER;

typedef struct trx_dc_trigger_deplist
{
	trx_uint64_t		triggerid;
	int			refcount;
	TRX_DC_TRIGGER		*trigger;
	trx_vector_ptr_t	dependencies;
}
TRX_DC_TRIGGER_DEPLIST;

typedef struct
{
	trx_uint64_t	functionid;
	trx_uint64_t	triggerid;
	trx_uint64_t	itemid;
	const char	*function;
	const char	*parameter;
	unsigned char	timer;
}
TRX_DC_FUNCTION;

typedef struct
{
	trx_uint64_t		itemid;
	trx_uint64_t		hostid;
	trx_uint64_t		interfaceid;
	trx_uint64_t		lastlogsize;
	trx_uint64_t		valuemapid;
	const char		*key;
	const char		*port;
	const char		*error;
	const char		*delay;
	TRX_DC_TRIGGER		**triggers;
	int			nextcheck;
	int			lastclock;
	int			mtime;
	int			data_expected_from;
	int			history_sec;
	unsigned char		history;
	unsigned char		type;
	unsigned char		value_type;
	unsigned char		poller_type;
	unsigned char		state;
	unsigned char		db_state;
	unsigned char		inventory_link;
	unsigned char		location;
	unsigned char		flags;
	unsigned char		status;
	unsigned char		queue_priority;
	unsigned char		schedulable;
	unsigned char		update_triggers;
	trx_uint64_t		templateid;
	trx_uint64_t		parent_itemid; /* from joined item_discovery table */
}
TRX_DC_ITEM;

typedef struct
{
	trx_uint64_t		itemid;
	trx_uint64_t		hostid;
	trx_uint64_t		templateid;
}
TRX_DC_TEMPLATE_ITEM;

typedef struct
{
	trx_uint64_t		itemid;
	trx_uint64_t		hostid;
	trx_uint64_t		templateid;
}
TRX_DC_PROTOTYPE_ITEM;

typedef struct
{
	trx_uint64_t	hostid;
	const char	*key;
	TRX_DC_ITEM	*item_ptr;
}
TRX_DC_ITEM_HK;

typedef struct
{
	trx_uint64_t	itemid;
	const char	*units;
	unsigned char	trends;
}
TRX_DC_NUMITEM;

typedef struct
{
	trx_uint64_t	itemid;
	const char	*snmp_oid;
	const char	*snmp_community;
	const char	*snmpv3_securityname;
	const char	*snmpv3_authpassphrase;
	const char	*snmpv3_privpassphrase;
	const char	*snmpv3_contextname;
	unsigned char	snmpv3_securitylevel;
	unsigned char	snmpv3_authprotocol;
	unsigned char	snmpv3_privprotocol;
	unsigned char	snmp_oid_type;
}
TRX_DC_SNMPITEM;

typedef struct
{
	trx_uint64_t	itemid;
	const char	*ipmi_sensor;
}
TRX_DC_IPMIITEM;

typedef struct
{
	trx_uint64_t	itemid;
	const char	*trapper_hosts;
}
TRX_DC_TRAPITEM;

typedef struct
{
	trx_uint64_t	itemid;
	trx_uint64_t	master_itemid;
	trx_uint64_t	last_master_itemid;
	unsigned char	flags;
}
TRX_DC_DEPENDENTITEM;

typedef struct
{
	trx_uint64_t	itemid;
	const char	*logtimefmt;
}
TRX_DC_LOGITEM;

typedef struct
{
	trx_uint64_t	itemid;
	const char	*params;
	const char	*username;
	const char	*password;
}
TRX_DC_DBITEM;

typedef struct
{
	trx_uint64_t	itemid;
	const char	*username;
	const char	*publickey;
	const char	*privatekey;
	const char	*password;
	const char	*params;
	unsigned char	authtype;
}
TRX_DC_SSHITEM;

typedef struct
{
	trx_uint64_t	itemid;
	const char	*username;
	const char	*password;
	const char	*params;
}
TRX_DC_TELNETITEM;

typedef struct
{
	trx_uint64_t	itemid;
	const char	*username;
	const char	*password;
}
TRX_DC_SIMPLEITEM;

typedef struct
{
	trx_uint64_t	itemid;
	const char	*username;
	const char	*password;
	const char	*jmx_endpoint;
}
TRX_DC_JMXITEM;

typedef struct
{
	trx_uint64_t	itemid;
	const char	*params;
}
TRX_DC_CALCITEM;

typedef struct
{
	trx_uint64_t			itemid;
	trx_vector_uint64_pair_t	dep_itemids;
}
TRX_DC_MASTERITEM;

typedef struct
{
	trx_uint64_t		itemid;
	int			update_time;
	trx_vector_ptr_t	preproc_ops;
}
TRX_DC_PREPROCITEM;

typedef struct
{
	trx_uint64_t	itemid;
	const char	*timeout;
	const char	*url;
	const char	*query_fields;
	const char	*status_codes;
	const char	*http_proxy;
	const char	*headers;
	const char	*username;
	const char	*ssl_cert_file;
	const char	*ssl_key_file;
	const char	*ssl_key_password;
	const char	*password;
	const char	*posts;
	const char	*trapper_hosts;
	unsigned char	authtype;
	unsigned char	follow_redirects;
	unsigned char	post_type;
	unsigned char	retrieve_mode;
	unsigned char	request_method;
	unsigned char	output_format;
	unsigned char	verify_peer;
	unsigned char	verify_host;
	unsigned char	allow_traps;
}
TRX_DC_HTTPITEM;

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
typedef struct
{
	const char	*tls_psk_identity;	/* pre-shared key identity           */
	const char	*tls_psk;		/* pre-shared key value (hex-string) */
	unsigned int	refcount;		/* reference count                   */
}
TRX_DC_PSK;
#endif

typedef struct
{
	trx_uint64_t	hostid;
	trx_uint64_t	proxy_hostid;
	trx_uint64_t	items_active_normal;		/* On enabled hosts these two fields store number of enabled */
	trx_uint64_t	items_active_notsupported;	/* and supported items and enabled and not supported items.  */
	trx_uint64_t	items_disabled;			/* On "hosts" corresponding to proxies this and two fields   */
							/* above store cumulative statistics for all hosts monitored */
							/* by a particular proxy. */
							/* NOTE: On disabled hosts all items are counted as disabled. */
	trx_uint64_t	maintenanceid;

	const char	*host;
	const char	*name;
	int		maintenance_from;
	int		data_expected_from;
	int		errors_from;
	int		disable_until;
	int		snmp_errors_from;
	int		snmp_disable_until;
	int		ipmi_errors_from;
	int		ipmi_disable_until;
	int		jmx_errors_from;
	int		jmx_disable_until;

	/* item statistics per interface type */
	int		items_num;
	int		snmp_items_num;
	int		ipmi_items_num;
	int		jmx_items_num;

	/* timestamp of last availability status (available/error) field change on any interface */
	int		availability_ts;

	unsigned char	maintenance_status;
	unsigned char	maintenance_type;
	unsigned char	available;
	unsigned char	snmp_available;
	unsigned char	ipmi_available;
	unsigned char	jmx_available;
	unsigned char	status;

	/* flag to reset host availability to unknown */
	unsigned char	reset_availability;

	/* flag to force update for all items */
	unsigned char	update_items;

	/* 'tls_connect' and 'tls_accept' must be respected even if encryption support is not compiled in */
	unsigned char	tls_connect;
	unsigned char	tls_accept;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	const char	*tls_issuer;
	const char	*tls_subject;
	TRX_DC_PSK	*tls_dc_psk;
#endif
	const char	*error;
	const char	*snmp_error;
	const char	*ipmi_error;
	const char	*jmx_error;

	trx_vector_ptr_t	interfaces_v;	/* for quick finding of all host interfaces in */
						/* 'config->interfaces' hashset */
}
TRX_DC_HOST;

typedef struct
{
	trx_uint64_t	hostid;
	unsigned char	inventory_mode;
	const char	*values[HOST_INVENTORY_FIELD_COUNT];
}
TRX_DC_HOST_INVENTORY;

typedef struct
{
	const char	*host;
	TRX_DC_HOST	*host_ptr;
}
TRX_DC_HOST_H;

typedef struct
{
	trx_uint64_t	hostid;
	trx_uint64_t	hosts_monitored;	/* number of enabled hosts assigned to proxy */
	trx_uint64_t	hosts_not_monitored;	/* number of disabled hosts assigned to proxy */
	double		required_performance;
	int		proxy_config_nextcheck;
	int		proxy_data_nextcheck;
	int		proxy_tasks_nextcheck;
	int		nextcheck;
	int		lastaccess;
	int		last_cfg_error_time;	/* time when passive proxy misconfiguration error was seen */
						/* or 0 if no error */
	int		version;
	unsigned char	location;
	unsigned char	auto_compress;
	const char	*proxy_address;
	int		last_version_error_time;
}
TRX_DC_PROXY;

typedef struct
{
	trx_uint64_t	hostid;
	const char	*ipmi_username;
	const char	*ipmi_password;
	signed char	ipmi_authtype;
	unsigned char	ipmi_privilege;
}
TRX_DC_IPMIHOST;

typedef struct
{
	trx_uint64_t		hostid;
	trx_vector_uint64_t	templateids;
}
TRX_DC_HTMPL;

typedef struct
{
	trx_uint64_t	globalmacroid;
	const char	*macro;
	const char	*context;
	const char	*value;
}
TRX_DC_GMACRO;

typedef struct
{
	const char		*macro;
	trx_vector_ptr_t	gmacros;
}
TRX_DC_GMACRO_M;

typedef struct
{
	trx_uint64_t	hostmacroid;
	trx_uint64_t	hostid;
	const char	*macro;
	const char	*context;
	const char	*value;
}
TRX_DC_HMACRO;

typedef struct
{
	trx_uint64_t		hostid;
	const char		*macro;
	trx_vector_ptr_t	hmacros;
}
TRX_DC_HMACRO_HM;

typedef struct
{
	trx_uint64_t	interfaceid;
	trx_uint64_t	hostid;
	const char	*ip;
	const char	*dns;
	const char	*port;
	unsigned char	type;
	unsigned char	main;
	unsigned char	useip;
	unsigned char	bulk;
	unsigned char	max_snmp_succeed;
	unsigned char	min_snmp_fail;
}
TRX_DC_INTERFACE;

typedef struct
{
	trx_uint64_t		hostid;
	TRX_DC_INTERFACE	*interface_ptr;
	unsigned char		type;
}
TRX_DC_INTERFACE_HT;

typedef struct
{
	const char		*addr;
	trx_vector_uint64_t	interfaceids;
}
TRX_DC_INTERFACE_ADDR;

typedef struct
{
	trx_uint64_t		interfaceid;
	trx_vector_uint64_t	itemids;
}
TRX_DC_INTERFACE_ITEM;

typedef struct
{
	const char		*name;
	trx_vector_uint64_t	expressionids;
}
TRX_DC_REGEXP;

typedef struct
{
	trx_uint64_t	expressionid;
	const char	*expression;
	const char	*regexp;
	char		delimiter;
	unsigned char	type;
	unsigned char	case_sensitive;
}
TRX_DC_EXPRESSION;

typedef struct
{
	const char	*severity_name[TRIGGER_SEVERITY_COUNT];
	trx_uint64_t	discovery_groupid;
	int		default_inventory_mode;
	int		refresh_unsupported;
	unsigned char	snmptrap_logging;
	unsigned char	autoreg_tls_accept;
	const char	*db_extension;
	/* housekeeping related configuration data */
	trx_config_hk_t	hk;
}
TRX_DC_CONFIG_TABLE;

typedef struct
{
	trx_uint64_t	hosts_monitored;		/* total number of enabled hosts */
	trx_uint64_t	hosts_not_monitored;		/* total number of disabled hosts */
	trx_uint64_t	items_active_normal;		/* total number of enabled and supported items */
	trx_uint64_t	items_active_notsupported;	/* total number of enabled and not supported items */
	trx_uint64_t	items_disabled;			/* total number of disabled items */
							/* (all items of disabled host are counted as disabled) */
	trx_uint64_t	triggers_enabled_ok;		/* total number of enabled triggers with value OK */
	trx_uint64_t	triggers_enabled_problem;	/* total number of enabled triggers with value PROBLEM */
	trx_uint64_t	triggers_disabled;		/* total number of disabled triggers */
							/* (if at least one item or host involved in trigger is */
							/* disabled then trigger is counted as disabled) */
	double		required_performance;		/* required performance of server (values per second) */
	time_t		last_update;
}
TRX_DC_STATUS;

typedef struct
{
	trx_uint64_t	conditionid;
	trx_uint64_t	actionid;
	unsigned char	conditiontype;
	unsigned char	op;
	const char	*value;
	const char	*value2;
}
trx_dc_action_condition_t;

typedef struct
{
	trx_uint64_t		actionid;
	const char		*formula;
	unsigned char		eventsource;
	unsigned char		evaltype;
	unsigned char		opflags;
	trx_vector_ptr_t	conditions;
}
trx_dc_action_t;

typedef struct
{
	trx_uint64_t	triggertagid;
	trx_uint64_t	triggerid;
	const char	*tag;
	const char	*value;
}
trx_dc_trigger_tag_t;

typedef struct
{
	trx_uint64_t	hosttagid;
	trx_uint64_t	hostid;
	const char	*tag;
	const char	*value;
}
trx_dc_host_tag_t;

typedef struct
{
	trx_uint64_t		hostid;
	trx_vector_ptr_t	tags;
		/* references to trx_dc_host_tag_t records cached in config-> host_tags hashset */
}
trx_dc_host_tag_index_t;

typedef struct
{
	const char	*tag;
}
trx_dc_corr_condition_tag_t;

typedef struct
{
	const char	*tag;
	const char	*value;
	unsigned char	op;
}
trx_dc_corr_condition_tag_value_t;

typedef struct
{
	trx_uint64_t	groupid;
	unsigned char	op;
}
trx_dc_corr_condition_group_t;

typedef struct
{
	const char	*oldtag;
	const char	*newtag;
}
trx_dc_corr_condition_tag_pair_t;

typedef union
{
	trx_dc_corr_condition_tag_t		tag;
	trx_dc_corr_condition_tag_value_t	tag_value;
	trx_dc_corr_condition_group_t		group;
	trx_dc_corr_condition_tag_pair_t	tag_pair;
}
trx_dc_corr_condition_data_t;

typedef struct
{
	trx_uint64_t			corr_conditionid;
	trx_uint64_t			correlationid;
	int				type;

	trx_dc_corr_condition_data_t	data;
}
trx_dc_corr_condition_t;

typedef struct
{
	trx_uint64_t	corr_operationid;
	trx_uint64_t	correlationid;
	unsigned char	type;
}
trx_dc_corr_operation_t;

typedef struct
{
	trx_uint64_t		correlationid;
	const char		*name;
	const char		*formula;
	unsigned char		evaltype;

	trx_vector_ptr_t	conditions;
	trx_vector_ptr_t	operations;
}
trx_dc_correlation_t;

#define TRX_DC_HOSTGROUP_FLAGS_NONE		0
#define TRX_DC_HOSTGROUP_FLAGS_NESTED_GROUPIDS	1

typedef struct
{
	trx_uint64_t		groupid;
	const char		*name;

	trx_vector_uint64_t	nested_groupids;
	trx_hashset_t		hostids;
	unsigned char		flags;
}
trx_dc_hostgroup_t;

typedef struct
{
	trx_uint64_t	item_preprocid;
	trx_uint64_t	itemid;
	int		step;
	int		error_handler;
	unsigned char	type;
	const char	*params;
	const char	*error_handler_params;
}
trx_dc_preproc_op_t;

typedef struct
{
	trx_uint64_t		maintenanceid;
	unsigned char		type;
	unsigned char		tags_evaltype;
	unsigned char		state;
	int			active_since;
	int			active_until;
	int			running_since;
	int			running_until;
	trx_vector_uint64_t	groupids;
	trx_vector_uint64_t	hostids;
	trx_vector_ptr_t	tags;
	trx_vector_ptr_t	periods;
}
trx_dc_maintenance_t;

typedef struct
{
	trx_uint64_t	maintenancetagid;
	trx_uint64_t	maintenanceid;
	unsigned char	op;		/* condition operator */
	const char	*tag;
	const char	*value;
}
trx_dc_maintenance_tag_t;

typedef struct
{
	trx_uint64_t	timeperiodid;
	trx_uint64_t	maintenanceid;
	unsigned char	type;
	int		every;
	int		month;
	int		dayofweek;
	int		day;
	int		start_time;
	int		period;
	int		start_date;
}
trx_dc_maintenance_period_t;

typedef struct
{
	trx_uint64_t	triggerid;
	int		nextcheck;
}
trx_dc_timer_trigger_t;

typedef struct
{
	/* timestamp of the last host availability diff sent to sever, used only by proxies */
	int			availability_diff_ts;
	int			proxy_lastaccess_ts;
	int			sync_ts;
	int			item_sync_ts;

	/* maintenance processing management */
	unsigned char		maintenance_update;		/* flag to trigger maintenance update by timers  */
	trx_uint64_t		*maintenance_update_flags;	/* Array of flags to manage timer maintenance updates.*/
								/* Each array member contains 0/1 flag for 64 timers  */
								/* indicating if the timer must process maintenance.  */

	char			*session_token;

	trx_hashset_t		items;
	trx_hashset_t		items_hk;		/* hostid, key */
	trx_hashset_t		template_items;		/* template items selected from items table */
	trx_hashset_t		prototype_items;	/* item prototypes selected from items table */
	trx_hashset_t		numitems;
	trx_hashset_t		snmpitems;
	trx_hashset_t		ipmiitems;
	trx_hashset_t		trapitems;
	trx_hashset_t		dependentitems;
	trx_hashset_t		logitems;
	trx_hashset_t		dbitems;
	trx_hashset_t		sshitems;
	trx_hashset_t		telnetitems;
	trx_hashset_t		simpleitems;
	trx_hashset_t		jmxitems;
	trx_hashset_t		calcitems;
	trx_hashset_t		masteritems;
	trx_hashset_t		preprocitems;
	trx_hashset_t		httpitems;
	trx_hashset_t		functions;
	trx_hashset_t		triggers;
	trx_hashset_t		trigdeps;
	trx_hashset_t		hosts;
	trx_hashset_t		hosts_h;		/* for searching hosts by 'host' name */
	trx_hashset_t		hosts_p;		/* for searching proxies by 'host' name */
	trx_hashset_t		proxies;
	trx_hashset_t		host_inventories;
	trx_hashset_t		host_inventories_auto;	/* For caching of automatically populated host inventories. */
	 	 	 	 	 	 	/* Configuration syncer will read host_inventories without  */
							/* locking cache and therefore it cannot be updated by      */
							/* by history syncers when new data is received.	    */
	trx_hashset_t		ipmihosts;
	trx_hashset_t		htmpls;
	trx_hashset_t		gmacros;
	trx_hashset_t		gmacros_m;		/* macro */
	trx_hashset_t		hmacros;
	trx_hashset_t		hmacros_hm;		/* hostid, macro */
	trx_hashset_t		interfaces;
	trx_hashset_t		interfaces_ht;		/* hostid, type */
	trx_hashset_t		interface_snmpaddrs;	/* addr, interfaceids for SNMP interfaces */
	trx_hashset_t		interface_snmpitems;	/* interfaceid, itemids for SNMP trap items */
	trx_hashset_t		regexps;
	trx_hashset_t		expressions;
	trx_hashset_t		actions;
	trx_hashset_t		action_conditions;
	trx_hashset_t		trigger_tags;
	trx_hashset_t		host_tags;
	trx_hashset_t		host_tags_index;		/* host tag index by hostid */
	trx_hashset_t		correlations;
	trx_hashset_t		corr_conditions;
	trx_hashset_t		corr_operations;
	trx_hashset_t		hostgroups;
	trx_vector_ptr_t	hostgroups_name; 	/* host groups sorted by name */
	trx_hashset_t		preprocops;
	trx_hashset_t		maintenances;
	trx_hashset_t		maintenance_periods;
	trx_hashset_t		maintenance_tags;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_hashset_t		psks;			/* for keeping PSK-identity and PSK pairs and for searching */
							/* by PSK identity */
#endif
	trx_hashset_t		data_sessions;
	trx_binary_heap_t	queues[TRX_POLLER_TYPE_COUNT];
	trx_binary_heap_t	pqueue;
	trx_binary_heap_t	timer_queue;
	TRX_DC_CONFIG_TABLE	*config;
	TRX_DC_STATUS		*status;
	trx_hashset_t		strpool;
	char			autoreg_psk_identity[HOST_TLS_PSK_IDENTITY_LEN_MAX];	/* autoregistration PSK */
	char			autoreg_psk[HOST_TLS_PSK_LEN_MAX];
}
TRX_DC_CONFIG;

extern int	sync_in_progress;
extern TRX_DC_CONFIG	*config;
extern trx_rwlock_t	config_lock;

#define	RDLOCK_CACHE	if (0 == sync_in_progress) trx_rwlock_rdlock(config_lock)
#define	WRLOCK_CACHE	if (0 == sync_in_progress) trx_rwlock_wrlock(config_lock)
#define	UNLOCK_CACHE	if (0 == sync_in_progress) trx_rwlock_unlock(config_lock)

#define TRX_IPMI_DEFAULT_AUTHTYPE	-1
#define TRX_IPMI_DEFAULT_PRIVILEGE	2

/* validator function optionally used to validate macro values when expanding user macros */

/******************************************************************************
 *                                                                            *
 * Function: trx_macro_value_validator_func_t                                 *
 *                                                                            *
 * Purpose: validate macro value when expanding user macros                   *
 *                                                                            *
 * Parameters: value   - [IN] the macro value                                 *
 *                                                                            *
 * Return value: SUCCEED - the value is valid                                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
typedef int (*trx_macro_value_validator_func_t)(const char *value);

char	*trx_dc_expand_user_macros(const char *text, trx_uint64_t *hostids, int hostids_num,
		trx_macro_value_validator_func_t validator_func);

void	trx_dc_get_hostids_by_functionids(const trx_uint64_t *functionids, int functionids_num,
		trx_vector_uint64_t *hostids);

void	DCdump_configuration(void);

/* utility functions */
void	*DCfind_id(trx_hashset_t *hashset, trx_uint64_t id, size_t size, int *found);

/* string pool */
void	trx_strpool_release(const char *str);
int	DCstrpool_replace(int found, const char **curr, const char *new_str);

/* host groups */
void	dc_get_nested_hostgroupids(trx_uint64_t groupid, trx_vector_uint64_t *nested_groupids);
void	dc_hostgroup_cache_nested_groupids(trx_dc_hostgroup_t *parent_group);

/* synchronization */
typedef struct trx_dbsync trx_dbsync_t;

void	DCsync_maintenances(trx_dbsync_t *sync);
void	DCsync_maintenance_tags(trx_dbsync_t *sync);
void	DCsync_maintenance_periods(trx_dbsync_t *sync);
void	DCsync_maintenance_groups(trx_dbsync_t *sync);
void	DCsync_maintenance_hosts(trx_dbsync_t *sync);

/* maintenance support */

/* number of slots to store maintenance update flags */
#define TRX_MAINTENANCE_UPDATE_FLAGS_NUM()	\
		((CONFIG_TIMER_FORKS + sizeof(uint64_t) * 8 - 1) / (sizeof(uint64_t) * 8))


#endif
