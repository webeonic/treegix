
#ifndef TREEGIX_VMWARE_H
#define TREEGIX_VMWARE_H

#include "common.h"
#include "threads.h"

/* the vmware service state */
#define TRX_VMWARE_STATE_NEW		0x001
#define TRX_VMWARE_STATE_READY		0x002
#define TRX_VMWARE_STATE_FAILED		0x004

#define TRX_VMWARE_STATE_MASK		0x0FF

#define TRX_VMWARE_STATE_UPDATING	0x100
#define TRX_VMWARE_STATE_UPDATING_PERF	0x200
#define TRX_VMWARE_STATE_REMOVING	0x400

#define TRX_VMWARE_STATE_BUSY		(TRX_VMWARE_STATE_UPDATING | TRX_VMWARE_STATE_UPDATING_PERF \
							| TRX_VMWARE_STATE_REMOVING)

/* the vmware performance counter state */
#define TRX_VMWARE_COUNTER_NEW		0x00
#define TRX_VMWARE_COUNTER_READY	0x01
#define TRX_VMWARE_COUNTER_UPDATING	0x10

#define TRX_VMWARE_EVENT_KEY_UNINITIALIZED	__UINT64_C(0xffffffffffffffff)

typedef struct
{
	char		*name;
	trx_uint64_t	value;
}
trx_str_uint64_pair_t;

TRX_VECTOR_DECL(str_uint64_pair, trx_str_uint64_pair_t)

/* performance counter data */
typedef struct
{
	/* the counter id */
	trx_uint64_t			counterid;

	/* the counter values for various instances */
	/*    pair->name  - instance                */
	/*    pair->value - value                   */
	trx_vector_str_uint64_pair_t	values;

	/* the counter state, see TRX_VMAWRE_COUNTER_* defines */
	unsigned char			state;
}
trx_vmware_perf_counter_t;

/* an entity monitored with performance counters */
typedef struct
{
	/* entity type: HostSystem or VirtualMachine */
	char			*type;

	/* entity id */
	char			*id;

	/* the performance counter refresh rate */
	int			refresh;

	/* timestamp when the entity was queried last time */
	int			last_seen;

	/* the performance counters to monitor */
	trx_vector_ptr_t	counters;

	/* the performance counter query instance name */
	char			*query_instance;

	/* error information */
	char			*error;
}
trx_vmware_perf_entity_t;

typedef struct
{
	char			*name;
	char			*uuid;
	char			*id;
	trx_uint64_t		capacity;
	trx_uint64_t		free_space;
	trx_uint64_t		uncommitted;
	trx_vector_str_t	hv_uuids;
}
trx_vmware_datastore_t;

int	vmware_ds_name_compare(const void *d1, const void *d2);
TRX_PTR_VECTOR_DECL(vmware_datastore, trx_vmware_datastore_t *)

#define TRX_VMWARE_DEV_TYPE_NIC		1
#define TRX_VMWARE_DEV_TYPE_DISK	2
typedef struct
{
	int	type;
	char	*instance;
	char	*label;
}
trx_vmware_dev_t;

/* file system data */
typedef struct
{
	char		*path;
	trx_uint64_t	capacity;
	trx_uint64_t	free_space;
}
trx_vmware_fs_t;

/* the vmware virtual machine data */
typedef struct
{
	char			*uuid;
	char			*id;
	char			**props;
	trx_vector_ptr_t	devs;
	trx_vector_ptr_t	file_systems;
}
trx_vmware_vm_t;

/* the vmware hypervisor data */
typedef struct
{
	char			*uuid;
	char			*id;
	char			*clusterid;
	char			*datacenter_name;
	char			*parent_name;
	char			*parent_type;
	char			**props;
	trx_vector_str_t	ds_names;
	trx_vector_ptr_t	vms;
}
trx_vmware_hv_t;

/* index virtual machines by uuids */
typedef struct
{
	trx_vmware_vm_t	*vm;
	trx_vmware_hv_t	*hv;
}
trx_vmware_vm_index_t;

/* the vmware cluster data */
typedef struct
{
	char	*id;
	char	*name;
	char	*status;
}
trx_vmware_cluster_t;

/* the vmware eventlog state */
typedef struct
{
	trx_uint64_t	last_key;	/* lastlogsize when vmware.eventlog[] item was polled last time */
	unsigned char	skip_old;	/* skip old event log records */

}
trx_vmware_eventlog_state_t;

/* the vmware event data */
typedef struct
{
	trx_uint64_t	key;		/* event's key, used to fill logeventid */
	char		*message;	/* event's fullFormattedMessage */
	int		timestamp;	/* event's time stamp */
}
trx_vmware_event_t;

/* the vmware service data object */
typedef struct
{
	char	*error;

	trx_hashset_t			hvs;
	trx_hashset_t			vms_index;
	trx_vector_ptr_t		clusters;
	trx_vector_ptr_t		events;			/* vector of pointers to trx_vmware_event_t structures */
	int				max_query_metrics;	/* max count of Datastore perfCounters in one request */
	trx_vector_vmware_datastore_t	datastores;
}
trx_vmware_data_t;

/* the vmware service data */
typedef struct
{
	char				*url;
	char				*username;
	char				*password;

	/* the service type - vCenter or vSphere */
	unsigned char			type;

	/* the service state - see TRX_VMWARE_STATE_* defines */
	int				state;

	int				lastcheck;
	int				lastperfcheck;

	/* The last vmware service access time. If a service is not accessed for a day it is removed */
	int				lastaccess;

	/* the vmware service instance version */
	char				*version;

	/* the vmware service instance fullname */
	char				*fullname;

	/* the performance counters */
	trx_hashset_t			counters;

	/* list of entities to monitor with performance counters */
	trx_hashset_t			entities;

	/* the service data object that is swapped with a new one during service update */
	trx_vmware_data_t		*data;

	/* lastlogsize when vmware.eventlog[] item was polled last time and skip old flag*/
	trx_vmware_eventlog_state_t	eventlog;
}
trx_vmware_service_t;

#define TRX_VMWARE_PERF_INTERVAL_UNKNOWN	0
#define TRX_VMWARE_PERF_INTERVAL_NONE		-1

/* the vmware collector data */
typedef struct
{
	trx_vector_ptr_t	services;
	trx_hashset_t		strpool;
}
trx_vmware_t;

/* the vmware collector statistics */
typedef struct
{
	trx_uint64_t	memory_used;
	trx_uint64_t	memory_total;
}
trx_vmware_stats_t;

TRX_THREAD_ENTRY(vmware_thread, args);

int	trx_vmware_init(char **error);
void	trx_vmware_destroy(void);

void	trx_vmware_lock(void);
void	trx_vmware_unlock(void);

int	trx_vmware_get_statistics(trx_vmware_stats_t *stats);

#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)

trx_vmware_service_t	*trx_vmware_get_service(const char* url, const char* username, const char* password);

int	trx_vmware_service_get_counterid(trx_vmware_service_t *service, const char *path, trx_uint64_t *counterid);
int	trx_vmware_service_add_perf_counter(trx_vmware_service_t *service, const char *type, const char *id,
		trx_uint64_t counterid, const char *instance);
trx_vmware_perf_entity_t	*trx_vmware_service_get_perf_entity(trx_vmware_service_t *service, const char *type,
		const char *id);

/* hypervisor properties */
#define TRX_VMWARE_HVPROP_OVERALL_CPU_USAGE		0
#define TRX_VMWARE_HVPROP_FULL_NAME			1
#define TRX_VMWARE_HVPROP_HW_NUM_CPU_CORES		2
#define TRX_VMWARE_HVPROP_HW_CPU_MHZ			3
#define TRX_VMWARE_HVPROP_HW_CPU_MODEL			4
#define TRX_VMWARE_HVPROP_HW_NUM_CPU_THREADS		5
#define TRX_VMWARE_HVPROP_HW_MEMORY_SIZE		6
#define TRX_VMWARE_HVPROP_HW_MODEL			7
#define TRX_VMWARE_HVPROP_HW_UUID			8
#define TRX_VMWARE_HVPROP_HW_VENDOR			9
#define TRX_VMWARE_HVPROP_MEMORY_USED			10
#define TRX_VMWARE_HVPROP_HEALTH_STATE			11
#define TRX_VMWARE_HVPROP_UPTIME			12
#define TRX_VMWARE_HVPROP_VERSION			13
#define TRX_VMWARE_HVPROP_NAME				14
#define TRX_VMWARE_HVPROP_STATUS			15

#define TRX_VMWARE_HVPROPS_NUM				16

/* virtual machine properties */
#define TRX_VMWARE_VMPROP_CPU_NUM			0
#define TRX_VMWARE_VMPROP_CPU_USAGE			1
#define TRX_VMWARE_VMPROP_NAME				2
#define TRX_VMWARE_VMPROP_MEMORY_SIZE			3
#define TRX_VMWARE_VMPROP_MEMORY_SIZE_BALLOONED		4
#define TRX_VMWARE_VMPROP_MEMORY_SIZE_COMPRESSED	5
#define TRX_VMWARE_VMPROP_MEMORY_SIZE_SWAPPED		6
#define TRX_VMWARE_VMPROP_MEMORY_SIZE_USAGE_GUEST	7
#define TRX_VMWARE_VMPROP_MEMORY_SIZE_USAGE_HOST	8
#define TRX_VMWARE_VMPROP_MEMORY_SIZE_PRIVATE		9
#define TRX_VMWARE_VMPROP_MEMORY_SIZE_SHARED		10
#define TRX_VMWARE_VMPROP_POWER_STATE			11
#define TRX_VMWARE_VMPROP_STORAGE_COMMITED		12
#define TRX_VMWARE_VMPROP_STORAGE_UNSHARED		13
#define TRX_VMWARE_VMPROP_STORAGE_UNCOMMITTED		14
#define TRX_VMWARE_VMPROP_UPTIME			15

#define TRX_VMWARE_VMPROPS_NUM				16

/* vmware service types */
#define TRX_VMWARE_TYPE_UNKNOWN	0
#define TRX_VMWARE_TYPE_VSPHERE	1
#define TRX_VMWARE_TYPE_VCENTER	2

#define TRX_VMWARE_SOAP_DATACENTER	"Datacenter"
#define TRX_VMWARE_SOAP_FOLDER		"Folder"
#define TRX_VMWARE_SOAP_CLUSTER		"ClusterComputeResource"
#define TRX_VMWARE_SOAP_DEFAULT		"VMware"

#endif	/* defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL) */

#endif	/* TREEGIX_VMWARE_H */
