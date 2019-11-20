

#include "common.h"

/* LIBXML2 is used */
#ifdef HAVE_LIBXML2
#	include <libxml/parser.h>
#	include <libxml/tree.h>
#	include <libxml/xpath.h>
#endif

#include "ipc.h"
#include "memalloc.h"
#include "log.h"
#include "trxalgo.h"
#include "daemon.h"
#include "trxself.h"

#include "vmware.h"
#include "../../libs/trxalgo/vectorimpl.h"

/*
 * The VMware data (trx_vmware_service_t structure) are stored in shared memory.
 * This data can be accessed with trx_vmware_get_service() function and is regularly
 * updated by VMware collector processes.
 *
 * When a new service is requested by poller the trx_vmware_get_service() function
 * creates a new service object, marks it as new, but still returns NULL object.
 *
 * The collectors check the service object list for new services or services not updated
 * during last CONFIG_VMWARE_FREQUENCY seconds. If such service is found it is marked
 * as updating.
 *
 * The service object is updated by creating a new data object, initializing it
 * with the latest data from VMware vCenter (or Hypervisor), destroying the old data
 * object and replacing it with the new one.
 *
 * The collector must be locked only when accessing service object list and working with
 * a service object. It is not locked for new data object creation during service update,
 * which is the most time consuming task.
 *
 * As the data retrieved by VMware collector can be quite big (for example 1 Hypervisor
 * with 500 Virtual Machines will result in approximately 20 MB of data), VMware collector
 * updates performance data (which is only 10% of the structure data) separately
 * with CONFIG_VMWARE_PERF_FREQUENCY period. The performance data is stored directly
 * in VMware service object entities vector - so the structure data is not affected by
 * performance data updates.
 */

extern int		CONFIG_VMWARE_FREQUENCY;
extern int		CONFIG_VMWARE_PERF_FREQUENCY;
extern trx_uint64_t	CONFIG_VMWARE_CACHE_SIZE;
extern int		CONFIG_VMWARE_TIMEOUT;

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;
extern char		*CONFIG_SOURCE_IP;

#define VMWARE_VECTOR_CREATE(ref, type)	trx_vector_##type##_create_ext(ref,  __vm_mem_malloc_func, \
		__vm_mem_realloc_func, __vm_mem_free_func)

#define TRX_VMWARE_CACHE_UPDATE_PERIOD	CONFIG_VMWARE_FREQUENCY
#define TRX_VMWARE_PERF_UPDATE_PERIOD	CONFIG_VMWARE_PERF_FREQUENCY
#define TRX_VMWARE_SERVICE_TTL		SEC_PER_HOUR
#define TRX_XML_DATETIME		26
#define TRX_INIT_UPD_XML_SIZE		(100 * TRX_KIBIBYTE)
#define trx_xml_free_doc(xdoc)		if (NULL != xdoc)\
						xmlFreeDoc(xdoc)
#define TRX_VMWARE_DS_REFRESH_VERSION	6

static trx_mutex_t	vmware_lock = TRX_MUTEX_NULL;

static trx_mem_info_t	*vmware_mem = NULL;

TRX_MEM_FUNC_IMPL(__vm, vmware_mem)

static trx_vmware_t	*vmware = NULL;

#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)

/* according to libxml2 changelog XML_PARSE_HUGE option was introduced in version 2.7.0 */
#if 20700 <= LIBXML_VERSION	/* version 2.7.0 */
#	define TRX_XML_PARSE_OPTS	XML_PARSE_HUGE
#else
#	define TRX_XML_PARSE_OPTS	0
#endif

#define TRX_VMWARE_COUNTERS_INIT_SIZE	500

#define TRX_VPXD_STATS_MAXQUERYMETRICS	64
#define TRX_MAXQUERYMETRICS_UNLIMITED	1000

TRX_VECTOR_IMPL(str_uint64_pair, trx_str_uint64_pair_t)
TRX_PTR_VECTOR_IMPL(vmware_datastore, trx_vmware_datastore_t *)

/* VMware service object name mapping for vcenter and vsphere installations */
typedef struct
{
	const char	*performance_manager;
	const char	*session_manager;
	const char	*event_manager;
	const char	*property_collector;
	const char	*root_folder;
}
trx_vmware_service_objects_t;

static trx_vmware_service_objects_t	vmware_service_objects[3] =
{
	{NULL, NULL, NULL, NULL, NULL},
	{"ha-perfmgr", "ha-sessionmgr", "ha-eventmgr", "ha-property-collector", "ha-folder-root"},
	{"PerfMgr", "SessionManager", "EventManager", "propertyCollector", "group-d1"}
};

/* mapping of performance counter group/key[rollup type] to its id (net/transmitted[average] -> <id>) */
typedef struct
{
	char		*path;
	trx_uint64_t	id;
}
trx_vmware_counter_t;

/* performance counter value for a specific instance */
typedef struct
{
	trx_uint64_t	counterid;
	char		*instance;
	trx_uint64_t	value;
}
trx_vmware_perf_value_t;

/* performance data for a performance collector entity */
typedef struct
{
	/* entity type: HostSystem, Datastore or VirtualMachine */
	char			*type;

	/* entity id */
	char			*id;

	/* the performance counter values (see trx_vmware_perfvalue_t) */
	trx_vector_ptr_t	values;

	/* error information */
	char			*error;
}
trx_vmware_perf_data_t;

typedef struct
{
	trx_uint64_t	id;
	xmlNode		*xml_node;
}
trx_id_xmlnode_t;

TRX_VECTOR_DECL(id_xmlnode, trx_id_xmlnode_t)
TRX_VECTOR_IMPL(id_xmlnode, trx_id_xmlnode_t)

/*
 * SOAP support
 */
#define	TRX_XML_HEADER1		"Soapaction:urn:vim25/4.1"
#define TRX_XML_HEADER2		"Content-Type:text/xml; charset=utf-8"
/* cURL specific attribute to prevent the use of "Expect" directive */
/* according to RFC 7231/5.1.1 if xml request is larger than 1k */
#define TRX_XML_HEADER3		"Expect:"

#define TRX_POST_VSPHERE_HEADER									\
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"					\
		"<SOAP-ENV:Envelope"								\
			" xmlns:ns0=\"urn:vim25\""						\
			" xmlns:ns1=\"http://schemas.xmlsoap.org/soap/envelope/\""		\
			" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""		\
			" xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\">"	\
			"<SOAP-ENV:Header/>"							\
			"<ns1:Body>"
#define TRX_POST_VSPHERE_FOOTER									\
			"</ns1:Body>"								\
		"</SOAP-ENV:Envelope>"

#define TRX_XPATH_FAULTSTRING()										\
	"/*/*/*[local-name()='Fault']/*[local-name()='faultstring']"

#define TRX_XPATH_REFRESHRATE()										\
	"/*/*/*/*/*[local-name()='refreshRate' and ../*[local-name()='currentSupported']='true']"

#define TRX_XPATH_ISAGGREGATE()										\
	"/*/*/*/*/*[local-name()='entity'][../*[local-name()='summarySupported']='true' and "		\
	"../*[local-name()='currentSupported']='false']"

#define TRX_XPATH_COUNTERINFO()										\
	"/*/*/*/*/*/*[local-name()='propSet']/*[local-name()='val']/*[local-name()='PerfCounterInfo']"

#define TRX_XPATH_DATASTORE_MOUNT()									\
	"/*/*/*/*/*/*[local-name()='propSet']/*/*[local-name()='DatastoreHostMount']"			\
	"/*[local-name()='mountInfo']/*[local-name()='path']"

#define TRX_XPATH_HV_DATASTORES()									\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='datastore']]"		\
	"/*[local-name()='val']/*[@type='Datastore']"

#define TRX_XPATH_HV_VMS()										\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='vm']]"			\
	"/*[local-name()='val']/*[@type='VirtualMachine']"

#define TRX_XPATH_DATASTORE_SUMMARY(property)								\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='summary']]"		\
		"/*[local-name()='val']/*[local-name()='" property "']"

#define TRX_XPATH_MAXQUERYMETRICS()									\
	"/*/*/*/*[*[local-name()='key']='config.vpxd.stats.maxQueryMetrics']/*[local-name()='value']"

#define TRX_XPATH_VM_HARDWARE(property)									\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='config.hardware']]"	\
		"/*[local-name()='val']/*[local-name()='" property "']"

#define TRX_XPATH_VM_GUESTDISKS()									\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='guest.disk']]"		\
	"/*/*[local-name()='GuestDiskInfo']"

#define TRX_XPATH_VM_UUID()										\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='config.uuid']]"		\
		"/*[local-name()='val']"

#define TRX_XPATH_VM_INSTANCE_UUID()									\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='config.instanceUuid']]"	\
		"/*[local-name()='val']"

#define TRX_XPATH_HV_SENSOR_STATUS(sensor)								\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name']"					\
		"[text()='runtime.healthSystemRuntime.systemHealthInfo']]"				\
		"/*[local-name()='val']/*[local-name()='numericSensorInfo']"				\
		"[*[local-name()='name'][text()='" sensor "']]"						\
		"/*[local-name()='healthState']/*[local-name()='key']"

#define TRX_XPATH_VMWARE_ABOUT(property)								\
	"/*/*/*/*/*[local-name()='about']/*[local-name()='" property "']"

#	define TRX_XPATH_NN(NN)			"*[local-name()='" NN "']"
#	define TRX_XPATH_LN(LN)			"/" TRX_XPATH_NN(LN)
#	define TRX_XPATH_LN1(LN1)		"/" TRX_XPATH_LN(LN1)
#	define TRX_XPATH_LN2(LN1, LN2)		"/" TRX_XPATH_LN(LN1) TRX_XPATH_LN(LN2)
#	define TRX_XPATH_LN3(LN1, LN2, LN3)	"/" TRX_XPATH_LN(LN1) TRX_XPATH_LN(LN2) TRX_XPATH_LN(LN3)

#define TRX_XPATH_PROP_NAME(property)									\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='" property "']]"		\
		"/*[local-name()='val']"

#define TRX_VM_NONAME_XML	"noname.xml"

#define TRX_PROPMAP(property)		{property, TRX_XPATH_PROP_NAME(property)}

typedef struct
{
	const char	*name;
	const char	*xpath;
}
trx_vmware_propmap_t;

static trx_vmware_propmap_t	hv_propmap[] = {
	TRX_PROPMAP("summary.quickStats.overallCpuUsage"),	/* TRX_VMWARE_HVPROP_OVERALL_CPU_USAGE */
	TRX_PROPMAP("summary.config.product.fullName"),		/* TRX_VMWARE_HVPROP_FULL_NAME */
	TRX_PROPMAP("summary.hardware.numCpuCores"),		/* TRX_VMWARE_HVPROP_HW_NUM_CPU_CORES */
	TRX_PROPMAP("summary.hardware.cpuMhz"),			/* TRX_VMWARE_HVPROP_HW_CPU_MHZ */
	TRX_PROPMAP("summary.hardware.cpuModel"),		/* TRX_VMWARE_HVPROP_HW_CPU_MODEL */
	TRX_PROPMAP("summary.hardware.numCpuThreads"), 		/* TRX_VMWARE_HVPROP_HW_NUM_CPU_THREADS */
	TRX_PROPMAP("summary.hardware.memorySize"), 		/* TRX_VMWARE_HVPROP_HW_MEMORY_SIZE */
	TRX_PROPMAP("summary.hardware.model"), 			/* TRX_VMWARE_HVPROP_HW_MODEL */
	TRX_PROPMAP("summary.hardware.uuid"), 			/* TRX_VMWARE_HVPROP_HW_UUID */
	TRX_PROPMAP("summary.hardware.vendor"), 		/* TRX_VMWARE_HVPROP_HW_VENDOR */
	TRX_PROPMAP("summary.quickStats.overallMemoryUsage"),	/* TRX_VMWARE_HVPROP_MEMORY_USED */
	{"runtime.healthSystemRuntime.systemHealthInfo", 	/* TRX_VMWARE_HVPROP_HEALTH_STATE */
			TRX_XPATH_HV_SENSOR_STATUS("VMware Rollup Health State")},
	TRX_PROPMAP("summary.quickStats.uptime"),		/* TRX_VMWARE_HVPROP_UPTIME */
	TRX_PROPMAP("summary.config.product.version"),		/* TRX_VMWARE_HVPROP_VERSION */
	TRX_PROPMAP("summary.config.name"),			/* TRX_VMWARE_HVPROP_NAME */
	TRX_PROPMAP("overallStatus")				/* TRX_VMWARE_HVPROP_STATUS */
};

static trx_vmware_propmap_t	vm_propmap[] = {
	TRX_PROPMAP("summary.config.numCpu"),			/* TRX_VMWARE_VMPROP_CPU_NUM */
	TRX_PROPMAP("summary.quickStats.overallCpuUsage"),	/* TRX_VMWARE_VMPROP_CPU_USAGE */
	TRX_PROPMAP("summary.config.name"),			/* TRX_VMWARE_VMPROP_NAME */
	TRX_PROPMAP("summary.config.memorySizeMB"),		/* TRX_VMWARE_VMPROP_MEMORY_SIZE */
	TRX_PROPMAP("summary.quickStats.balloonedMemory"),	/* TRX_VMWARE_VMPROP_MEMORY_SIZE_BALLOONED */
	TRX_PROPMAP("summary.quickStats.compressedMemory"),	/* TRX_VMWARE_VMPROP_MEMORY_SIZE_COMPRESSED */
	TRX_PROPMAP("summary.quickStats.swappedMemory"),	/* TRX_VMWARE_VMPROP_MEMORY_SIZE_SWAPPED */
	TRX_PROPMAP("summary.quickStats.guestMemoryUsage"),	/* TRX_VMWARE_VMPROP_MEMORY_SIZE_USAGE_GUEST */
	TRX_PROPMAP("summary.quickStats.hostMemoryUsage"),	/* TRX_VMWARE_VMPROP_MEMORY_SIZE_USAGE_HOST */
	TRX_PROPMAP("summary.quickStats.privateMemory"),	/* TRX_VMWARE_VMPROP_MEMORY_SIZE_PRIVATE */
	TRX_PROPMAP("summary.quickStats.sharedMemory"),		/* TRX_VMWARE_VMPROP_MEMORY_SIZE_SHARED */
	TRX_PROPMAP("summary.runtime.powerState"),		/* TRX_VMWARE_VMPROP_POWER_STATE */
	TRX_PROPMAP("summary.storage.committed"),		/* TRX_VMWARE_VMPROP_STORAGE_COMMITED */
	TRX_PROPMAP("summary.storage.unshared"),		/* TRX_VMWARE_VMPROP_STORAGE_UNSHARED */
	TRX_PROPMAP("summary.storage.uncommitted"),		/* TRX_VMWARE_VMPROP_STORAGE_UNCOMMITTED */
	TRX_PROPMAP("summary.quickStats.uptimeSeconds")		/* TRX_VMWARE_VMPROP_UPTIME */
};

/* hypervisor hashset support */
static trx_hash_t	vmware_hv_hash(const void *data)
{
	trx_vmware_hv_t	*hv = (trx_vmware_hv_t *)data;

	return TRX_DEFAULT_STRING_HASH_ALGO(hv->uuid, strlen(hv->uuid), TRX_DEFAULT_HASH_SEED);
}

static int	vmware_hv_compare(const void *d1, const void *d2)
{
	trx_vmware_hv_t	*hv1 = (trx_vmware_hv_t *)d1;
	trx_vmware_hv_t	*hv2 = (trx_vmware_hv_t *)d2;

	return strcmp(hv1->uuid, hv2->uuid);
}

/* virtual machine index support */
static trx_hash_t	vmware_vm_hash(const void *data)
{
	trx_vmware_vm_index_t	*vmi = (trx_vmware_vm_index_t *)data;

	return TRX_DEFAULT_STRING_HASH_ALGO(vmi->vm->uuid, strlen(vmi->vm->uuid), TRX_DEFAULT_HASH_SEED);
}

static int	vmware_vm_compare(const void *d1, const void *d2)
{
	trx_vmware_vm_index_t	*vmi1 = (trx_vmware_vm_index_t *)d1;
	trx_vmware_vm_index_t	*vmi2 = (trx_vmware_vm_index_t *)d2;

	return strcmp(vmi1->vm->uuid, vmi2->vm->uuid);
}

/* string pool support */

#define REFCOUNT_FIELD_SIZE	sizeof(trx_uint32_t)

static trx_hash_t	vmware_strpool_hash_func(const void *data)
{
	return TRX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
}

static int	vmware_strpool_compare_func(const void *d1, const void *d2)
{
	return strcmp((char *)d1 + REFCOUNT_FIELD_SIZE, (char *)d2 + REFCOUNT_FIELD_SIZE);
}

static char	*vmware_shared_strdup(const char *str)
{
	void	*ptr;

	if (NULL == str)
		return NULL;

	ptr = trx_hashset_search(&vmware->strpool, str - REFCOUNT_FIELD_SIZE);

	if (NULL == ptr)
	{
		ptr = trx_hashset_insert_ext(&vmware->strpool, str - REFCOUNT_FIELD_SIZE,
				REFCOUNT_FIELD_SIZE + strlen(str) + 1, REFCOUNT_FIELD_SIZE);

		*(trx_uint32_t *)ptr = 0;
	}

	(*(trx_uint32_t *)ptr)++;

	return (char *)ptr + REFCOUNT_FIELD_SIZE;
}

static void	vmware_shared_strfree(char *str)
{
	if (NULL != str)
	{
		void	*ptr = str - REFCOUNT_FIELD_SIZE;

		if (0 == --(*(trx_uint32_t *)ptr))
			trx_hashset_remove_direct(&vmware->strpool, ptr);
	}
}

#define TRX_XPATH_NAME_BY_TYPE(type)									\
	"/*/*/*/*/*[local-name()='objects'][*[local-name()='obj'][@type='" type "']]"			\
	"/*[local-name()='propSet'][*[local-name()='name']]/*[local-name()='val']"

#define TRX_XPATH_HV_PARENTFOLDERNAME(parent_id)							\
	"/*/*/*/*/*[local-name()='objects']["								\
		"*[local-name()='obj'][@type='Folder'] and "						\
		"*[local-name()='propSet'][*[local-name()='name'][text()='childEntity']]"		\
		"/*[local-name()='val']/*[local-name()='ManagedObjectReference']=" parent_id " and "	\
		"*[local-name()='propSet'][*[local-name()='name'][text()='parent']]"			\
		"/*[local-name()='val'][@type!='Datacenter']"						\
	"]/*[local-name()='propSet'][*[local-name()='name'][text()='name']]/*[local-name()='val']"

#define TRX_XPATH_HV_PARENTID										\
	"/*/*/*/*/*[local-name()='objects'][*[local-name()='obj'][@type='HostSystem']]"			\
	"/*[local-name()='propSet'][*[local-name()='name'][text()='parent']]/*[local-name()='val']"

typedef struct
{
	char	*data;
	size_t	alloc;
	size_t	offset;
}
TRX_HTTPPAGE;

static int	trx_xml_read_values(xmlDoc *xdoc, const char *xpath, trx_vector_str_t *values);
static int	trx_xml_try_read_value(const char *data, size_t len, const char *xpath, xmlDoc **xdoc, char **value,
		char **error);
static char	*trx_xml_read_node_value(xmlDoc *doc, xmlNode *node, const char *xpath);
static char	*trx_xml_read_doc_value(xmlDoc *xdoc, const char *xpath);

static size_t	curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t		r_size = size * nmemb;
	TRX_HTTPPAGE	*page_http = (TRX_HTTPPAGE *)userdata;

	trx_strncpy_alloc(&page_http->data, &page_http->alloc, &page_http->offset, (const char *)ptr, r_size);

	return r_size;
}

static size_t	curl_header_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	TRX_UNUSED(ptr);
	TRX_UNUSED(userdata);

	return size * nmemb;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_http_post                                                    *
 *                                                                            *
 * Purpose: abstracts the curl_easy_setopt/curl_easy_perform call pair        *
 *                                                                            *
 * Parameters: easyhandle - [IN] the CURL handle                              *
 *             request    - [IN] the http request                             *
 *             response   - [OUT] the http response                           *
 *             error      - [OUT] the error message in the case of failure    *
 *                                                                            *
 * Return value: SUCCEED - the http request was completed successfully        *
 *               FAIL    - the http request has failed                        *
 ******************************************************************************/
static int	trx_http_post(CURL *easyhandle, const char *request, TRX_HTTPPAGE **response, char **error)
{
	CURLoption	opt;
	CURLcode	err;
	TRX_HTTPPAGE	*resp;

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_POSTFIELDS, request)))
	{
		if (NULL != error)
			*error = trx_dsprintf(*error, "Cannot set cURL option %d: %s.", (int)opt, curl_easy_strerror(err));

		return FAIL;
	}

	if (CURLE_OK != (err = curl_easy_getinfo(easyhandle, CURLINFO_PRIVATE, (char **)&resp)))
	{
		if (NULL != error)
			*error = trx_dsprintf(*error, "Cannot get response buffer: %s.", curl_easy_strerror(err));

		return FAIL;
	}

	resp->offset = 0;

	if (CURLE_OK != (err = curl_easy_perform(easyhandle)))
	{
		if (NULL != error)
			*error = trx_strdup(*error, curl_easy_strerror(err));

		return FAIL;
	}

	*response = resp;

	return SUCCEED;
}
/******************************************************************************
 *                                                                            *
 * Function: trx_soap_post                                                    *
 *                                                                            *
 * Purpose: unification of vmware web service call with SOAP error validation *
 *                                                                            *
 * Parameters: fn_parent  - [IN] the parent function name for Log records     *
 *             easyhandle - [IN] the CURL handle                              *
 *             request    - [IN] the http request                             *
 *             xdoc       - [OUT] the xml document response (optional)        *
 *             error      - [OUT] the error message in the case of failure    *
 *                                (optional)                                  *
 *                                                                            *
 * Return value: SUCCEED - the SOAP request was completed successfully        *
 *               FAIL    - the SOAP request has failed                        *
 ******************************************************************************/
static int	trx_soap_post(const char *fn_parent, CURL *easyhandle, const char *request, xmlDoc **xdoc, char **error)
{
	xmlDoc		*doc;
	TRX_HTTPPAGE	*resp;
	int		ret = SUCCEED;

	if (SUCCEED != trx_http_post(easyhandle, request, &resp, error))
		return FAIL;

	if (NULL != fn_parent)
		treegix_log(LOG_LEVEL_TRACE, "%s() SOAP response: %s", fn_parent, resp->data);

	if (SUCCEED != trx_xml_try_read_value(resp->data, resp->offset, TRX_XPATH_FAULTSTRING(), &doc, error, error)
			|| NULL != *error)
	{
		ret = FAIL;
	}

	if (NULL != xdoc)
	{
		*xdoc = doc;
	}
	else
	{
		trx_xml_free_doc(doc);
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * performance counter hashset support functions                              *
 *                                                                            *
 ******************************************************************************/
static trx_hash_t	vmware_counter_hash_func(const void *data)
{
	trx_vmware_counter_t	*counter = (trx_vmware_counter_t *)data;

	return TRX_DEFAULT_STRING_HASH_ALGO(counter->path, strlen(counter->path), TRX_DEFAULT_HASH_SEED);
}

static int	vmware_counter_compare_func(const void *d1, const void *d2)
{
	trx_vmware_counter_t	*c1 = (trx_vmware_counter_t *)d1;
	trx_vmware_counter_t	*c2 = (trx_vmware_counter_t *)d2;

	return strcmp(c1->path, c2->path);
}

/******************************************************************************
 *                                                                            *
 * performance entities hashset support functions                             *
 *                                                                            *
 ******************************************************************************/
static trx_hash_t	vmware_perf_entity_hash_func(const void *data)
{
	trx_hash_t	seed;

	trx_vmware_perf_entity_t	*entity = (trx_vmware_perf_entity_t *)data;

	seed = TRX_DEFAULT_STRING_HASH_ALGO(entity->type, strlen(entity->type), TRX_DEFAULT_HASH_SEED);

	return TRX_DEFAULT_STRING_HASH_ALGO(entity->id, strlen(entity->id), seed);
}

static int	vmware_perf_entity_compare_func(const void *d1, const void *d2)
{
	int	ret;

	trx_vmware_perf_entity_t	*e1 = (trx_vmware_perf_entity_t *)d1;
	trx_vmware_perf_entity_t	*e2 = (trx_vmware_perf_entity_t *)d2;

	if (0 == (ret = strcmp(e1->type, e2->type)))
		ret = strcmp(e1->id, e2->id);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_free_perfvalue                                            *
 *                                                                            *
 * Purpose: frees perfvalue data structure                                    *
 *                                                                            *
 ******************************************************************************/
static void	vmware_free_perfvalue(trx_vmware_perf_value_t *value)
{
	trx_free(value->instance);
	trx_free(value);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_free_perfdata                                             *
 *                                                                            *
 * Purpose: frees perfdata data structure                                     *
 *                                                                            *
 ******************************************************************************/
static void	vmware_free_perfdata(trx_vmware_perf_data_t *data)
{
	trx_free(data->id);
	trx_free(data->type);
	trx_free(data->error);
	trx_vector_ptr_clear_ext(&data->values, (trx_mem_free_func_t)vmware_free_perfvalue);
	trx_vector_ptr_destroy(&data->values);

	trx_free(data);
}

/******************************************************************************
 *                                                                            *
 * Function: xml_read_props                                                   *
 *                                                                            *
 * Purpose: reads the vmware object properties by their xpaths from xml data  *
 *                                                                            *
 * Parameters: xdoc      - [IN] the xml document                              *
 *             propmap   - [IN] the xpaths of the properties to read          *
 *             props_num - [IN] the number of properties to read              *
 *                                                                            *
 * Return value: an array of property values                                  *
 *                                                                            *
 * Comments: The array with property values must be freed by the caller.      *
 *                                                                            *
 ******************************************************************************/
static char	**xml_read_props(xmlDoc *xdoc, const trx_vmware_propmap_t *propmap, int props_num)
{
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	xmlChar		*val;
	char		**props;
	int		i;

	props = (char **)trx_malloc(NULL, sizeof(char *) * props_num);
	memset(props, 0, sizeof(char *) * props_num);

	for (i = 0; i < props_num; i++)
	{
		xpathCtx = xmlXPathNewContext(xdoc);

		if (NULL != (xpathObj = xmlXPathEvalExpression((const xmlChar *)propmap[i].xpath, xpathCtx)))
		{
			if (0 == xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
			{
				nodeset = xpathObj->nodesetval;

				if (NULL != (val = xmlNodeListGetString(xdoc, nodeset->nodeTab[0]->xmlChildrenNode, 1)))
				{
					props[i] = trx_strdup(NULL, (const char *)val);
					xmlFree(val);
				}
			}

			xmlXPathFreeObject(xpathObj);
		}

		xmlXPathFreeContext(xpathCtx);
	}

	return props;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_counters_shared_copy                                      *
 *                                                                            *
 * Purpose: copies performance counter vector into shared memory hashset      *
 *                                                                            *
 * Parameters: dst - [IN] the destination hashset                             *
 *             src - [IN] the source vector                                   *
 *                                                                            *
 ******************************************************************************/
static void	vmware_counters_shared_copy(trx_hashset_t *dst, const trx_vector_ptr_t *src)
{
	int			i;
	trx_vmware_counter_t	*csrc, *cdst;

	if (SUCCEED != trx_hashset_reserve(dst, src->values_num))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < src->values_num; i++)
	{
		csrc = (trx_vmware_counter_t *)src->values[i];

		cdst = (trx_vmware_counter_t *)trx_hashset_insert(dst, csrc, sizeof(trx_vmware_counter_t));

		/* check if the counter was inserted - copy path only for inserted counters */
		if (cdst->path == csrc->path)
			cdst->path = vmware_shared_strdup(csrc->path);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_vector_str_uint64_pair_shared_clean                       *
 *                                                                            *
 * Purpose: frees shared resources allocated to store instance performance    *
 *          counter values                                                    *
 *                                                                            *
 * Parameters: pairs - [IN] vector of performance counter pairs               *
 *                                                                            *
 ******************************************************************************/
static void	vmware_vector_str_uint64_pair_shared_clean(trx_vector_str_uint64_pair_t *pairs)
{
	int	i;

	for (i = 0; i < pairs->values_num; i++)
	{
		trx_str_uint64_pair_t	*pair = &pairs->values[i];

		if (NULL != pair->name)
			vmware_shared_strfree(pair->name);
	}

	pairs->values_num = 0;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_perf_counter_shared_free                                  *
 *                                                                            *
 * Purpose: frees shared resources allocated to store performance counter     *
 *          data                                                              *
 *                                                                            *
 * Parameters: counter - [IN] the performance counter data                    *
 *                                                                            *
 ******************************************************************************/
static void	vmware_perf_counter_shared_free(trx_vmware_perf_counter_t *counter)
{
	vmware_vector_str_uint64_pair_shared_clean(&counter->values);
	trx_vector_str_uint64_pair_destroy(&counter->values);
	__vm_mem_free_func(counter);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_entities_shared_clean_stats                               *
 *                                                                            *
 * Purpose: removes statistics data from vmware entities                      *
 *                                                                            *
 ******************************************************************************/
static void	vmware_entities_shared_clean_stats(trx_hashset_t *entities)
{
	int				i;
	trx_vmware_perf_entity_t	*entity;
	trx_vmware_perf_counter_t	*counter;
	trx_hashset_iter_t		iter;


	trx_hashset_iter_reset(entities, &iter);
	while (NULL != (entity = (trx_vmware_perf_entity_t *)trx_hashset_iter_next(&iter)))
	{
		for (i = 0; i < entity->counters.values_num; i++)
		{
			counter = (trx_vmware_perf_counter_t *)entity->counters.values[i];
			vmware_vector_str_uint64_pair_shared_clean(&counter->values);

			if (0 != (counter->state & TRX_VMWARE_COUNTER_UPDATING))
				counter->state = TRX_VMWARE_COUNTER_READY;
		}
		vmware_shared_strfree(entity->error);
		entity->error = NULL;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_datastore_shared_free                                     *
 *                                                                            *
 * Purpose: frees shared resources allocated to store datastore data          *
 *                                                                            *
 * Parameters: datastore   - [IN] the datastore                               *
 *                                                                            *
 ******************************************************************************/
static void	vmware_datastore_shared_free(trx_vmware_datastore_t *datastore)
{
	vmware_shared_strfree(datastore->name);
	vmware_shared_strfree(datastore->id);

	if (NULL != datastore->uuid)
		vmware_shared_strfree(datastore->uuid);

	trx_vector_str_clear_ext(&datastore->hv_uuids, vmware_shared_strfree);
	trx_vector_str_destroy(&datastore->hv_uuids);

	__vm_mem_free_func(datastore);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_props_shared_free                                         *
 *                                                                            *
 * Purpose: frees shared resources allocated to store properties list         *
 *                                                                            *
 * Parameters: props     - [IN] the properties list                           *
 *             props_num - [IN] the number of properties in the list          *
 *                                                                            *
 ******************************************************************************/
static void	vmware_props_shared_free(char **props, int props_num)
{
	int	i;

	if (NULL == props)
		return;

	for (i = 0; i < props_num; i++)
	{
		if (NULL != props[i])
			vmware_shared_strfree(props[i]);
	}

	__vm_mem_free_func(props);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_dev_shared_free                                           *
 *                                                                            *
 * Purpose: frees shared resources allocated to store vm device data          *
 *                                                                            *
 * Parameters: dev   - [IN] the vm device                                     *
 *                                                                            *
 ******************************************************************************/
static void	vmware_dev_shared_free(trx_vmware_dev_t *dev)
{
	if (NULL != dev->instance)
		vmware_shared_strfree(dev->instance);

	if (NULL != dev->label)
		vmware_shared_strfree(dev->label);

	__vm_mem_free_func(dev);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_fs_shared_free                                            *
 *                                                                            *
 * Purpose: frees shared resources allocated to store file system object      *
 *                                                                            *
 * Parameters: fs   - [IN] the file system                                    *
 *                                                                            *
 ******************************************************************************/
static void	vmware_fs_shared_free(trx_vmware_fs_t *fs)
{
	if (NULL != fs->path)
		vmware_shared_strfree(fs->path);

	__vm_mem_free_func(fs);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_vm_shared_free                                            *
 *                                                                            *
 * Purpose: frees shared resources allocated to store virtual machine         *
 *                                                                            *
 * Parameters: vm   - [IN] the virtual machine                                *
 *                                                                            *
 ******************************************************************************/
static void	vmware_vm_shared_free(trx_vmware_vm_t *vm)
{
	trx_vector_ptr_clear_ext(&vm->devs, (trx_clean_func_t)vmware_dev_shared_free);
	trx_vector_ptr_destroy(&vm->devs);

	trx_vector_ptr_clear_ext(&vm->file_systems, (trx_mem_free_func_t)vmware_fs_shared_free);
	trx_vector_ptr_destroy(&vm->file_systems);

	if (NULL != vm->uuid)
		vmware_shared_strfree(vm->uuid);

	if (NULL != vm->id)
		vmware_shared_strfree(vm->id);

	vmware_props_shared_free(vm->props, TRX_VMWARE_VMPROPS_NUM);

	__vm_mem_free_func(vm);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_hv_shared_clean                                           *
 *                                                                            *
 * Purpose: frees shared resources allocated to store vmware hypervisor       *
 *                                                                            *
 * Parameters: hv   - [IN] the vmware hypervisor                              *
 *                                                                            *
 ******************************************************************************/
static void	vmware_hv_shared_clean(trx_vmware_hv_t *hv)
{
	trx_vector_str_clear_ext(&hv->ds_names, vmware_shared_strfree);
	trx_vector_str_destroy(&hv->ds_names);

	trx_vector_ptr_clear_ext(&hv->vms, (trx_clean_func_t)vmware_vm_shared_free);
	trx_vector_ptr_destroy(&hv->vms);

	if (NULL != hv->uuid)
		vmware_shared_strfree(hv->uuid);

	if (NULL != hv->id)
		vmware_shared_strfree(hv->id);

	if (NULL != hv->clusterid)
		vmware_shared_strfree(hv->clusterid);

	if (NULL != hv->datacenter_name)
		vmware_shared_strfree(hv->datacenter_name);

	if (NULL != hv->parent_name)
		vmware_shared_strfree(hv->parent_name);

	if (NULL != hv->parent_type)
		vmware_shared_strfree(hv->parent_type);

	vmware_props_shared_free(hv->props, TRX_VMWARE_HVPROPS_NUM);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_cluster_shared_free                                       *
 *                                                                            *
 * Purpose: frees shared resources allocated to store vmware cluster          *
 *                                                                            *
 * Parameters: cluster   - [IN] the vmware cluster                            *
 *                                                                            *
 ******************************************************************************/
static void	vmware_cluster_shared_free(trx_vmware_cluster_t *cluster)
{
	if (NULL != cluster->name)
		vmware_shared_strfree(cluster->name);

	if (NULL != cluster->id)
		vmware_shared_strfree(cluster->id);

	if (NULL != cluster->status)
		vmware_shared_strfree(cluster->status);

	__vm_mem_free_func(cluster);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_event_shared_free                                         *
 *                                                                            *
 * Purpose: frees shared resources allocated to store vmware event            *
 *                                                                            *
 * Parameters: event - [IN] the vmware event                                  *
 *                                                                            *
 ******************************************************************************/
static void	vmware_event_shared_free(trx_vmware_event_t *event)
{
	if (NULL != event->message)
		vmware_shared_strfree(event->message);

	__vm_mem_free_func(event);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_data_shared_free                                          *
 *                                                                            *
 * Purpose: frees shared resources allocated to store vmware service data     *
 *                                                                            *
 * Parameters: data   - [IN] the vmware service data                          *
 *                                                                            *
 ******************************************************************************/
static void	vmware_data_shared_free(trx_vmware_data_t *data)
{
	if (NULL != data)
	{
		trx_hashset_iter_t	iter;
		trx_vmware_hv_t		*hv;

		trx_hashset_iter_reset(&data->hvs, &iter);
		while (NULL != (hv = (trx_vmware_hv_t *)trx_hashset_iter_next(&iter)))
			vmware_hv_shared_clean(hv);

		trx_hashset_destroy(&data->hvs);
		trx_hashset_destroy(&data->vms_index);

		trx_vector_ptr_clear_ext(&data->clusters, (trx_clean_func_t)vmware_cluster_shared_free);
		trx_vector_ptr_destroy(&data->clusters);

		trx_vector_ptr_clear_ext(&data->events, (trx_clean_func_t)vmware_event_shared_free);
		trx_vector_ptr_destroy(&data->events);

		trx_vector_vmware_datastore_clear_ext(&data->datastores, vmware_datastore_shared_free);
		trx_vector_vmware_datastore_destroy(&data->datastores);

		if (NULL != data->error)
			vmware_shared_strfree(data->error);

		__vm_mem_free_func(data);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_shared_perf_entity_clean                                  *
 *                                                                            *
 * Purpose: cleans resources allocated by vmware performance entity in vmware *
 *          cache                                                             *
 *                                                                            *
 * Parameters: entity - [IN] the entity to free                               *
 *                                                                            *
 ******************************************************************************/
static void	vmware_shared_perf_entity_clean(trx_vmware_perf_entity_t *entity)
{
	trx_vector_ptr_clear_ext(&entity->counters, (trx_mem_free_func_t)vmware_perf_counter_shared_free);
	trx_vector_ptr_destroy(&entity->counters);

	vmware_shared_strfree(entity->query_instance);
	vmware_shared_strfree(entity->type);
	vmware_shared_strfree(entity->id);
	vmware_shared_strfree(entity->error);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_counter_shared_clean                                      *
 *                                                                            *
 * Purpose: frees resources allocated by vmware performance counter           *
 *                                                                            *
 * Parameters: counter - [IN] the performance counter to free                 *
 *                                                                            *
 ******************************************************************************/
static void	vmware_counter_shared_clean(trx_vmware_counter_t *counter)
{
	vmware_shared_strfree(counter->path);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_shared_free                                       *
 *                                                                            *
 * Purpose: frees shared resources allocated to store vmware service          *
 *                                                                            *
 * Parameters: data   - [IN] the vmware service data                          *
 *                                                                            *
 ******************************************************************************/
static void	vmware_service_shared_free(trx_vmware_service_t *service)
{
	trx_hashset_iter_t		iter;
	trx_vmware_counter_t		*counter;
	trx_vmware_perf_entity_t	*entity;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() '%s'@'%s'", __func__, service->username, service->url);

	vmware_shared_strfree(service->url);
	vmware_shared_strfree(service->username);
	vmware_shared_strfree(service->password);

	if (NULL != service->version)
		vmware_shared_strfree(service->version);

	if (NULL != service->fullname)
		vmware_shared_strfree(service->fullname);

	vmware_data_shared_free(service->data);

	trx_hashset_iter_reset(&service->entities, &iter);
	while (NULL != (entity = (trx_vmware_perf_entity_t *)trx_hashset_iter_next(&iter)))
		vmware_shared_perf_entity_clean(entity);

	trx_hashset_destroy(&service->entities);

	trx_hashset_iter_reset(&service->counters, &iter);
	while (NULL != (counter = (trx_vmware_counter_t *)trx_hashset_iter_next(&iter)))
		vmware_counter_shared_clean(counter);

	trx_hashset_destroy(&service->counters);

	__vm_mem_free_func(service);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_cluster_shared_dup                                        *
 *                                                                            *
 * Purpose: copies vmware cluster object into shared memory                   *
 *                                                                            *
 * Parameters: src   - [IN] the vmware cluster object                         *
 *                                                                            *
 * Return value: a copied vmware cluster object                               *
 *                                                                            *
 ******************************************************************************/
static trx_vmware_cluster_t	*vmware_cluster_shared_dup(const trx_vmware_cluster_t *src)
{
	trx_vmware_cluster_t	*cluster;

	cluster = (trx_vmware_cluster_t *)__vm_mem_malloc_func(NULL, sizeof(trx_vmware_cluster_t));
	cluster->id = vmware_shared_strdup(src->id);
	cluster->name = vmware_shared_strdup(src->name);
	cluster->status = vmware_shared_strdup(src->status);

	return cluster;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_event_shared_dup                                          *
 *                                                                            *
 * Purpose: copies vmware event object into shared memory                     *
 *                                                                            *
 * Parameters: src - [IN] the vmware event object                             *
 *                                                                            *
 * Return value: a copied vmware event object                                 *
 *                                                                            *
 ******************************************************************************/
static trx_vmware_event_t	*vmware_event_shared_dup(const trx_vmware_event_t *src)
{
	trx_vmware_event_t	*event;

	event = (trx_vmware_event_t *)__vm_mem_malloc_func(NULL, sizeof(trx_vmware_event_t));
	event->key = src->key;
	event->message = vmware_shared_strdup(src->message);
	event->timestamp = src->timestamp;

	return event;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_datastore_shared_dup                                      *
 *                                                                            *
 * Purpose: copies vmware hypervisor datastore object into shared memory      *
 *                                                                            *
 * Parameters: src   - [IN] the vmware datastore object                       *
 *                                                                            *
 * Return value: a duplicated vmware datastore object                         *
 *                                                                            *
 ******************************************************************************/
static trx_vmware_datastore_t	*vmware_datastore_shared_dup(const trx_vmware_datastore_t *src)
{
	int			i;
	trx_vmware_datastore_t	*datastore;

	datastore = (trx_vmware_datastore_t *)__vm_mem_malloc_func(NULL, sizeof(trx_vmware_datastore_t));
	datastore->uuid = vmware_shared_strdup(src->uuid);
	datastore->name = vmware_shared_strdup(src->name);
	datastore->id = vmware_shared_strdup(src->id);
	VMWARE_VECTOR_CREATE(&datastore->hv_uuids, str);
	trx_vector_str_reserve(&datastore->hv_uuids, src->hv_uuids.values_num);

	datastore->capacity = src->capacity;
	datastore->free_space = src->free_space;
	datastore->uncommitted = src->uncommitted;

	for (i = 0; i < src->hv_uuids.values_num; i++)
		trx_vector_str_append(&datastore->hv_uuids, vmware_shared_strdup(src->hv_uuids.values[i]));

	return datastore;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_dev_shared_dup                                            *
 *                                                                            *
 * Purpose: copies vmware virtual machine device object into shared memory    *
 *                                                                            *
 * Parameters: src   - [IN] the vmware device object                          *
 *                                                                            *
 * Return value: a duplicated vmware device object                            *
 *                                                                            *
 ******************************************************************************/
static trx_vmware_dev_t	*vmware_dev_shared_dup(const trx_vmware_dev_t *src)
{
	trx_vmware_dev_t	*dev;

	dev = (trx_vmware_dev_t *)__vm_mem_malloc_func(NULL, sizeof(trx_vmware_dev_t));
	dev->type = src->type;
	dev->instance = vmware_shared_strdup(src->instance);
	dev->label = vmware_shared_strdup(src->label);

	return dev;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_fs_shared_dup                                             *
 *                                                                            *
 * Purpose: copies vmware virtual machine file system object into shared      *
 *          memory                                                            *
 *                                                                            *
 * Parameters: src   - [IN] the vmware device object                          *
 *                                                                            *
 * Return value: a duplicated vmware device object                            *
 *                                                                            *
 ******************************************************************************/
static trx_vmware_fs_t	*vmware_fs_shared_dup(const trx_vmware_fs_t *src)
{
	trx_vmware_fs_t	*fs;

	fs = (trx_vmware_fs_t *)__vm_mem_malloc_func(NULL, sizeof(trx_vmware_fs_t));
	fs->path = vmware_shared_strdup(src->path);
	fs->capacity = src->capacity;
	fs->free_space = src->free_space;

	return fs;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_props_shared_dup                                          *
 *                                                                            *
 * Purpose: copies object properties list into shared memory                  *
 *                                                                            *
 * Parameters: src       - [IN] the properties list                           *
 *             props_num - [IN] the number of properties in the list          *
 *                                                                            *
 * Return value: a duplicated object properties list                          *
 *                                                                            *
 ******************************************************************************/
static char	**vmware_props_shared_dup(char ** const src, int props_num)
{
	char	**props;
	int	i;

	props = (char **)__vm_mem_malloc_func(NULL, sizeof(char *) * props_num);

	for (i = 0; i < props_num; i++)
		props[i] = vmware_shared_strdup(src[i]);

	return props;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_vm_shared_dup                                             *
 *                                                                            *
 * Purpose: copies vmware virtual machine object into shared memory           *
 *                                                                            *
 * Parameters: src   - [IN] the vmware virtual machine object                 *
 *                                                                            *
 * Return value: a duplicated vmware virtual machine object                   *
 *                                                                            *
 ******************************************************************************/
static trx_vmware_vm_t	*vmware_vm_shared_dup(const trx_vmware_vm_t *src)
{
	trx_vmware_vm_t	*vm;
	int		i;

	vm = (trx_vmware_vm_t *)__vm_mem_malloc_func(NULL, sizeof(trx_vmware_vm_t));

	VMWARE_VECTOR_CREATE(&vm->devs, ptr);
	VMWARE_VECTOR_CREATE(&vm->file_systems, ptr);
	trx_vector_ptr_reserve(&vm->devs, src->devs.values_num);
	trx_vector_ptr_reserve(&vm->file_systems, src->file_systems.values_num);

	vm->uuid = vmware_shared_strdup(src->uuid);
	vm->id = vmware_shared_strdup(src->id);
	vm->props = vmware_props_shared_dup(src->props, TRX_VMWARE_VMPROPS_NUM);

	for (i = 0; i < src->devs.values_num; i++)
		trx_vector_ptr_append(&vm->devs, vmware_dev_shared_dup((trx_vmware_dev_t *)src->devs.values[i]));

	for (i = 0; i < src->file_systems.values_num; i++)
		trx_vector_ptr_append(&vm->file_systems, vmware_fs_shared_dup((trx_vmware_fs_t *)src->file_systems.values[i]));

	return vm;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_hv_shared_copy                                            *
 *                                                                            *
 * Purpose: copies vmware hypervisor object into shared memory                *
 *                                                                            *
 * Parameters: src   - [IN] the vmware hypervisor object                      *
 *                                                                            *
 * Return value: a duplicated vmware hypervisor object                        *
 *                                                                            *
 ******************************************************************************/
static	void	vmware_hv_shared_copy(trx_vmware_hv_t *dst, const trx_vmware_hv_t *src)
{
	int	i;

	VMWARE_VECTOR_CREATE(&dst->ds_names, str);
	VMWARE_VECTOR_CREATE(&dst->vms, ptr);
	trx_vector_str_reserve(&dst->ds_names, src->ds_names.values_num);
	trx_vector_ptr_reserve(&dst->vms, src->vms.values_num);

	dst->uuid = vmware_shared_strdup(src->uuid);
	dst->id = vmware_shared_strdup(src->id);
	dst->clusterid = vmware_shared_strdup(src->clusterid);

	dst->props = vmware_props_shared_dup(src->props, TRX_VMWARE_HVPROPS_NUM);
	dst->datacenter_name = vmware_shared_strdup(src->datacenter_name);
	dst->parent_name = vmware_shared_strdup(src->parent_name);
	dst->parent_type= vmware_shared_strdup(src->parent_type);

	for (i = 0; i < src->ds_names.values_num; i++)
		trx_vector_str_append(&dst->ds_names, vmware_shared_strdup(src->ds_names.values[i]));

	for (i = 0; i < src->vms.values_num; i++)
		trx_vector_ptr_append(&dst->vms, vmware_vm_shared_dup((trx_vmware_vm_t *)src->vms.values[i]));
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_data_shared_dup                                           *
 *                                                                            *
 * Purpose: copies vmware data object into shared memory                      *
 *                                                                            *
 * Parameters: src   - [IN] the vmware data object                            *
 *                                                                            *
 * Return value: a duplicated vmware data object                              *
 *                                                                            *
 ******************************************************************************/
static trx_vmware_data_t	*vmware_data_shared_dup(trx_vmware_data_t *src)
{
	trx_vmware_data_t	*data;
	int			i;
	trx_hashset_iter_t	iter;
	trx_vmware_hv_t		*hv, hv_local;

	data = (trx_vmware_data_t *)__vm_mem_malloc_func(NULL, sizeof(trx_vmware_data_t));

	trx_hashset_create_ext(&data->hvs, 1, vmware_hv_hash, vmware_hv_compare, NULL, __vm_mem_malloc_func,
			__vm_mem_realloc_func, __vm_mem_free_func);
	VMWARE_VECTOR_CREATE(&data->clusters, ptr);
	VMWARE_VECTOR_CREATE(&data->events, ptr);
	VMWARE_VECTOR_CREATE(&data->datastores, vmware_datastore);
	trx_vector_ptr_reserve(&data->clusters, src->clusters.values_num);
	trx_vector_ptr_reserve(&data->events, src->events.values_num);
	trx_vector_vmware_datastore_reserve(&data->datastores, src->datastores.values_num);

	trx_hashset_create_ext(&data->vms_index, 100, vmware_vm_hash, vmware_vm_compare, NULL, __vm_mem_malloc_func,
			__vm_mem_realloc_func, __vm_mem_free_func);

	data->error = vmware_shared_strdup(src->error);

	for (i = 0; i < src->clusters.values_num; i++)
		trx_vector_ptr_append(&data->clusters, vmware_cluster_shared_dup((trx_vmware_cluster_t *)src->clusters.values[i]));

	for (i = 0; i < src->events.values_num; i++)
		trx_vector_ptr_append(&data->events, vmware_event_shared_dup((trx_vmware_event_t *)src->events.values[i]));

	for (i = 0; i < src->datastores.values_num; i++)
		trx_vector_vmware_datastore_append(&data->datastores, vmware_datastore_shared_dup(src->datastores.values[i]));

	trx_hashset_iter_reset(&src->hvs, &iter);
	while (NULL != (hv = (trx_vmware_hv_t *)trx_hashset_iter_next(&iter)))
	{

		vmware_hv_shared_copy(&hv_local, hv);
		hv = (trx_vmware_hv_t *)trx_hashset_insert(&data->hvs, &hv_local, sizeof(hv_local));

		if (SUCCEED != trx_hashset_reserve(&data->vms_index, hv->vms.values_num))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		for (i = 0; i < hv->vms.values_num; i++)
		{
			trx_vmware_vm_index_t	vmi_local = {(trx_vmware_vm_t *)hv->vms.values[i], hv};

			trx_hashset_insert(&data->vms_index, &vmi_local, sizeof(vmi_local));
		}
	}

	data->max_query_metrics = src->max_query_metrics;

	return data;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_datastore_free                                            *
 *                                                                            *
 * Purpose: frees resources allocated to store datastore data                 *
 *                                                                            *
 * Parameters: datastore   - [IN] the datastore                               *
 *                                                                            *
 ******************************************************************************/
static void	vmware_datastore_free(trx_vmware_datastore_t *datastore)
{
	trx_vector_str_clear_ext(&datastore->hv_uuids, trx_str_free);
	trx_vector_str_destroy(&datastore->hv_uuids);

	trx_free(datastore->name);
	trx_free(datastore->uuid);
	trx_free(datastore->id);
	trx_free(datastore);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_props_free                                                *
 *                                                                            *
 * Purpose: frees shared resources allocated to store properties list         *
 *                                                                            *
 * Parameters: props     - [IN] the properties list                           *
 *             props_num - [IN] the number of properties in the list          *
 *                                                                            *
 ******************************************************************************/
static void	vmware_props_free(char **props, int props_num)
{
	int	i;

	if (NULL == props)
		return;

	for (i = 0; i < props_num; i++)
		trx_free(props[i]);

	trx_free(props);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_dev_free                                                  *
 *                                                                            *
 * Purpose: frees resources allocated to store vm device object               *
 *                                                                            *
 * Parameters: dev   - [IN] the vm device                                     *
 *                                                                            *
 ******************************************************************************/
static void	vmware_dev_free(trx_vmware_dev_t *dev)
{
	trx_free(dev->instance);
	trx_free(dev->label);
	trx_free(dev);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_fs_free                                                   *
 *                                                                            *
 * Purpose: frees resources allocated to store vm file system object          *
 *                                                                            *
 * Parameters: fs    - [IN] the file system                                   *
 *                                                                            *
 ******************************************************************************/
static void	vmware_fs_free(trx_vmware_fs_t *fs)
{
	trx_free(fs->path);
	trx_free(fs);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_vm_free                                                   *
 *                                                                            *
 * Purpose: frees resources allocated to store virtual machine                *
 *                                                                            *
 * Parameters: vm   - [IN] the virtual machine                                *
 *                                                                            *
 ******************************************************************************/
static void	vmware_vm_free(trx_vmware_vm_t *vm)
{
	trx_vector_ptr_clear_ext(&vm->devs, (trx_clean_func_t)vmware_dev_free);
	trx_vector_ptr_destroy(&vm->devs);

	trx_vector_ptr_clear_ext(&vm->file_systems, (trx_mem_free_func_t)vmware_fs_free);
	trx_vector_ptr_destroy(&vm->file_systems);

	trx_free(vm->uuid);
	trx_free(vm->id);
	vmware_props_free(vm->props, TRX_VMWARE_VMPROPS_NUM);
	trx_free(vm);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_hv_clean                                                  *
 *                                                                            *
 * Purpose: frees resources allocated to store vmware hypervisor              *
 *                                                                            *
 * Parameters: hv   - [IN] the vmware hypervisor                              *
 *                                                                            *
 ******************************************************************************/
static void	vmware_hv_clean(trx_vmware_hv_t *hv)
{
	trx_vector_str_clear_ext(&hv->ds_names, trx_str_free);
	trx_vector_str_destroy(&hv->ds_names);

	trx_vector_ptr_clear_ext(&hv->vms, (trx_clean_func_t)vmware_vm_free);
	trx_vector_ptr_destroy(&hv->vms);

	trx_free(hv->uuid);
	trx_free(hv->id);
	trx_free(hv->clusterid);
	trx_free(hv->datacenter_name);
	trx_free(hv->parent_name);
	trx_free(hv->parent_type);
	vmware_props_free(hv->props, TRX_VMWARE_HVPROPS_NUM);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_cluster_free                                              *
 *                                                                            *
 * Purpose: frees resources allocated to store vmware cluster                 *
 *                                                                            *
 * Parameters: cluster   - [IN] the vmware cluster                            *
 *                                                                            *
 ******************************************************************************/
static void	vmware_cluster_free(trx_vmware_cluster_t *cluster)
{
	trx_free(cluster->name);
	trx_free(cluster->id);
	trx_free(cluster->status);
	trx_free(cluster);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_event_free                                                *
 *                                                                            *
 * Purpose: frees resources allocated to store vmware event                   *
 *                                                                            *
 * Parameters: event - [IN] the vmware event                                  *
 *                                                                            *
 ******************************************************************************/
static void	vmware_event_free(trx_vmware_event_t *event)
{
	trx_free(event->message);
	trx_free(event);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_data_free                                                 *
 *                                                                            *
 * Purpose: frees resources allocated to store vmware service data            *
 *                                                                            *
 * Parameters: data   - [IN] the vmware service data                          *
 *                                                                            *
 ******************************************************************************/
static void	vmware_data_free(trx_vmware_data_t *data)
{
	trx_hashset_iter_t	iter;
	trx_vmware_hv_t		*hv;

	trx_hashset_iter_reset(&data->hvs, &iter);
	while (NULL != (hv = (trx_vmware_hv_t *)trx_hashset_iter_next(&iter)))
		vmware_hv_clean(hv);

	trx_hashset_destroy(&data->hvs);

	trx_vector_ptr_clear_ext(&data->clusters, (trx_clean_func_t)vmware_cluster_free);
	trx_vector_ptr_destroy(&data->clusters);

	trx_vector_ptr_clear_ext(&data->events, (trx_clean_func_t)vmware_event_free);
	trx_vector_ptr_destroy(&data->events);

	trx_vector_vmware_datastore_clear_ext(&data->datastores, vmware_datastore_free);
	trx_vector_vmware_datastore_destroy(&data->datastores);

	trx_free(data->error);
	trx_free(data);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_counter_free                                              *
 *                                                                            *
 * Purpose: frees vmware performance counter and the resources allocated by   *
 *          it                                                                *
 *                                                                            *
 * Parameters: counter - [IN] the performance counter to free                 *
 *                                                                            *
 ******************************************************************************/
static void	vmware_counter_free(trx_vmware_counter_t *counter)
{
	trx_free(counter->path);
	trx_free(counter);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_authenticate                                      *
 *                                                                            *
 * Purpose: authenticates vmware service                                      *
 *                                                                            *
 * Parameters: service    - [IN] the vmware service                           *
 *             easyhandle - [IN] the CURL handle                              *
 *             page       - [IN] the CURL output buffer                       *
 *             error      - [OUT] the error message in the case of failure    *
 *                                                                            *
 * Return value: SUCCEED - the authentication was completed successfully      *
 *               FAIL    - the authentication process has failed              *
 *                                                                            *
 * Comments: If service type is unknown this function will attempt to         *
 *           determine the right service type by trying to login with vCenter *
 *           and vSphere session managers.                                    *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_authenticate(trx_vmware_service_t *service, CURL *easyhandle, TRX_HTTPPAGE *page,
		char **error)
{
#	define TRX_POST_VMWARE_AUTH						\
		TRX_POST_VSPHERE_HEADER						\
		"<ns0:Login xsi:type=\"ns0:LoginRequestType\">"			\
			"<ns0:_this type=\"SessionManager\">%s</ns0:_this>"	\
			"<ns0:userName>%s</ns0:userName>"			\
			"<ns0:password>%s</ns0:password>"			\
		"</ns0:Login>"							\
		TRX_POST_VSPHERE_FOOTER

	char		xml[MAX_STRING_LEN], *error_object = NULL, *username_esc = NULL, *password_esc = NULL;
	CURLoption	opt;
	CURLcode	err;
	xmlDoc		*doc = NULL;
	int		ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() '%s'@'%s'", __func__, service->username, service->url);

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_COOKIEFILE, "")) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_FOLLOWLOCATION, 1L)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_WRITEFUNCTION, curl_write_cb)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_WRITEDATA, page)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_PRIVATE, page)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_HEADERFUNCTION, curl_header_cb)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_SSL_VERIFYPEER, 0L)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_POST, 1L)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_URL, service->url)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_TIMEOUT,
					(long)CONFIG_VMWARE_TIMEOUT)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_SSL_VERIFYHOST, 0L)))
	{
		*error = trx_dsprintf(*error, "Cannot set cURL option %d: %s.", (int)opt, curl_easy_strerror(err));
		goto out;
	}

	if (NULL != CONFIG_SOURCE_IP)
	{
		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_INTERFACE, CONFIG_SOURCE_IP)))
		{
			*error = trx_dsprintf(*error, "Cannot set cURL option %d: %s.", (int)opt,
					curl_easy_strerror(err));
			goto out;
		}
	}

	username_esc = xml_escape_dyn(service->username);
	password_esc = xml_escape_dyn(service->password);

	if (TRX_VMWARE_TYPE_UNKNOWN == service->type)
	{
		/* try to detect the service type first using vCenter service manager object */
		trx_snprintf(xml, sizeof(xml), TRX_POST_VMWARE_AUTH,
				vmware_service_objects[TRX_VMWARE_TYPE_VCENTER].session_manager,
				username_esc, password_esc);

		if (SUCCEED != trx_soap_post(__func__, easyhandle, xml, &doc, error) && NULL == doc)
			goto out;

		if (NULL == *error)
		{
			/* Successfully authenticated with vcenter service manager. */
			/* Set the service type and return with success.            */
			service->type = TRX_VMWARE_TYPE_VCENTER;
			ret = SUCCEED;
			goto out;
		}

		/* If the wrong service manager was used, set the service type as vsphere and */
		/* try again with vsphere service manager. Otherwise return with failure.     */
		if (NULL == (error_object = trx_xml_read_doc_value(doc,
				TRX_XPATH_LN3("detail", "NotAuthenticatedFault", "object"))))
		{
			goto out;
		}

		if (0 != strcmp(error_object, vmware_service_objects[TRX_VMWARE_TYPE_VCENTER].session_manager))
			goto out;

		service->type = TRX_VMWARE_TYPE_VSPHERE;
		trx_free(*error);
	}

	trx_snprintf(xml, sizeof(xml), TRX_POST_VMWARE_AUTH, vmware_service_objects[service->type].session_manager,
			username_esc, password_esc);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, xml, NULL, error))
		goto out;

	ret = SUCCEED;
out:
	trx_free(error_object);
	trx_free(username_esc);
	trx_free(password_esc);
	trx_xml_free_doc(doc);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_logout                                            *
 *                                                                            *
 * Purpose: Close unused connection with vCenter                              *
 *                                                                            *
 * Parameters: service    - [IN] the vmware service                           *
 *             easyhandle - [IN] the CURL handle                              *
 *             error      - [OUT] the error message in the case of failure    *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_logout(trx_vmware_service_t *service, CURL *easyhandle, char **error)
{
#	define TRX_POST_VMWARE_LOGOUT						\
		TRX_POST_VSPHERE_HEADER						\
		"<ns0:Logout>"							\
			"<ns0:_this type=\"SessionManager\">%s</ns0:_this>"	\
		"</ns0:Logout>"							\
		TRX_POST_VSPHERE_FOOTER

	char	tmp[MAX_STRING_LEN];

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_VMWARE_LOGOUT, vmware_service_objects[service->type].session_manager);
	return trx_soap_post(__func__, easyhandle, tmp, NULL, error);
}

typedef struct
{
	const char	*property_collector;
	CURL		*easyhandle;
	char		*token;
}
trx_property_collection_iter;

static int	trx_property_collection_init(CURL *easyhandle, const char *property_collection_query,
		const char *property_collector, trx_property_collection_iter **iter, xmlDoc **xdoc, char **error)
{
#	define TRX_XPATH_RETRIEVE_PROPERTIES_TOKEN			\
		"/*[local-name()='Envelope']/*[local-name()='Body']"	\
		"/*[local-name()='RetrievePropertiesExResponse']"	\
		"/*[local-name()='returnval']/*[local-name()='token']"

	*iter = (trx_property_collection_iter *)trx_malloc(*iter, sizeof(trx_property_collection_iter));
	(*iter)->property_collector = property_collector;
	(*iter)->easyhandle = easyhandle;
	(*iter)->token = NULL;

	if (SUCCEED != trx_soap_post("trx_property_collection_init", (*iter)->easyhandle, property_collection_query, xdoc, error))
		return FAIL;

	(*iter)->token = trx_xml_read_doc_value(*xdoc, TRX_XPATH_RETRIEVE_PROPERTIES_TOKEN);

	return SUCCEED;
}

static int	trx_property_collection_next(trx_property_collection_iter *iter, xmlDoc **xdoc, char **error)
{
#	define TRX_POST_CONTINUE_RETRIEVE_PROPERTIES								\
		TRX_POST_VSPHERE_HEADER										\
		"<ns0:ContinueRetrievePropertiesEx xsi:type=\"ns0:ContinueRetrievePropertiesExRequestType\">"	\
			"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"					\
			"<ns0:token>%s</ns0:token>"								\
		"</ns0:ContinueRetrievePropertiesEx>"								\
		TRX_POST_VSPHERE_FOOTER

#	define TRX_XPATH_CONTINUE_RETRIEVE_PROPERTIES_TOKEN			\
		"/*[local-name()='Envelope']/*[local-name()='Body']"		\
		"/*[local-name()='ContinueRetrievePropertiesExResponse']"	\
		"/*[local-name()='returnval']/*[local-name()='token']"

	char	*token_esc, post[MAX_STRING_LEN];

	treegix_log(LOG_LEVEL_DEBUG, "%s() continue retrieving properties with token: '%s'", __func__,
			iter->token);

	token_esc = xml_escape_dyn(iter->token);
	trx_snprintf(post, sizeof(post), TRX_POST_CONTINUE_RETRIEVE_PROPERTIES, iter->property_collector, token_esc);
	trx_free(token_esc);

	if (SUCCEED != trx_soap_post(__func__, iter->easyhandle, post, xdoc, error))
		return FAIL;

	trx_free(iter->token);
	iter->token = trx_xml_read_doc_value(*xdoc, TRX_XPATH_CONTINUE_RETRIEVE_PROPERTIES_TOKEN);

	return SUCCEED;
}

static void	trx_property_collection_free(trx_property_collection_iter *iter)
{
	if (NULL != iter)
	{
		trx_free(iter->token);
		trx_free(iter);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_contents                                      *
 *                                                                            *
 * Purpose: retrieves vmware service instance contents                        *
 *                                                                            *
 * Parameters: easyhandle - [IN] the CURL handle                              *
 *             version    - [OUT] the version of the instance                 *
 *             fullname   - [OUT] the fullname of the instance                *
 *             error      - [OUT] the error message in the case of failure    *
 *                                                                            *
 * Return value: SUCCEED - the contents were retrieved successfully           *
 *               FAIL    - the content retrieval failed                       *
 *                                                                            *
 ******************************************************************************/
static	int	vmware_service_get_contents(CURL *easyhandle, char **version, char **fullname, char **error)
{
#	define TRX_POST_VMWARE_CONTENTS 							\
		TRX_POST_VSPHERE_HEADER								\
		"<ns0:RetrieveServiceContent>"							\
			"<ns0:_this type=\"ServiceInstance\">ServiceInstance</ns0:_this>"	\
		"</ns0:RetrieveServiceContent>"							\
		TRX_POST_VSPHERE_FOOTER

	xmlDoc	*doc = NULL;

	if (SUCCEED != trx_soap_post(__func__, easyhandle, TRX_POST_VMWARE_CONTENTS, &doc, error))
	{
		trx_xml_free_doc(doc);
		return FAIL;
	}

	*version = trx_xml_read_doc_value(doc, TRX_XPATH_VMWARE_ABOUT("version"));
	*fullname = trx_xml_read_doc_value(doc, TRX_XPATH_VMWARE_ABOUT("fullName"));
	trx_xml_free_doc(doc);

	return SUCCEED;

#	undef TRX_POST_VMWARE_CONTENTS
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_perf_counter_refreshrate                      *
 *                                                                            *
 * Purpose: get the performance counter refreshrate for the specified entity  *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             type         - [IN] the entity type (HostSystem, Datastore or  *
 *                                 VirtualMachine)                            *
 *             id           - [IN] the entity id                              *
 *             refresh_rate - [OUT] a pointer to variable to store the        *
 *                                  regresh rate                              *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the authentication was completed successfully      *
 *               FAIL    - the authentication process has failed              *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_perf_counter_refreshrate(trx_vmware_service_t *service, CURL *easyhandle,
		const char *type, const char *id, int *refresh_rate, char **error)
{
#	define TRX_POST_VCENTER_PERF_COUNTERS_REFRESH_RATE			\
		TRX_POST_VSPHERE_HEADER						\
		"<ns0:QueryPerfProviderSummary>"				\
			"<ns0:_this type=\"PerformanceManager\">%s</ns0:_this>"	\
			"<ns0:entity type=\"%s\">%s</ns0:entity>"		\
		"</ns0:QueryPerfProviderSummary>"				\
		TRX_POST_VSPHERE_FOOTER

	char	tmp[MAX_STRING_LEN], *value = NULL, *id_esc;
	int	ret = FAIL;
	xmlDoc	*doc = NULL;


	treegix_log(LOG_LEVEL_DEBUG, "In %s() type: %s id: %s", __func__, type, id);

	id_esc = xml_escape_dyn(id);
	trx_snprintf(tmp, sizeof(tmp), TRX_POST_VCENTER_PERF_COUNTERS_REFRESH_RATE,
			vmware_service_objects[service->type].performance_manager, type, id_esc);
	trx_free(id_esc);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, &doc, error))
		goto out;

	if (NULL != (value = trx_xml_read_doc_value(doc, TRX_XPATH_ISAGGREGATE())))
	{
		trx_free(value);
		*refresh_rate = TRX_VMWARE_PERF_INTERVAL_NONE;
		ret = SUCCEED;

		treegix_log(LOG_LEVEL_DEBUG, "%s() refresh_rate: unused", __func__);
		goto out;
	}
	else if (NULL == (value = trx_xml_read_doc_value(doc, TRX_XPATH_REFRESHRATE())))
	{
		*error = trx_strdup(*error, "Cannot find refreshRate.");
		goto out;
	}

	treegix_log(LOG_LEVEL_DEBUG, "%s() refresh_rate:%s", __func__, value);

	if (SUCCEED != (ret = is_uint31(value, refresh_rate)))
		*error = trx_dsprintf(*error, "Cannot convert refreshRate from %s.",  value);

	trx_free(value);
out:
	trx_xml_free_doc(doc);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_perf_counters                                 *
 *                                                                            *
 * Purpose: get the performance counter ids                                   *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             counters     - [IN/OUT] the vector the created performance     *
 *                                     counter object should be added to      *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_perf_counters(trx_vmware_service_t *service, CURL *easyhandle,
		trx_vector_ptr_t *counters, char **error)
{
#	define TRX_POST_VMWARE_GET_PERFCOUNTER							\
		TRX_POST_VSPHERE_HEADER								\
		"<ns0:RetrievePropertiesEx>"							\
			"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"			\
			"<ns0:specSet>"								\
				"<ns0:propSet>"							\
					"<ns0:type>PerformanceManager</ns0:type>"		\
					"<ns0:pathSet>perfCounter</ns0:pathSet>"		\
				"</ns0:propSet>"						\
				"<ns0:objectSet>"						\
					"<ns0:obj type=\"PerformanceManager\">%s</ns0:obj>"	\
				"</ns0:objectSet>"						\
			"</ns0:specSet>"							\
			"<ns0:options/>"							\
		"</ns0:RetrievePropertiesEx>"							\
		TRX_POST_VSPHERE_FOOTER

	char		tmp[MAX_STRING_LEN], *group = NULL, *key = NULL, *rollup = NULL, *stats = NULL,
			*counterid = NULL;
	xmlDoc		*doc = NULL;
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	int		i, ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_VMWARE_GET_PERFCOUNTER,
			vmware_service_objects[service->type].property_collector,
			vmware_service_objects[service->type].performance_manager);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, &doc, error))
		goto out;

	xpathCtx = xmlXPathNewContext(doc);

	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)TRX_XPATH_COUNTERINFO(), xpathCtx)))
	{
		*error = trx_strdup(*error, "Cannot make performance counter list parsing query.");
		goto clean;
	}

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
	{
		*error = trx_strdup(*error, "Cannot find items in performance counter list.");
		goto clean;
	}

	nodeset = xpathObj->nodesetval;
	trx_vector_ptr_reserve(counters, 2 * nodeset->nodeNr + counters->values_alloc);

	for (i = 0; i < nodeset->nodeNr; i++)
	{
		trx_vmware_counter_t	*counter;

		group = trx_xml_read_node_value(doc, nodeset->nodeTab[i],
				"*[local-name()='groupInfo']/*[local-name()='key']");

		key = trx_xml_read_node_value(doc, nodeset->nodeTab[i],
						"*[local-name()='nameInfo']/*[local-name()='key']");

		rollup = trx_xml_read_node_value(doc, nodeset->nodeTab[i], "*[local-name()='rollupType']");
		stats = trx_xml_read_node_value(doc, nodeset->nodeTab[i], "*[local-name()='statsType']");
		counterid = trx_xml_read_node_value(doc, nodeset->nodeTab[i], "*[local-name()='key']");

		if (NULL != group && NULL != key && NULL != rollup && NULL != counterid)
		{
			counter = (trx_vmware_counter_t *)trx_malloc(NULL, sizeof(trx_vmware_counter_t));
			counter->path = trx_dsprintf(NULL, "%s/%s[%s]", group, key, rollup);
			TRX_STR2UINT64(counter->id, counterid);

			trx_vector_ptr_append(counters, counter);

			treegix_log(LOG_LEVEL_DEBUG, "adding performance counter %s:" TRX_FS_UI64, counter->path,
					counter->id);
		}

		if (NULL != group && NULL != key && NULL != rollup && NULL != counterid && NULL != stats)
		{
			counter = (trx_vmware_counter_t *)trx_malloc(NULL, sizeof(trx_vmware_counter_t));
			counter->path = trx_dsprintf(NULL, "%s/%s[%s,%s]", group, key, rollup, stats);
			TRX_STR2UINT64(counter->id, counterid);

			trx_vector_ptr_append(counters, counter);

			treegix_log(LOG_LEVEL_DEBUG, "adding performance counter %s:" TRX_FS_UI64, counter->path,
					counter->id);
		}

		trx_free(counterid);
		trx_free(stats);
		trx_free(rollup);
		trx_free(key);
		trx_free(group);
	}

	ret = SUCCEED;
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);
out:
	trx_xml_free_doc(doc);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_vm_get_nic_devices                                        *
 *                                                                            *
 * Purpose: gets virtual machine network interface devices                    *
 *                                                                            *
 * Parameters: vm      - [OUT] the virtual machine                            *
 *             details - [IN] a xml document containing virtual machine data  *
 *                                                                            *
 * Comments: The network interface devices are taken from vm device list      *
 *           filtered by macAddress key.                                      *
 *                                                                            *
 ******************************************************************************/
static void	vmware_vm_get_nic_devices(trx_vmware_vm_t *vm, xmlDoc *details)
{
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	int		i, nics = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	xpathCtx = xmlXPathNewContext(details);

	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)TRX_XPATH_VM_HARDWARE("device")
			"[*[local-name()='macAddress']]", xpathCtx)))
	{
		goto clean;
	}

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto clean;

	nodeset = xpathObj->nodesetval;
	trx_vector_ptr_reserve(&vm->devs, nodeset->nodeNr + vm->devs.values_alloc);

	for (i = 0; i < nodeset->nodeNr; i++)
	{
		char			*key;
		trx_vmware_dev_t	*dev;

		if (NULL == (key = trx_xml_read_node_value(details, nodeset->nodeTab[i], "*[local-name()='key']")))
			continue;

		dev = (trx_vmware_dev_t *)trx_malloc(NULL, sizeof(trx_vmware_dev_t));
		dev->type =  TRX_VMWARE_DEV_TYPE_NIC;
		dev->instance = key;
		dev->label = trx_xml_read_node_value(details, nodeset->nodeTab[i],
				"*[local-name()='deviceInfo']/*[local-name()='label']");

		trx_vector_ptr_append(&vm->devs, dev);
		nics++;
	}
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s() found:%d", __func__, nics);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_vm_get_disk_devices                                       *
 *                                                                            *
 * Purpose: gets virtual machine virtual disk devices                         *
 *                                                                            *
 * Parameters: vm      - [OUT] the virtual machine                            *
 *             details - [IN] a xml document containing virtual machine data  *
 *                                                                            *
 ******************************************************************************/
static void	vmware_vm_get_disk_devices(trx_vmware_vm_t *vm, xmlDoc *details)
{
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	int		i, disks = 0;
	char		*xpath = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	xpathCtx = xmlXPathNewContext(details);

	/* select all hardware devices of VirtualDisk type */
	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)TRX_XPATH_VM_HARDWARE("device")
			"[string(@*[local-name()='type'])='VirtualDisk']", xpathCtx)))
	{
		goto clean;
	}

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto clean;

	nodeset = xpathObj->nodesetval;
	trx_vector_ptr_reserve(&vm->devs, nodeset->nodeNr + vm->devs.values_alloc);

	for (i = 0; i < nodeset->nodeNr; i++)
	{
		trx_vmware_dev_t	*dev;
		char			*unitNumber = NULL, *controllerKey = NULL, *busNumber = NULL,
					*controllerLabel = NULL, *controllerType = NULL,
					*scsiCtlrUnitNumber = NULL;
		xmlXPathObject		*xpathObjController = NULL;

		do
		{
			if (NULL == (unitNumber = trx_xml_read_node_value(details, nodeset->nodeTab[i],
					"*[local-name()='unitNumber']")))
			{
				break;
			}

			if (NULL == (controllerKey = trx_xml_read_node_value(details, nodeset->nodeTab[i],
					"*[local-name()='controllerKey']")))
			{
				break;
			}

			/* find the controller (parent) device */
			xpath = trx_dsprintf(xpath, TRX_XPATH_VM_HARDWARE("device")
					"[*[local-name()='key']/text()='%s']", controllerKey);

			if (NULL == (xpathObjController = xmlXPathEvalExpression((xmlChar *)xpath, xpathCtx)))
				break;

			if (0 != xmlXPathNodeSetIsEmpty(xpathObjController->nodesetval))
				break;

			if (NULL == (busNumber = trx_xml_read_node_value(details,
					xpathObjController->nodesetval->nodeTab[0], "*[local-name()='busNumber']")))
			{
				break;
			}

			/* scsiCtlrUnitNumber property is simply used to determine controller type. */
			/* For IDE controllers it is not set.                                       */
			scsiCtlrUnitNumber = trx_xml_read_node_value(details, xpathObjController->nodesetval->nodeTab[0],
				"*[local-name()='scsiCtlrUnitNumber']");

			dev = (trx_vmware_dev_t *)trx_malloc(NULL, sizeof(trx_vmware_dev_t));
			dev->type =  TRX_VMWARE_DEV_TYPE_DISK;

			/* the virtual disk instance has format <controller type><busNumber>:<unitNumber>     */
			/* where controller type is either ide, sata or scsi depending on the controller type */

			dev->label = trx_xml_read_node_value(details, nodeset->nodeTab[i],
					"*[local-name()='deviceInfo']/*[local-name()='label']");

			controllerLabel = trx_xml_read_node_value(details, xpathObjController->nodesetval->nodeTab[0],
				"*[local-name()='deviceInfo']/*[local-name()='label']");

			if (NULL != scsiCtlrUnitNumber ||
				(NULL != controllerLabel && NULL != strstr(controllerLabel, "SCSI")))
			{
				controllerType = "scsi";
			}
			else if (NULL != controllerLabel && NULL != strstr(controllerLabel, "SATA"))
			{
				controllerType = "sata";
			}
			else
			{
				controllerType = "ide";
			}

			dev->instance = trx_dsprintf(NULL, "%s%s:%s", controllerType, busNumber, unitNumber);
			trx_vector_ptr_append(&vm->devs, dev);

			disks++;

		}
		while (0);

		if (NULL != xpathObjController)
			xmlXPathFreeObject(xpathObjController);

		trx_free(controllerLabel);
		trx_free(scsiCtlrUnitNumber);
		trx_free(busNumber);
		trx_free(unitNumber);
		trx_free(controllerKey);

	}
clean:
	trx_free(xpath);

	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s() found:%d", __func__, disks);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_vm_get_file_systems                                       *
 *                                                                            *
 * Purpose: gets the parameters of virtual machine disks                      *
 *                                                                            *
 * Parameters: vm      - [OUT] the virtual machine                            *
 *             details - [IN] a xml document containing virtual machine data  *
 *                                                                            *
 ******************************************************************************/
static void	vmware_vm_get_file_systems(trx_vmware_vm_t *vm, xmlDoc *details)
{
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	int		i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	xpathCtx = xmlXPathNewContext(details);

	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)TRX_XPATH_VM_GUESTDISKS(), xpathCtx)))
		goto clean;

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto clean;

	nodeset = xpathObj->nodesetval;
	trx_vector_ptr_reserve(&vm->file_systems, nodeset->nodeNr + vm->file_systems.values_alloc);

	for (i = 0; i < nodeset->nodeNr; i++)
	{
		trx_vmware_fs_t	*fs;
		char		*value;

		if (NULL == (value = trx_xml_read_node_value(details, nodeset->nodeTab[i], "*[local-name()='diskPath']")))
			continue;

		fs = (trx_vmware_fs_t *)trx_malloc(NULL, sizeof(trx_vmware_fs_t));
		memset(fs, 0, sizeof(trx_vmware_fs_t));

		fs->path = value;

		if (NULL != (value = trx_xml_read_node_value(details, nodeset->nodeTab[i], "*[local-name()='capacity']")))
		{
			TRX_STR2UINT64(fs->capacity, value);
			trx_free(value);
		}

		if (NULL != (value = trx_xml_read_node_value(details, nodeset->nodeTab[i], "*[local-name()='freeSpace']")))
		{
			TRX_STR2UINT64(fs->free_space, value);
			trx_free(value);
		}

		trx_vector_ptr_append(&vm->file_systems, fs);
	}
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s() found:%d", __func__, vm->file_systems.values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_vm_data                                       *
 *                                                                            *
 * Purpose: gets the virtual machine data                                     *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             vmid         - [IN] the virtual machine id                     *
 *             propmap      - [IN] the xpaths of the properties to read       *
 *             props_num    - [IN] the number of properties to read           *
 *             xdoc         - [OUT] a reference to output xml document        *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_vm_data(trx_vmware_service_t *service, CURL *easyhandle, const char *vmid,
		const trx_vmware_propmap_t *propmap, int props_num, xmlDoc **xdoc, char **error)
{
#	define TRX_POST_VMWARE_VM_STATUS_EX 						\
		TRX_POST_VSPHERE_HEADER							\
		"<ns0:RetrievePropertiesEx>"						\
			"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"		\
			"<ns0:specSet>"							\
				"<ns0:propSet>"						\
					"<ns0:type>VirtualMachine</ns0:type>"		\
					"<ns0:pathSet>config.hardware</ns0:pathSet>"	\
					"<ns0:pathSet>config.uuid</ns0:pathSet>"	\
					"<ns0:pathSet>config.instanceUuid</ns0:pathSet>"\
					"<ns0:pathSet>guest.disk</ns0:pathSet>"		\
					"%s"						\
				"</ns0:propSet>"					\
				"<ns0:objectSet>"					\
					"<ns0:obj type=\"VirtualMachine\">%s</ns0:obj>"	\
				"</ns0:objectSet>"					\
			"</ns0:specSet>"						\
			"<ns0:options/>"						\
		"</ns0:RetrievePropertiesEx>"						\
		TRX_POST_VSPHERE_FOOTER

	char	tmp[MAX_STRING_LEN], props[MAX_STRING_LEN], *vmid_esc;
	int	i, ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() vmid:'%s'", __func__, vmid);
	props[0] = '\0';

	for (i = 0; i < props_num; i++)
	{
		trx_strlcat(props, "<ns0:pathSet>", sizeof(props));
		trx_strlcat(props, propmap[i].name, sizeof(props));
		trx_strlcat(props, "</ns0:pathSet>", sizeof(props));
	}

	vmid_esc = xml_escape_dyn(vmid);

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_VMWARE_VM_STATUS_EX,
			vmware_service_objects[service->type].property_collector, props, vmid_esc);

	trx_free(vmid_esc);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, xdoc, error))
		goto out;

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_create_vm                                         *
 *                                                                            *
 * Purpose: create virtual machine object                                     *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             id           - [IN] the virtual machine id                     *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: The created virtual machine object or NULL if an error was   *
 *               detected.                                                    *
 *                                                                            *
 ******************************************************************************/
static trx_vmware_vm_t	*vmware_service_create_vm(trx_vmware_service_t *service,  CURL *easyhandle,
		const char *id, char **error)
{
	trx_vmware_vm_t	*vm;
	char		*value;
	xmlDoc		*details = NULL;
	const char	*uuid_xpath[3] = {NULL, TRX_XPATH_VM_UUID(), TRX_XPATH_VM_INSTANCE_UUID()};
	int		ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() vmid:'%s'", __func__, id);

	vm = (trx_vmware_vm_t *)trx_malloc(NULL, sizeof(trx_vmware_vm_t));
	memset(vm, 0, sizeof(trx_vmware_vm_t));

	trx_vector_ptr_create(&vm->devs);
	trx_vector_ptr_create(&vm->file_systems);

	if (SUCCEED != vmware_service_get_vm_data(service, easyhandle, id, vm_propmap,
			TRX_VMWARE_VMPROPS_NUM, &details, error))
	{
		goto out;
	}

	if (NULL == (value = trx_xml_read_doc_value(details, uuid_xpath[service->type])))
		goto out;

	vm->uuid = value;
	vm->id = trx_strdup(NULL, id);

	if (NULL == (vm->props = xml_read_props(details, vm_propmap, TRX_VMWARE_VMPROPS_NUM)))
		goto out;

	vmware_vm_get_nic_devices(vm, details);
	vmware_vm_get_disk_devices(vm, details);
	vmware_vm_get_file_systems(vm, details);

	ret = SUCCEED;
out:
	trx_xml_free_doc(details);

	if (SUCCEED != ret)
	{
		vmware_vm_free(vm);
		vm = NULL;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return vm;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_refresh_datastore_info                            *
 *                                                                            *
 * Purpose: Refreshes all storage related information including free-space,   *
 *          capacity, and detailed usage of virtual machines.                 *
 *                                                                            *
 * Parameters: easyhandle   - [IN] the CURL handle                            *
 *             id           - [IN] the datastore id                           *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Comments: This is required for ESX/ESXi hosts version < 6.0 only           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_refresh_datastore_info(CURL *easyhandle, const char *id, char **error)
{
#	define TRX_POST_REFRESH_DATASTORE							\
		TRX_POST_VSPHERE_HEADER								\
		"<ns0:RefreshDatastoreStorageInfo>"						\
			"<ns0:_this type=\"Datastore\">%s</ns0:_this>"				\
		"</ns0:RefreshDatastoreStorageInfo>"						\
		TRX_POST_VSPHERE_FOOTER

	char		tmp[MAX_STRING_LEN];
	int		ret = FAIL;

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_REFRESH_DATASTORE, id);
	if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, NULL, error))
		goto out;

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_create_datastore                                  *
 *                                                                            *
 * Purpose: create vmware hypervisor datastore object                         *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             id           - [IN] the datastore id                           *
 *                                                                            *
 * Return value: The created datastore object or NULL if an error was         *
 *                detected                                                    *
 *                                                                            *
 ******************************************************************************/
static trx_vmware_datastore_t	*vmware_service_create_datastore(const trx_vmware_service_t *service, CURL *easyhandle,
		const char *id)
{
#	define TRX_POST_DATASTORE_GET								\
		TRX_POST_VSPHERE_HEADER								\
		"<ns0:RetrievePropertiesEx>"							\
			"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"			\
			"<ns0:specSet>"								\
				"<ns0:propSet>"							\
					"<ns0:type>Datastore</ns0:type>"			\
					"<ns0:pathSet>summary</ns0:pathSet>"			\
					"<ns0:pathSet>host</ns0:pathSet>"			\
				"</ns0:propSet>"						\
				"<ns0:objectSet>"						\
					"<ns0:obj type=\"Datastore\">%s</ns0:obj>"		\
				"</ns0:objectSet>"						\
			"</ns0:specSet>"							\
			"<ns0:options/>"							\
		"</ns0:RetrievePropertiesEx>"							\
		TRX_POST_VSPHERE_FOOTER

	char			tmp[MAX_STRING_LEN], *uuid = NULL, *name = NULL, *path, *id_esc, *value, *error = NULL;
	trx_vmware_datastore_t	*datastore = NULL;
	trx_uint64_t		capacity = TRX_MAX_UINT64, free_space = TRX_MAX_UINT64, uncommitted = TRX_MAX_UINT64;
	xmlDoc			*doc = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() datastore:'%s'", __func__, id);

	id_esc = xml_escape_dyn(id);

	if (TRX_VMWARE_TYPE_VSPHERE == service->type &&
			NULL != service->version && TRX_VMWARE_DS_REFRESH_VERSION > atoi(service->version) &&
			SUCCEED != vmware_service_refresh_datastore_info(easyhandle, id_esc, &error))
	{
		trx_free(id_esc);
		goto out;
	}

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_DATASTORE_GET,
			vmware_service_objects[service->type].property_collector, id_esc);

	trx_free(id_esc);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, &doc, &error))
		goto out;

	name = trx_xml_read_doc_value(doc, TRX_XPATH_DATASTORE_SUMMARY("name"));

	if (NULL != (path = trx_xml_read_doc_value(doc, TRX_XPATH_DATASTORE_MOUNT())))
	{
		if ('\0' != *path)
		{
			size_t	len;
			char	*ptr;

			len = strlen(path);

			if ('/' == path[len - 1])
				path[len - 1] = '\0';

			for (ptr = path + len - 2; ptr > path && *ptr != '/'; ptr--)
				;

			uuid = trx_strdup(NULL, ptr + 1);
		}
		trx_free(path);
	}

	if (TRX_VMWARE_TYPE_VSPHERE == service->type)
	{
		if (NULL != (value = trx_xml_read_doc_value(doc, TRX_XPATH_DATASTORE_SUMMARY("capacity"))))
		{
			is_uint64(value, &capacity);
			trx_free(value);
		}

		if (NULL != (value = trx_xml_read_doc_value(doc, TRX_XPATH_DATASTORE_SUMMARY("freeSpace"))))
		{
			is_uint64(value, &free_space);
			trx_free(value);
		}

		if (NULL != (value = trx_xml_read_doc_value(doc, TRX_XPATH_DATASTORE_SUMMARY("uncommitted"))))
		{
			is_uint64(value, &uncommitted);
			trx_free(value);
		}
	}

	datastore = (trx_vmware_datastore_t *)trx_malloc(NULL, sizeof(trx_vmware_datastore_t));
	datastore->name = (NULL != name) ? name : trx_strdup(NULL, id);
	datastore->uuid = uuid;
	datastore->id = trx_strdup(NULL, id);
	datastore->capacity = capacity;
	datastore->free_space = free_space;
	datastore->uncommitted = uncommitted;
	trx_vector_str_create(&datastore->hv_uuids);
out:
	trx_xml_free_doc(doc);

	if (NULL != error)
	{
		treegix_log(LOG_LEVEL_WARNING, "Cannot get Datastore info: %s.", error);
		trx_free(error);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return datastore;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_hv_data                                       *
 *                                                                            *
 * Purpose: gets the vmware hypervisor data                                   *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             hvid         - [IN] the vmware hypervisor id                   *
 *             propmap      - [IN] the xpaths of the properties to read       *
 *             props_num    - [IN] the number of properties to read           *
 *             xdoc         - [OUT] a reference to output xml document        *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_hv_data(const trx_vmware_service_t *service, CURL *easyhandle, const char *hvid,
		const trx_vmware_propmap_t *propmap, int props_num, xmlDoc **xdoc, char **error)
{
#	define TRX_POST_HV_DETAILS 								\
		TRX_POST_VSPHERE_HEADER								\
		"<ns0:RetrievePropertiesEx>"							\
			"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"			\
			"<ns0:specSet>"								\
				"<ns0:propSet>"							\
					"<ns0:type>HostSystem</ns0:type>"			\
					"<ns0:pathSet>vm</ns0:pathSet>"				\
					"<ns0:pathSet>parent</ns0:pathSet>"			\
					"<ns0:pathSet>datastore</ns0:pathSet>"			\
					"%s"							\
				"</ns0:propSet>"						\
				"<ns0:objectSet>"						\
					"<ns0:obj type=\"HostSystem\">%s</ns0:obj>"		\
				"</ns0:objectSet>"						\
			"</ns0:specSet>"							\
			"<ns0:options/>"							\
		"</ns0:RetrievePropertiesEx>"							\
		TRX_POST_VSPHERE_FOOTER

	char	tmp[MAX_STRING_LEN], props[MAX_STRING_LEN], *hvid_esc;
	int	i, ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() guesthvid:'%s'", __func__, hvid);
	props[0] = '\0';

	for (i = 0; i < props_num; i++)
	{
		trx_strlcat(props, "<ns0:pathSet>", sizeof(props));
		trx_strlcat(props, propmap[i].name, sizeof(props));
		trx_strlcat(props, "</ns0:pathSet>", sizeof(props));
	}

	hvid_esc = xml_escape_dyn(hvid);

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_HV_DETAILS,
			vmware_service_objects[service->type].property_collector, props, hvid_esc);

	trx_free(hvid_esc);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, xdoc, error))
		goto out;

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_hv_get_parent_data                                        *
 *                                                                            *
 * Purpose: gets the vmware hypervisor datacenter, parent folder or cluster   *
 *          name                                                              *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             hv           - [IN/OUT] the vmware hypervisor                  *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_hv_get_parent_data(const trx_vmware_service_t *service, CURL *easyhandle,
		trx_vmware_hv_t *hv, char **error)
{
#	define TRX_POST_HV_DATACENTER_NAME									\
		TRX_POST_VSPHERE_HEADER										\
			"<ns0:RetrievePropertiesEx>"								\
				"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"				\
				"<ns0:specSet>"									\
					"<ns0:propSet>"								\
						"<ns0:type>Datacenter</ns0:type>"				\
						"<ns0:pathSet>name</ns0:pathSet>"				\
					"</ns0:propSet>"							\
					"%s"									\
					"<ns0:objectSet>"							\
						"<ns0:obj type=\"HostSystem\">%s</ns0:obj>"			\
						"<ns0:skip>false</ns0:skip>"					\
						"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"		\
							"<ns0:name>parentObject</ns0:name>"			\
							"<ns0:type>HostSystem</ns0:type>"			\
							"<ns0:path>parent</ns0:path>"				\
							"<ns0:skip>false</ns0:skip>"				\
							"<ns0:selectSet>"					\
								"<ns0:name>parentComputeResource</ns0:name>"	\
							"</ns0:selectSet>"					\
							"<ns0:selectSet>"					\
								"<ns0:name>parentFolder</ns0:name>"		\
							"</ns0:selectSet>"					\
						"</ns0:selectSet>"						\
						"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"		\
							"<ns0:name>parentComputeResource</ns0:name>"		\
							"<ns0:type>ComputeResource</ns0:type>"			\
							"<ns0:path>parent</ns0:path>"				\
							"<ns0:skip>false</ns0:skip>"				\
							"<ns0:selectSet>"					\
								"<ns0:name>parentFolder</ns0:name>"		\
							"</ns0:selectSet>"					\
						"</ns0:selectSet>"						\
						"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"		\
							"<ns0:name>parentFolder</ns0:name>"			\
							"<ns0:type>Folder</ns0:type>"				\
							"<ns0:path>parent</ns0:path>"				\
							"<ns0:skip>false</ns0:skip>"				\
							"<ns0:selectSet>"					\
								"<ns0:name>parentFolder</ns0:name>"		\
							"</ns0:selectSet>"					\
							"<ns0:selectSet>"					\
								"<ns0:name>parentComputeResource</ns0:name>"	\
							"</ns0:selectSet>"					\
						"</ns0:selectSet>"						\
					"</ns0:objectSet>"							\
				"</ns0:specSet>"								\
				"<ns0:options/>"								\
			"</ns0:RetrievePropertiesEx>"								\
		TRX_POST_VSPHERE_FOOTER

#	define TRX_POST_SOAP_FOLDER										\
		"<ns0:propSet>"											\
			"<ns0:type>Folder</ns0:type>"								\
			"<ns0:pathSet>name</ns0:pathSet>"							\
			"<ns0:pathSet>parent</ns0:pathSet>"							\
			"<ns0:pathSet>childEntity</ns0:pathSet>"						\
		"</ns0:propSet>"										\
		"<ns0:propSet>"											\
			"<ns0:type>HostSystem</ns0:type>"							\
			"<ns0:pathSet>parent</ns0:pathSet>"							\
		"</ns0:propSet>"

#	define TRX_POST_SOAP_CUSTER										\
		"<ns0:propSet>"											\
			"<ns0:type>ClusterComputeResource</ns0:type>"						\
			"<ns0:pathSet>name</ns0:pathSet>"							\
		"</ns0:propSet>"

	char	tmp[MAX_STRING_LEN];
	int	ret = FAIL;
	xmlDoc	*doc = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() id:'%s'", __func__, hv->id);

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_HV_DATACENTER_NAME,
			vmware_service_objects[service->type].property_collector,
			NULL != hv->clusterid ? TRX_POST_SOAP_CUSTER : TRX_POST_SOAP_FOLDER, hv->id);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, &doc, error))
		goto out;

	if (NULL == (hv->datacenter_name = trx_xml_read_doc_value(doc,
			TRX_XPATH_NAME_BY_TYPE(TRX_VMWARE_SOAP_DATACENTER))))
	{
		hv->datacenter_name = trx_strdup(NULL, "");
	}

	if (NULL != hv->clusterid && (NULL != (hv->parent_name = trx_xml_read_doc_value(doc,
			TRX_XPATH_NAME_BY_TYPE(TRX_VMWARE_SOAP_CLUSTER)))))
	{
		hv->parent_type = trx_strdup(NULL, TRX_VMWARE_SOAP_CLUSTER);
	}
	else if (NULL != (hv->parent_name = trx_xml_read_doc_value(doc,
			TRX_XPATH_HV_PARENTFOLDERNAME(TRX_XPATH_HV_PARENTID))))
	{
		hv->parent_type = trx_strdup(NULL, TRX_VMWARE_SOAP_FOLDER);
	}
	else if ('\0' != *hv->datacenter_name)
	{
		hv->parent_name = trx_strdup(NULL, hv->datacenter_name);
		hv->parent_type = trx_strdup(NULL, TRX_VMWARE_SOAP_DATACENTER);
	}
	else
	{
		hv->parent_name = trx_strdup(NULL, TRX_VMWARE_TYPE_VCENTER == service->type ? "Vcenter" : "ESXi");
		hv->parent_type = trx_strdup(NULL, TRX_VMWARE_SOAP_DEFAULT);
	}

	ret = SUCCEED;
out:
	trx_xml_free_doc(doc);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_ds_name_compare                                           *
 *                                                                            *
 * Purpose: sorting function to sort Datastore vector by name                 *
 *                                                                            *
 ******************************************************************************/
int	vmware_ds_name_compare(const void *d1, const void *d2)
{
	const trx_vmware_datastore_t	*ds1 = *(const trx_vmware_datastore_t **)d1;
	const trx_vmware_datastore_t	*ds2 = *(const trx_vmware_datastore_t **)d2;

	return strcmp(ds1->name, ds2->name);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_ds_id_compare                                             *
 *                                                                            *
 * Purpose: sorting function to sort Datastore vector by id                   *
 *                                                                            *
 ******************************************************************************/
static int	vmware_ds_id_compare(const void *d1, const void *d2)
{
	const trx_vmware_datastore_t	*ds1 = *(const trx_vmware_datastore_t **)d1;
	const trx_vmware_datastore_t	*ds2 = *(const trx_vmware_datastore_t **)d2;

	return strcmp(ds1->id, ds2->id);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_init_hv                                           *
 *                                                                            *
 * Purpose: initialize vmware hypervisor object                               *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             id           - [IN] the vmware hypervisor id                   *
 *             dss          - [IN/OUT] the vector with all Datastores         *
 *             hv           - [OUT] the hypervisor object (must be allocated) *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the hypervisor object was initialized successfully *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_init_hv(trx_vmware_service_t *service, CURL *easyhandle, const char *id,
		trx_vector_vmware_datastore_t *dss, trx_vmware_hv_t *hv, char **error)
{
	char			*value;
	xmlDoc			*details = NULL;
	trx_vector_str_t	datastores, vms;
	int			i, j, ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() hvid:'%s'", __func__, id);

	memset(hv, 0, sizeof(trx_vmware_hv_t));

	trx_vector_str_create(&hv->ds_names);
	trx_vector_ptr_create(&hv->vms);

	trx_vector_str_create(&datastores);
	trx_vector_str_create(&vms);

	if (SUCCEED != vmware_service_get_hv_data(service, easyhandle, id, hv_propmap,
			TRX_VMWARE_HVPROPS_NUM, &details, error))
	{
		goto out;
	}

	if (NULL == (hv->props = xml_read_props(details, hv_propmap, TRX_VMWARE_HVPROPS_NUM)))
		goto out;

	if (NULL == hv->props[TRX_VMWARE_HVPROP_HW_UUID])
		goto out;

	hv->uuid = trx_strdup(NULL, hv->props[TRX_VMWARE_HVPROP_HW_UUID]);
	hv->id = trx_strdup(NULL, id);

	if (NULL != (value = trx_xml_read_doc_value(details, "//*[@type='" TRX_VMWARE_SOAP_CLUSTER "']")))
		hv->clusterid = value;

	if (SUCCEED != vmware_hv_get_parent_data(service, easyhandle, hv, error))
		goto out;

	trx_xml_read_values(details, TRX_XPATH_HV_DATASTORES(), &datastores);
	trx_vector_str_reserve(&hv->ds_names, datastores.values_num);

	for (i = 0; i < datastores.values_num; i++)
	{
		trx_vmware_datastore_t *ds;
		trx_vmware_datastore_t ds_cmp;

		ds_cmp.id = datastores.values[i];

		if (FAIL == (j = trx_vector_vmware_datastore_bsearch(dss, &ds_cmp, vmware_ds_id_compare)))
		{
			treegix_log(LOG_LEVEL_DEBUG, "%s(): Datastore \"%s\" not found on hypervisor \"%s\".", __func__,
					datastores.values[i], hv->id);
			continue;
		}

		ds = dss->values[j];
		trx_vector_str_append(&ds->hv_uuids, trx_strdup(NULL, hv->uuid));
		trx_vector_str_append(&hv->ds_names, trx_strdup(NULL, ds->name));
	}

	trx_vector_str_sort(&hv->ds_names, TRX_DEFAULT_STR_COMPARE_FUNC);
	trx_xml_read_values(details, TRX_XPATH_HV_VMS(), &vms);
	trx_vector_ptr_reserve(&hv->vms, vms.values_num + hv->vms.values_alloc);

	for (i = 0; i < vms.values_num; i++)
	{
		trx_vmware_vm_t	*vm;

		if (NULL != (vm = vmware_service_create_vm(service, easyhandle, vms.values[i], error)))
			trx_vector_ptr_append(&hv->vms, vm);
	}

	ret = SUCCEED;
out:
	trx_xml_free_doc(details);

	trx_vector_str_clear_ext(&vms, trx_str_free);
	trx_vector_str_destroy(&vms);

	trx_vector_str_clear_ext(&datastores, trx_str_free);
	trx_vector_str_destroy(&datastores);

	if (SUCCEED != ret)
		vmware_hv_clean(hv);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_hv_ds_list                                    *
 *                                                                            *
 * Purpose: retrieves a list of all vmware service hypervisor ids             *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             hvs          - [OUT] list of vmware hypervisor ids             *
 *             dss          - [OUT] list of vmware datastore ids              *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_hv_ds_list(const trx_vmware_service_t *service, CURL *easyhandle,
		trx_vector_str_t *hvs, trx_vector_str_t *dss, char **error)
{
#	define TRX_POST_VCENTER_HV_DS_LIST							\
		TRX_POST_VSPHERE_HEADER								\
		"<ns0:RetrievePropertiesEx xsi:type=\"ns0:RetrievePropertiesExRequestType\">"	\
			"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"			\
			"<ns0:specSet>"								\
				"<ns0:propSet>"							\
					"<ns0:type>HostSystem</ns0:type>"			\
				"</ns0:propSet>"						\
				"<ns0:propSet>"							\
					"<ns0:type>Datastore</ns0:type>"			\
				"</ns0:propSet>"						\
				"<ns0:objectSet>"						\
					"<ns0:obj type=\"Folder\">%s</ns0:obj>"			\
					"<ns0:skip>false</ns0:skip>"				\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>visitFolders</ns0:name>"		\
						"<ns0:type>Folder</ns0:type>"			\
						"<ns0:path>childEntity</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>dcToHf</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>dcToVmf</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>crToH</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>crToRp</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>dcToDs</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>hToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>dcToVmf</ns0:name>"			\
						"<ns0:type>Datacenter</ns0:type>"		\
						"<ns0:path>vmFolder</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>dcToDs</ns0:name>"			\
						"<ns0:type>Datacenter</ns0:type>"		\
						"<ns0:path>datastore</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>dcToHf</ns0:name>"			\
						"<ns0:type>Datacenter</ns0:type>"		\
						"<ns0:path>hostFolder</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>crToH</ns0:name>"			\
						"<ns0:type>ComputeResource</ns0:type>"		\
						"<ns0:path>host</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>crToRp</ns0:name>"			\
						"<ns0:type>ComputeResource</ns0:type>"		\
						"<ns0:path>resourcePool</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToRp</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>rpToRp</ns0:name>"			\
						"<ns0:type>ResourcePool</ns0:type>"		\
						"<ns0:path>resourcePool</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToRp</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>hToVm</ns0:name>"			\
						"<ns0:type>HostSystem</ns0:type>"		\
						"<ns0:path>vm</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>rpToVm</ns0:name>"			\
						"<ns0:type>ResourcePool</ns0:type>"		\
						"<ns0:path>vm</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
					"</ns0:selectSet>"					\
				"</ns0:objectSet>"						\
			"</ns0:specSet>"							\
			"<ns0:options/>"							\
		"</ns0:RetrievePropertiesEx>"							\
		TRX_POST_VSPHERE_FOOTER

	char				tmp[MAX_STRING_LEN * 2];
	int				ret = FAIL;
	xmlDoc				*doc = NULL;
	trx_property_collection_iter	*iter = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_VCENTER_HV_DS_LIST,
			vmware_service_objects[service->type].property_collector,
			vmware_service_objects[service->type].root_folder);

	if (SUCCEED != trx_property_collection_init(easyhandle, tmp, "propertyCollector", &iter, &doc, error))
	{
		goto out;
	}

	if (TRX_VMWARE_TYPE_VCENTER == service->type)
		trx_xml_read_values(doc, "//*[@type='HostSystem']", hvs);
	else
		trx_vector_str_append(hvs, trx_strdup(NULL, "ha-host"));

	trx_xml_read_values(doc, "//*[@type='Datastore']", dss);

	while (NULL != iter->token)
	{
		trx_xml_free_doc(doc);
		doc = NULL;

		if (SUCCEED != trx_property_collection_next(iter, &doc, error))
			goto out;

		if (TRX_VMWARE_TYPE_VCENTER == service->type)
			trx_xml_read_values(doc, "//*[@type='HostSystem']", hvs);

		trx_xml_read_values(doc, "//*[@type='Datastore']", dss);
	}

	ret = SUCCEED;
out:
	trx_property_collection_free(iter);
	trx_xml_free_doc(doc);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s found hv:%d ds:%d", __func__, trx_result_string(ret),
			hvs->values_num, dss->values_num);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_event_session                                 *
 *                                                                            *
 * Purpose: retrieves event session name                                      *
 *                                                                            *
 * Parameters: service        - [IN] the vmware service                       *
 *             easyhandle     - [IN] the CURL handle                          *
 *             event_session  - [OUT] a pointer to the output variable        *
 *             error          - [OUT] the error message in the case of failure*
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_event_session(const trx_vmware_service_t *service, CURL *easyhandle,
		char **event_session, char **error)
{
#	define TRX_POST_VMWARE_CREATE_EVENT_COLLECTOR				\
		TRX_POST_VSPHERE_HEADER						\
		"<ns0:CreateCollectorForEvents>"				\
			"<ns0:_this type=\"EventManager\">%s</ns0:_this>"	\
			"<ns0:filter/>"						\
		"</ns0:CreateCollectorForEvents>"				\
		TRX_POST_VSPHERE_FOOTER

	char	tmp[MAX_STRING_LEN];
	int	ret = FAIL;
	xmlDoc	*doc = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_VMWARE_CREATE_EVENT_COLLECTOR,
			vmware_service_objects[service->type].event_manager);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, &doc, error))
		goto out;

	if (NULL == (*event_session = trx_xml_read_doc_value(doc, "/*/*/*/*[@type='EventHistoryCollector']")))
	{
		*error = trx_strdup(*error, "Cannot get EventHistoryCollector session.");
		goto out;
	}

	ret = SUCCEED;
out:
	trx_xml_free_doc(doc);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s event_session:'%s'", __func__, trx_result_string(ret),
			TRX_NULL2EMPTY_STR(*event_session));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_reset_event_history_collector                     *
 *                                                                            *
 * Purpose: resets "scrollable view" to the latest events                     *
 *                                                                            *
 * Parameters: easyhandle     - [IN] the CURL handle                          *
 *             event_session  - [IN] event session (EventHistoryCollector)    *
 *                                   identifier                               *
 *             error          - [OUT] the error message in the case of failure*
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_reset_event_history_collector(CURL *easyhandle, const char *event_session, char **error)
{
#	define TRX_POST_VMWARE_RESET_EVENT_COLLECTOR					\
		TRX_POST_VSPHERE_HEADER							\
		"<ns0:ResetCollector>"							\
			"<ns0:_this type=\"EventHistoryCollector\">%s</ns0:_this>"	\
		"</ns0:ResetCollector>"							\
		TRX_POST_VSPHERE_FOOTER

	int		ret = FAIL;
	char		tmp[MAX_STRING_LEN], *event_session_esc;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	event_session_esc = xml_escape_dyn(event_session);

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_VMWARE_RESET_EVENT_COLLECTOR, event_session_esc);

	trx_free(event_session_esc);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, NULL, error))
		goto out;

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;

#	undef TRX_POST_VMWARE_DESTROY_EVENT_COLLECTOR
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_read_previous_events                              *
 *                                                                            *
 * Purpose: reads events from "scrollable view" and moves it back in time     *
 *                                                                            *
 * Parameters: easyhandle     - [IN] the CURL handle                          *
 *             event_session  - [IN] event session (EventHistoryCollector)    *
 *                                   identifier                               *
 *             soap_count     - [IN] max count of events in response          *
 *             xdoc           - [OUT] the result as xml document              *
 *             error          - [OUT] the error message in the case of failure*
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_read_previous_events(CURL *easyhandle, const char *event_session, int soap_count,
		xmlDoc **xdoc, char **error)
{
#	define TRX_POST_VMWARE_READ_PREVIOUS_EVENTS					\
		TRX_POST_VSPHERE_HEADER							\
		"<ns0:ReadPreviousEvents>"						\
			"<ns0:_this type=\"EventHistoryCollector\">%s</ns0:_this>"	\
			"<ns0:maxCount>%d</ns0:maxCount>"				\
		"</ns0:ReadPreviousEvents>"						\
		TRX_POST_VSPHERE_FOOTER

	int	ret = FAIL;
	char	tmp[MAX_STRING_LEN], *event_session_esc;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() soap_count: %d", __func__, soap_count);

	event_session_esc = xml_escape_dyn(event_session);

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_VMWARE_READ_PREVIOUS_EVENTS, event_session_esc, soap_count);

	trx_free(event_session_esc);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, xdoc, error))
		goto out;

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_destroy_event_session                             *
 *                                                                            *
 * Purpose: destroys event session                                            *
 *                                                                            *
 * Parameters: easyhandle     - [IN] the CURL handle                          *
 *             event_session  - [IN] event session (EventHistoryCollector)    *
 *                                   identifier                               *
 *             error          - [OUT] the error message in the case of failure*
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_destroy_event_session(CURL *easyhandle, const char *event_session, char **error)
{
#	define TRX_POST_VMWARE_DESTROY_EVENT_COLLECTOR					\
		TRX_POST_VSPHERE_HEADER							\
		"<ns0:DestroyCollector>"						\
			"<ns0:_this type=\"EventHistoryCollector\">%s</ns0:_this>"	\
		"</ns0:DestroyCollector>"						\
		TRX_POST_VSPHERE_FOOTER

	int	ret = FAIL;
	char	tmp[MAX_STRING_LEN], *event_session_esc;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	event_session_esc = xml_escape_dyn(event_session);

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_VMWARE_DESTROY_EVENT_COLLECTOR, event_session_esc);

	trx_free(event_session_esc);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, NULL, error))
		goto out;

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_put_event_data                                    *
 *                                                                            *
 * Purpose: read event data by id from xml and put to array of events         *
 *                                                                            *
 * Parameters: events    - [IN/OUT] the array of parsed events                *
 *             xml_event - [IN] the xml node and id of parsed event           *
 *             xdoc      - [IN] xml document with eventlog records            *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 ******************************************************************************/
static int	vmware_service_put_event_data(trx_vector_ptr_t *events, trx_id_xmlnode_t xml_event, xmlDoc *xdoc)
{
	trx_vmware_event_t	*event = NULL;
	char			*message, *time_str;
	int			timestamp = 0;

	if (NULL == (message = trx_xml_read_node_value(xdoc, xml_event.xml_node, TRX_XPATH_NN("fullFormattedMessage"))))
	{
		treegix_log(LOG_LEVEL_TRACE, "skipping event key '" TRX_FS_UI64 "', fullFormattedMessage"
				" is missing", xml_event.id);
		return FAIL;
	}

	trx_replace_invalid_utf8(message);

	if (NULL == (time_str = trx_xml_read_node_value(xdoc, xml_event.xml_node, TRX_XPATH_NN("createdTime"))))
	{
		treegix_log(LOG_LEVEL_TRACE, "createdTime is missing for event key '" TRX_FS_UI64 "'", xml_event.id);
	}
	else
	{
		int	year, mon, mday, hour, min, sec, t;

		/* 2013-06-04T14:19:23.406298Z */
		if (6 != sscanf(time_str, "%d-%d-%dT%d:%d:%d.%*s", &year, &mon, &mday, &hour, &min, &sec))
		{
			treegix_log(LOG_LEVEL_TRACE, "unexpected format of createdTime '%s' for event"
					" key '" TRX_FS_UI64 "'", time_str, xml_event.id);
		}
		else if (SUCCEED != trx_utc_time(year, mon, mday, hour, min, sec, &t))
		{
			treegix_log(LOG_LEVEL_TRACE, "cannot convert createdTime '%s' for event key '"
					TRX_FS_UI64 "'", time_str, xml_event.id);
		}
		else
			timestamp = t;

		trx_free(time_str);
	}

	event = (trx_vmware_event_t *)trx_malloc(event, sizeof(trx_vmware_event_t));
	event->key = xml_event.id;
	event->message = message;
	event->timestamp = timestamp;
	trx_vector_ptr_append(events, event);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_parse_event_data                                  *
 *                                                                            *
 * Purpose: parse multiple events data                                        *
 *                                                                            *
 * Parameters: events   - [IN/OUT] the array of parsed events                 *
 *             last_key - [IN] the key of last parsed event                   *
 *             xdoc     - [IN] xml document with eventlog records             *
 *                                                                            *
 * Return value: The count of events successfully parsed                      *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_parse_event_data(trx_vector_ptr_t *events, trx_uint64_t last_key, xmlDoc *xdoc)
{
	trx_vector_id_xmlnode_t	ids;
	int			i, parsed_num = 0;
	char			*value;
	xmlXPathContext		*xpathCtx;
	xmlXPathObject		*xpathObj;
	xmlNodeSetPtr		nodeset;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() last_key:" TRX_FS_UI64, __func__, last_key);

	xpathCtx = xmlXPathNewContext(xdoc);

	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)"/*/*/*"TRX_XPATH_LN("returnval"), xpathCtx)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "Cannot make evenlog list parsing query.");
		goto clean;
	}

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
	{
		treegix_log(LOG_LEVEL_DEBUG, "Cannot find items in evenlog list.");
		goto clean;
	}

	nodeset = xpathObj->nodesetval;
	trx_vector_id_xmlnode_create(&ids);
	trx_vector_id_xmlnode_reserve(&ids, nodeset->nodeNr);

	for (i = 0; i < nodeset->nodeNr; i++)
	{
		trx_id_xmlnode_t	xml_event;
		trx_uint64_t		key;

		if (NULL == (value = trx_xml_read_node_value(xdoc, nodeset->nodeTab[i], TRX_XPATH_NN("key"))))
		{
			treegix_log(LOG_LEVEL_TRACE, "skipping eventlog record without key, xml number '%d'", i);
			continue;
		}

		if (SUCCEED != is_uint64(value, &key))
		{
			treegix_log(LOG_LEVEL_TRACE, "skipping eventlog key '%s', not a number", value);
			trx_free(value);
			continue;
		}

		trx_free(value);

		if (key <= last_key)
		{
			treegix_log(LOG_LEVEL_TRACE, "skipping event key '" TRX_FS_UI64 "', has been processed", key);
			continue;
		}

		xml_event.id = key;
		xml_event.xml_node = nodeset->nodeTab[i];
		trx_vector_id_xmlnode_append(&ids, xml_event);
	}

	if (0 != ids.values_num)
	{
		trx_vector_id_xmlnode_sort(&ids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
		trx_vector_ptr_reserve(events, ids.values_num + events->values_alloc);

		/* we are reading "scrollable views" in reverse chronological order, */
		/* so inside a "scrollable view" latest events should come first too */
		for (i = ids.values_num - 1; i >= 0; i--)
		{
			if (SUCCEED == vmware_service_put_event_data(events, ids.values[i], xdoc))
				parsed_num++;
		}
	}

	trx_vector_id_xmlnode_destroy(&ids);
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() parsed:%d", __func__, parsed_num);

	return parsed_num;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_event_data                                    *
 *                                                                            *
 * Purpose: retrieves event data                                              *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             events       - [OUT] a pointer to the output variable          *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_event_data(const trx_vmware_service_t *service, CURL *easyhandle,
		trx_vector_ptr_t *events, char **error)
{
	char		*event_session = NULL;
	int		ret = FAIL, soap_count = 5; /* 10 - initial value of eventlog records number in one response */
	xmlDoc		*doc = NULL;
	trx_uint64_t	eventlog_last_key;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != vmware_service_get_event_session(service, easyhandle, &event_session, error))
		goto out;

	if (SUCCEED != vmware_service_reset_event_history_collector(easyhandle, event_session, error))
		goto end_session;

	if (NULL != service->data && 0 != service->data->events.values_num &&
			((const trx_vmware_event_t *)service->data->events.values[0])->key > service->eventlog.last_key)
	{
		eventlog_last_key = ((const trx_vmware_event_t *)service->data->events.values[0])->key;
	}
	else
		eventlog_last_key = service->eventlog.last_key;

	do
	{
		trx_xml_free_doc(doc);
		doc = NULL;

		if ((TRX_MAXQUERYMETRICS_UNLIMITED / 2) >= soap_count)
			soap_count = soap_count * 2;
		else if (TRX_MAXQUERYMETRICS_UNLIMITED != soap_count)
			soap_count = TRX_MAXQUERYMETRICS_UNLIMITED;

		if (0 != events->values_num &&
				(((const trx_vmware_event_t *)events->values[events->values_num - 1])->key -
				eventlog_last_key -1) < (unsigned int)soap_count)
		{
			soap_count = ((const trx_vmware_event_t *)events->values[events->values_num - 1])->key -
					eventlog_last_key - 1;
		}

		if (0 < soap_count && SUCCEED != vmware_service_read_previous_events(easyhandle, event_session,
				soap_count, &doc, error))
		{
			goto end_session;
		}
	}
	while (0 < vmware_service_parse_event_data(events, eventlog_last_key, doc));

	ret = SUCCEED;
end_session:
	if (SUCCEED != vmware_service_destroy_event_session(easyhandle, event_session, error))
		ret = FAIL;
out:
	trx_free(event_session);
	trx_xml_free_doc(doc);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_last_event_data                               *
 *                                                                            *
 * Purpose: retrieves data only last event                                    *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             events       - [OUT] a pointer to the output variable          *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_last_event_data(const trx_vmware_service_t *service, CURL *easyhandle,
		trx_vector_ptr_t *events, char **error)
{
#	define TRX_POST_VMWARE_LASTEVENT 								\
		TRX_POST_VSPHERE_HEADER									\
		"<ns0:RetrievePropertiesEx>"								\
			"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"				\
			"<ns0:specSet>"									\
				"<ns0:propSet>"								\
					"<ns0:type>EventManager</ns0:type>"				\
					"<ns0:all>false</ns0:all>"					\
					"<ns0:pathSet>latestEvent</ns0:pathSet>"			\
				"</ns0:propSet>"							\
				"<ns0:objectSet>"							\
					"<ns0:obj type=\"EventManager\">%s</ns0:obj>"			\
				"</ns0:objectSet>"							\
			"</ns0:specSet>"								\
			"<ns0:options/>"								\
		"</ns0:RetrievePropertiesEx>"								\
		TRX_POST_VSPHERE_FOOTER

	char			tmp[MAX_STRING_LEN], *value;
	int			ret = FAIL;
	xmlDoc			*doc = NULL;
	trx_id_xmlnode_t	xml_event;
	xmlXPathContext		*xpathCtx;
	xmlXPathObject		*xpathObj;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_VMWARE_LASTEVENT,
			vmware_service_objects[service->type].property_collector,
			vmware_service_objects[service->type].event_manager);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, &doc, error))
		goto out;

	xpathCtx = xmlXPathNewContext(doc);

	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)TRX_XPATH_PROP_NAME("latestEvent"), xpathCtx)))
	{
		*error = trx_strdup(*error, "Cannot make lastevenlog list parsing query.");
		goto clean;
	}

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
	{
		*error = trx_strdup(*error, "Cannot find items in lastevenlog list.");
		goto clean;
	}

	xml_event.xml_node = xpathObj->nodesetval->nodeTab[0];

	if (NULL == (value = trx_xml_read_node_value(doc, xml_event.xml_node, TRX_XPATH_NN("key"))))
	{
		*error = trx_strdup(*error, "Cannot find last event key");
		goto clean;
	}

	if (SUCCEED != is_uint64(value, &xml_event.id))
	{
		*error = trx_dsprintf(*error, "Cannot convert eventlog key from %s", value);
		trx_free(value);
		goto clean;
	}

	trx_free(value);

	if (SUCCEED != vmware_service_put_event_data(events, xml_event, doc))
	{
		*error = trx_dsprintf(*error, "Cannot retrieve last eventlog data for key "TRX_FS_UI64, xml_event.id);
		goto clean;
	}

	ret = SUCCEED;
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);
out:
	trx_xml_free_doc(doc);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;

#	undef TRX_POST_VMWARE_LASTEVENT
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_clusters                                      *
 *                                                                            *
 * Purpose: retrieves a list of vmware service clusters                       *
 *                                                                            *
 * Parameters: easyhandle   - [IN] the CURL handle                            *
 *             clusters     - [OUT] a pointer to the output variable          *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_clusters(CURL *easyhandle, xmlDoc **clusters, char **error)
{
#	define TRX_POST_VCENTER_CLUSTER								\
		TRX_POST_VSPHERE_HEADER								\
		"<ns0:RetrievePropertiesEx xsi:type=\"ns0:RetrievePropertiesExRequestType\">"	\
			"<ns0:_this type=\"PropertyCollector\">propertyCollector</ns0:_this>"	\
			"<ns0:specSet>"								\
				"<ns0:propSet>"							\
					"<ns0:type>ClusterComputeResource</ns0:type>"		\
					"<ns0:pathSet>name</ns0:pathSet>"			\
				"</ns0:propSet>"						\
				"<ns0:objectSet>"						\
					"<ns0:obj type=\"Folder\">group-d1</ns0:obj>"		\
					"<ns0:skip>false</ns0:skip>"				\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>visitFolders</ns0:name>"		\
						"<ns0:type>Folder</ns0:type>"			\
						"<ns0:path>childEntity</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>dcToHf</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>dcToVmf</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>crToH</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>crToRp</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>dcToDs</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>hToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>dcToVmf</ns0:name>"			\
						"<ns0:type>Datacenter</ns0:type>"		\
						"<ns0:path>vmFolder</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>dcToDs</ns0:name>"			\
						"<ns0:type>Datacenter</ns0:type>"		\
						"<ns0:path>datastore</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>dcToHf</ns0:name>"			\
						"<ns0:type>Datacenter</ns0:type>"		\
						"<ns0:path>hostFolder</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>crToH</ns0:name>"			\
						"<ns0:type>ComputeResource</ns0:type>"		\
						"<ns0:path>host</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>crToRp</ns0:name>"			\
						"<ns0:type>ComputeResource</ns0:type>"		\
						"<ns0:path>resourcePool</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToRp</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>rpToRp</ns0:name>"			\
						"<ns0:type>ResourcePool</ns0:type>"		\
						"<ns0:path>resourcePool</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToRp</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>hToVm</ns0:name>"			\
						"<ns0:type>HostSystem</ns0:type>"		\
						"<ns0:path>vm</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>rpToVm</ns0:name>"			\
						"<ns0:type>ResourcePool</ns0:type>"		\
						"<ns0:path>vm</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
					"</ns0:selectSet>"					\
				"</ns0:objectSet>"						\
			"</ns0:specSet>"							\
			"<ns0:options/>"							\
		"</ns0:RetrievePropertiesEx>"							\
		TRX_POST_VSPHERE_FOOTER

	int	ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, TRX_POST_VCENTER_CLUSTER, clusters, error))
		goto out;

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;

#	undef TRX_POST_VCENTER_CLUSTER
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_cluster_status                                *
 *                                                                            *
 * Purpose: retrieves status of the specified vmware cluster                  *
 *                                                                            *
 * Parameters: easyhandle   - [IN] the CURL handle                            *
 *             clusterid    - [IN] the cluster id                             *
 *             status       - [OUT] a pointer to the output variable          *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_cluster_status(CURL *easyhandle, const char *clusterid, char **status, char **error)
{
#	define TRX_POST_VMWARE_CLUSTER_STATUS 								\
		TRX_POST_VSPHERE_HEADER									\
		"<ns0:RetrievePropertiesEx>"								\
			"<ns0:_this type=\"PropertyCollector\">propertyCollector</ns0:_this>"		\
			"<ns0:specSet>"									\
				"<ns0:propSet>"								\
					"<ns0:type>ClusterComputeResource</ns0:type>"			\
					"<ns0:all>false</ns0:all>"					\
					"<ns0:pathSet>summary.overallStatus</ns0:pathSet>"		\
				"</ns0:propSet>"							\
				"<ns0:objectSet>"							\
					"<ns0:obj type=\"ClusterComputeResource\">%s</ns0:obj>"		\
				"</ns0:objectSet>"							\
			"</ns0:specSet>"								\
			"<ns0:options></ns0:options>"							\
		"</ns0:RetrievePropertiesEx>"								\
		TRX_POST_VSPHERE_FOOTER

	char	tmp[MAX_STRING_LEN], *clusterid_esc;
	int	ret = FAIL;
	xmlDoc	*doc = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() clusterid:'%s'", __func__, clusterid);

	clusterid_esc = xml_escape_dyn(clusterid);

	trx_snprintf(tmp, sizeof(tmp), TRX_POST_VMWARE_CLUSTER_STATUS, clusterid_esc);

	trx_free(clusterid_esc);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, &doc, error))
		goto out;

	*status = trx_xml_read_doc_value(doc, TRX_XPATH_PROP_NAME("summary.overallStatus"));

	ret = SUCCEED;
out:
	trx_xml_free_doc(doc);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;

#	undef TRX_POST_VMWARE_CLUSTER_STATUS
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_cluster_list                                  *
 *                                                                            *
 * Purpose: creates list of vmware cluster objects                            *
 *                                                                            *
 * Parameters: easyhandle   - [IN] the CURL handle                            *
 *             clusters     - [OUT] a pointer to the resulting cluster vector *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_cluster_list(CURL *easyhandle, trx_vector_ptr_t *clusters, char **error)
{
	char			xpath[MAX_STRING_LEN], *name;
	xmlDoc			*cluster_data = NULL;
	trx_vector_str_t	ids;
	trx_vmware_cluster_t	*cluster;
	int			i, ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_str_create(&ids);

	if (SUCCEED != vmware_service_get_clusters(easyhandle, &cluster_data, error))
		goto out;

	trx_xml_read_values(cluster_data, "//*[@type='ClusterComputeResource']", &ids);
	trx_vector_ptr_reserve(clusters, ids.values_num + clusters->values_alloc);

	for (i = 0; i < ids.values_num; i++)
	{
		char	*status;

		trx_snprintf(xpath, sizeof(xpath), "//*[@type='ClusterComputeResource'][.='%s']"
				"/.." TRX_XPATH_LN2("propSet", "val"), ids.values[i]);

		if (NULL == (name = trx_xml_read_doc_value(cluster_data, xpath)))
			continue;

		if (SUCCEED != vmware_service_get_cluster_status(easyhandle, ids.values[i], &status, error))
		{
			trx_free(name);
			goto out;
		}

		cluster = (trx_vmware_cluster_t *)trx_malloc(NULL, sizeof(trx_vmware_cluster_t));
		cluster->id = trx_strdup(NULL, ids.values[i]);
		cluster->name = name;
		cluster->status = status;

		trx_vector_ptr_append(clusters, cluster);
	}

	ret = SUCCEED;
out:
	trx_xml_free_doc(cluster_data);
	trx_vector_str_clear_ext(&ids, trx_str_free);
	trx_vector_str_destroy(&ids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s found:%d", __func__, trx_result_string(ret),
			clusters->values_num);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_maxquerymetrics                               *
 *                                                                            *
 * Purpose: get vpxd.stats.maxquerymetrics parameter from vcenter only        *
 *                                                                            *
 * Parameters: easyhandle   - [IN] the CURL handle                            *
 *             max_qm       - [OUT] max count of Datastore metrics in one     *
 *                                  request                                   *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_maxquerymetrics(CURL *easyhandle, int *max_qm, char **error)
{
#	define TRX_POST_MAXQUERYMETRICS								\
		TRX_POST_VSPHERE_HEADER								\
		"<ns0:QueryOptions>"								\
			"<ns0:_this type=\"OptionManager\">VpxSettings</ns0:_this>"		\
			"<ns0:name>config.vpxd.stats.maxQueryMetrics</ns0:name>"		\
		"</ns0:QueryOptions>"								\
		TRX_POST_VSPHERE_FOOTER

	int	ret = FAIL;
	char	*val;
	xmlDoc	*doc = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != trx_soap_post(__func__, easyhandle, TRX_POST_MAXQUERYMETRICS, &doc, error))
	{
		if (NULL == doc)	/* if not SOAP error */
			goto out;

		treegix_log(LOG_LEVEL_WARNING, "Error of query maxQueryMetrics: %s.", *error);
		trx_free(*error);
	}

	ret = SUCCEED;

	if (NULL == (val = trx_xml_read_doc_value(doc, TRX_XPATH_MAXQUERYMETRICS())))
	{
		*max_qm = TRX_VPXD_STATS_MAXQUERYMETRICS;
		treegix_log(LOG_LEVEL_DEBUG, "maxQueryMetrics used default value %d", TRX_VPXD_STATS_MAXQUERYMETRICS);
		goto out;
	}

	/* vmware article 2107096                                                                    */
	/* Edit the config.vpxd.stats.maxQueryMetrics key in the advanced settings of vCenter Server */
	/* To disable the limit, set a value to -1                                                   */
	/* Edit the web.xml file. To disable the limit, set a value 0                                */
	if (-1 == atoi(val))
	{
		*max_qm = TRX_MAXQUERYMETRICS_UNLIMITED;
	}
	else if (SUCCEED != is_uint31(val, max_qm))
	{
		treegix_log(LOG_LEVEL_DEBUG, "Cannot convert maxQueryMetrics from %s.", val);
		*max_qm = TRX_VPXD_STATS_MAXQUERYMETRICS;
	}
	else if (0 == *max_qm)
	{
		*max_qm = TRX_MAXQUERYMETRICS_UNLIMITED;
	}

	trx_free(val);
out:
	trx_xml_free_doc(doc);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}
/******************************************************************************
 *                                                                            *
 * Function: vmware_counters_add_new                                          *
 *                                                                            *
 * Purpose: creates a new performance counter object in shared memory and     *
 *          adds to the specified vector                                      *
 *                                                                            *
 * Parameters: counters  - [IN/OUT] the vector the created performance        *
 *                                  counter object should be added to         *
 *             counterid - [IN] the performance counter id                    *
 *                                                                            *
 ******************************************************************************/
static void	vmware_counters_add_new(trx_vector_ptr_t *counters, trx_uint64_t counterid)
{
	trx_vmware_perf_counter_t	*counter;

	counter = (trx_vmware_perf_counter_t *)__vm_mem_malloc_func(NULL, sizeof(trx_vmware_perf_counter_t));
	counter->counterid = counterid;
	counter->state = TRX_VMWARE_COUNTER_NEW;

	trx_vector_str_uint64_pair_create_ext(&counter->values, __vm_mem_malloc_func, __vm_mem_realloc_func,
			__vm_mem_free_func);

	trx_vector_ptr_append(counters, counter);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_initialize                                        *
 *                                                                            *
 * Purpose: initializes vmware service object                                 *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 * Comments: While the service object can't be accessed from other processes  *
 *           during initialization it's still processed outside vmware locks  *
 *           and therefore must not allocate/free shared memory.              *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_initialize(trx_vmware_service_t *service, CURL *easyhandle, char **error)
{
	char			*version = NULL, *fullname = NULL;
	trx_vector_ptr_t	counters;
	int			ret = FAIL;

	trx_vector_ptr_create(&counters);

	if (SUCCEED != vmware_service_get_perf_counters(service, easyhandle, &counters, error))
		goto out;

	if (SUCCEED != vmware_service_get_contents(easyhandle, &version, &fullname, error))
		goto out;

	trx_vmware_lock();

	service->version = vmware_shared_strdup(version);
	service->fullname = vmware_shared_strdup(fullname);
	vmware_counters_shared_copy(&service->counters, &counters);

	trx_vmware_unlock();

	ret = SUCCEED;
out:
	trx_free(version);
	trx_free(fullname);

	trx_vector_ptr_clear_ext(&counters, (trx_mem_free_func_t)vmware_counter_free);
	trx_vector_ptr_destroy(&counters);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_add_perf_entity                                   *
 *                                                                            *
 * Purpose: adds entity to vmware service performance entity list             *
 *                                                                            *
 * Parameters: service  - [IN] the vmware service                             *
 *             type     - [IN] the performance entity type (HostSystem,       *
 *                             (Datastore, VirtualMachine...)                 *
 *             id       - [IN] the performance entity id                      *
 *             counters - [IN] NULL terminated list of performance counters   *
 *                             to be monitored for this entity                *
 *             instance - [IN] the performance counter instance name          *
 *             now      - [IN] the current timestamp                          *
 *                                                                            *
 * Comments: The performance counters are specified by their path:            *
 *             <group>/<key>[<rollup type>]                                   *
 *                                                                            *
 ******************************************************************************/
static void	vmware_service_add_perf_entity(trx_vmware_service_t *service, const char *type, const char *id,
		const char **counters, const char *instance, int now)
{
	trx_vmware_perf_entity_t	entity, *pentity;
	trx_uint64_t			counterid;
	int				i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() type:%s id:%s", __func__, type, id);

	if (NULL == (pentity = trx_vmware_service_get_perf_entity(service, type, id)))
	{
		entity.type = vmware_shared_strdup(type);
		entity.id = vmware_shared_strdup(id);

		pentity = (trx_vmware_perf_entity_t *)trx_hashset_insert(&service->entities, &entity, sizeof(trx_vmware_perf_entity_t));

		trx_vector_ptr_create_ext(&pentity->counters, __vm_mem_malloc_func, __vm_mem_realloc_func,
				__vm_mem_free_func);

		for (i = 0; NULL != counters[i]; i++)
		{
			if (SUCCEED == trx_vmware_service_get_counterid(service, counters[i], &counterid))
				vmware_counters_add_new(&pentity->counters, counterid);
			else
				treegix_log(LOG_LEVEL_DEBUG, "cannot find performance counter %s", counters[i]);
		}

		trx_vector_ptr_sort(&pentity->counters, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		pentity->refresh = TRX_VMWARE_PERF_INTERVAL_UNKNOWN;
		pentity->query_instance = vmware_shared_strdup(instance);
		pentity->error = NULL;
	}

	pentity->last_seen = now;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() perfcounters:%d", __func__, pentity->counters.values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_update_perf_entities                              *
 *                                                                            *
 * Purpose: adds new or remove old entities (hypervisors, virtual machines)   *
 *          from service performance entity list                              *
 *                                                                            *
 * Parameters: service - [IN] the vmware service                              *
 *                                                                            *
 ******************************************************************************/
static void	vmware_service_update_perf_entities(trx_vmware_service_t *service)
{
	int			i;
	trx_vmware_hv_t		*hv;
	trx_vmware_vm_t		*vm;
	trx_hashset_iter_t	iter;

	const char			*hv_perfcounters[] = {
						"net/packetsRx[summation]", "net/packetsTx[summation]",
						"net/received[average]", "net/transmitted[average]",
						"datastore/totalReadLatency[average]",
						"datastore/totalWriteLatency[average]", NULL
					};
	const char			*vm_perfcounters[] = {
						"virtualDisk/read[average]", "virtualDisk/write[average]",
						"virtualDisk/numberReadAveraged[average]",
						"virtualDisk/numberWriteAveraged[average]",
						"net/packetsRx[summation]", "net/packetsTx[summation]",
						"net/received[average]", "net/transmitted[average]",
						"cpu/ready[summation]", NULL
					};

	const char			*ds_perfcounters[] = {
						"disk/used[latest]", "disk/provisioned[latest]",
						"disk/capacity[latest]", NULL
					};

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* update current performance entities */
	trx_hashset_iter_reset(&service->data->hvs, &iter);
	while (NULL != (hv = (trx_vmware_hv_t *)trx_hashset_iter_next(&iter)))
	{
		vmware_service_add_perf_entity(service, "HostSystem", hv->id, hv_perfcounters, "*", service->lastcheck);

		for (i = 0; i < hv->vms.values_num; i++)
		{
			vm = (trx_vmware_vm_t *)hv->vms.values[i];
			vmware_service_add_perf_entity(service, "VirtualMachine", vm->id, vm_perfcounters, "*",
					service->lastcheck);
			treegix_log(LOG_LEVEL_TRACE, "%s() for type: VirtualMachine hv id: %s hv uuid: %s linked vm id:"
					" %s vm uuid: %s", __func__, hv->id, hv->uuid, vm->id, vm->uuid);
		}
	}

	if (TRX_VMWARE_TYPE_VCENTER == service->type)
	{
		for (i = 0; i < service->data->datastores.values_num; i++)
		{
			trx_vmware_datastore_t	*ds = service->data->datastores.values[i];
			vmware_service_add_perf_entity(service, "Datastore", ds->id, ds_perfcounters, "",
					service->lastcheck);
			treegix_log(LOG_LEVEL_TRACE, "%s() for type: Datastore id: %s name: %s uuid: %s", __func__,
					ds->id, ds->name, ds->uuid);
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() entities:%d", __func__, service->entities.num_data);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_update                                            *
 *                                                                            *
 * Purpose: updates object with a new data from vmware service                *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *                                                                            *
 ******************************************************************************/
static void	vmware_service_update(trx_vmware_service_t *service)
{
	CURL			*easyhandle = NULL;
	CURLoption		opt;
	CURLcode		err;
	struct curl_slist	*headers = NULL;
	trx_vmware_data_t	*data;
	trx_vector_str_t	hvs, dss;
	trx_vector_ptr_t	events;
	int			i, ret = FAIL;
	TRX_HTTPPAGE		page;	/* 347K/87K */
	unsigned char		skip_old = service->eventlog.skip_old;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() '%s'@'%s'", __func__, service->username, service->url);

	data = (trx_vmware_data_t *)trx_malloc(NULL, sizeof(trx_vmware_data_t));
	memset(data, 0, sizeof(trx_vmware_data_t));
	page.alloc = 0;

	trx_hashset_create(&data->hvs, 1, vmware_hv_hash, vmware_hv_compare);
	trx_vector_ptr_create(&data->clusters);
	trx_vector_ptr_create(&data->events);
	trx_vector_vmware_datastore_create(&data->datastores);

	trx_vector_str_create(&hvs);
	trx_vector_str_create(&dss);

	if (NULL == (easyhandle = curl_easy_init()))
	{
		treegix_log(LOG_LEVEL_WARNING, "Cannot initialize cURL library");
		goto out;
	}

	page.alloc = TRX_INIT_UPD_XML_SIZE;
	page.data = (char *)trx_malloc(NULL, page.alloc);
	headers = curl_slist_append(headers, TRX_XML_HEADER1);
	headers = curl_slist_append(headers, TRX_XML_HEADER2);
	headers = curl_slist_append(headers, TRX_XML_HEADER3);

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_HTTPHEADER, headers)))
	{
		treegix_log(LOG_LEVEL_WARNING, "Cannot set cURL option %d: %s.", (int)opt, curl_easy_strerror(err));
		goto clean;
	}

	if (SUCCEED != vmware_service_authenticate(service, easyhandle, &page, &data->error))
		goto clean;

	if (0 != (service->state & TRX_VMWARE_STATE_NEW) &&
			SUCCEED != vmware_service_initialize(service, easyhandle, &data->error))
	{
		goto clean;
	}

	if (SUCCEED != vmware_service_get_hv_ds_list(service, easyhandle, &hvs, &dss, &data->error))
		goto clean;

	trx_vector_vmware_datastore_reserve(&data->datastores, dss.values_num + data->datastores.values_alloc);

	for (i = 0; i < dss.values_num; i++)
	{
		trx_vmware_datastore_t	*datastore;

		if (NULL != (datastore = vmware_service_create_datastore(service, easyhandle, dss.values[i])))
			trx_vector_vmware_datastore_append(&data->datastores, datastore);
	}

	trx_vector_vmware_datastore_sort(&data->datastores, vmware_ds_id_compare);

	if (SUCCEED != trx_hashset_reserve(&data->hvs, hvs.values_num))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < hvs.values_num; i++)
	{
		trx_vmware_hv_t	hv_local;

		if (SUCCEED == vmware_service_init_hv(service, easyhandle, hvs.values[i], &data->datastores, &hv_local,
				&data->error))
		{
			trx_hashset_insert(&data->hvs, &hv_local, sizeof(hv_local));
		}
	}

	for (i = 0; i < data->datastores.values_num; i++)
	{
		trx_vector_str_sort(&data->datastores.values[i]->hv_uuids, TRX_DEFAULT_STR_COMPARE_FUNC);
	}

	trx_vector_vmware_datastore_sort(&data->datastores, vmware_ds_name_compare);

	/* skip collection of event data if we don't know where we stopped last time or item can't accept values */
	if (TRX_VMWARE_EVENT_KEY_UNINITIALIZED != service->eventlog.last_key && 0 == service->eventlog.skip_old &&
			SUCCEED != vmware_service_get_event_data(service, easyhandle, &data->events, &data->error))
	{
		goto clean;
	}

	if (0 != service->eventlog.skip_old)
	{
		char	*error = NULL;

		/* May not be present */
		if (SUCCEED != vmware_service_get_last_event_data(service, easyhandle, &data->events, &error))
		{
			treegix_log(LOG_LEVEL_DEBUG, "Unable retrieve lastevent value: %s.", error);
			trx_free(error);
		}
		else
			skip_old = 0;
	}

	if (TRX_VMWARE_TYPE_VCENTER == service->type &&
			SUCCEED != vmware_service_get_cluster_list(easyhandle, &data->clusters, &data->error))
	{
		goto clean;
	}

	if (TRX_VMWARE_TYPE_VCENTER != service->type)
		data->max_query_metrics = TRX_VPXD_STATS_MAXQUERYMETRICS;
	else if (SUCCEED != vmware_service_get_maxquerymetrics(easyhandle, &data->max_query_metrics, &data->error))
		goto clean;

	if (SUCCEED != vmware_service_logout(service, easyhandle, &data->error))
	{
		treegix_log(LOG_LEVEL_DEBUG, "Cannot close vmware connection: %s.", data->error);
		trx_free(data->error);
	}

	ret = SUCCEED;
clean:
	curl_slist_free_all(headers);
	curl_easy_cleanup(easyhandle);
	trx_free(page.data);

	trx_vector_str_clear_ext(&hvs, trx_str_free);
	trx_vector_str_destroy(&hvs);
	trx_vector_str_clear_ext(&dss, trx_str_free);
	trx_vector_str_destroy(&dss);
out:
	trx_vector_ptr_create(&events);
	trx_vmware_lock();

	/* remove UPDATING flag and set READY or FAILED flag */
	service->state &= ~(TRX_VMWARE_STATE_MASK | TRX_VMWARE_STATE_UPDATING);
	service->state |= (SUCCEED == ret) ? TRX_VMWARE_STATE_READY : TRX_VMWARE_STATE_FAILED;

	if (NULL != service->data && 0 != service->data->events.values_num &&
			((const trx_vmware_event_t *)service->data->events.values[0])->key > service->eventlog.last_key)
	{
		trx_vector_ptr_append_array(&events, service->data->events.values, service->data->events.values_num);
		trx_vector_ptr_clear(&service->data->events);
	}

	vmware_data_shared_free(service->data);
	service->data = vmware_data_shared_dup(data);
	service->eventlog.skip_old = skip_old;

	if (0 != events.values_num)
		trx_vector_ptr_append_array(&service->data->events, events.values, events.values_num);

	service->lastcheck = time(NULL);

	vmware_service_update_perf_entities(service);

	trx_vmware_unlock();

	vmware_data_free(data);
	trx_vector_ptr_destroy(&events);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s \tprocessed:" TRX_FS_SIZE_T " bytes of data", __func__,
			trx_result_string(ret), (trx_fs_size_t)page.alloc);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_process_perf_entity_data                          *
 *                                                                            *
 * Purpose: updates vmware performance statistics data                        *
 *                                                                            *
 * Parameters: pervalues - [OUT] the performance counter values               *
 *             xdoc      - [IN] the XML document containing performance       *
 *                              counter values for all entities               *
 *             node      - [IN] the XML node containing performance counter   *
 *                              values for the specified entity               *
 *                                                                            *
 * Return value: SUCCEED - the performance entity data was parsed             *
 *               FAIL    - the perofmance entity data did not contain valid   *
 *                         values                                             *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_process_perf_entity_data(trx_vector_ptr_t *pervalues, xmlDoc *xdoc, xmlNode *node)
{
	xmlXPathContext		*xpathCtx;
	xmlXPathObject		*xpathObj;
	xmlNodeSetPtr		nodeset;
	char			*instance, *counter, *value;
	int			i, values = 0, ret = FAIL;
	trx_vmware_perf_value_t	*perfvalue;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	xpathCtx = xmlXPathNewContext(xdoc);
	xpathCtx->node = node;

	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)"*[local-name()='value']", xpathCtx)))
		goto out;

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto out;

	nodeset = xpathObj->nodesetval;
	trx_vector_ptr_reserve(pervalues, nodeset->nodeNr + pervalues->values_alloc);

	for (i = 0; i < nodeset->nodeNr; i++)
	{
		value = trx_xml_read_node_value(xdoc, nodeset->nodeTab[i], "*[local-name()='value'][last()]");
		instance = trx_xml_read_node_value(xdoc, nodeset->nodeTab[i], "*[local-name()='id']"
				"/*[local-name()='instance']");
		counter = trx_xml_read_node_value(xdoc, nodeset->nodeTab[i], "*[local-name()='id']"
				"/*[local-name()='counterId']");

		if (NULL != value && NULL != counter)
		{
			perfvalue = (trx_vmware_perf_value_t *)trx_malloc(NULL, sizeof(trx_vmware_perf_value_t));

			TRX_STR2UINT64(perfvalue->counterid, counter);
			perfvalue->instance = (NULL != instance ? instance : trx_strdup(NULL, ""));

			if (0 == strcmp(value, "-1") || SUCCEED != is_uint64(value, &perfvalue->value))
				perfvalue->value = UINT64_MAX;
			else if (FAIL == ret)
				ret = SUCCEED;

			trx_vector_ptr_append(pervalues, perfvalue);

			instance = NULL;
			values++;
		}

		trx_free(counter);
		trx_free(instance);
		trx_free(value);
	}

out:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() values:%d", __func__, values);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_parse_perf_data                                   *
 *                                                                            *
 * Purpose: updates vmware performance statistics data                        *
 *                                                                            *
 * Parameters: perfdata - [OUT] performance entity data                       *
 *             xdoc     - [IN] the performance data xml document              *
 *                                                                            *
 ******************************************************************************/
static void	vmware_service_parse_perf_data(trx_vector_ptr_t *perfdata, xmlDoc *xdoc)
{
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	int		i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	xpathCtx = xmlXPathNewContext(xdoc);

	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)"/*/*/*/*", xpathCtx)))
		goto clean;

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto clean;

	nodeset = xpathObj->nodesetval;
	trx_vector_ptr_reserve(perfdata, nodeset->nodeNr + perfdata->values_alloc);

	for (i = 0; i < nodeset->nodeNr; i++)
	{
		trx_vmware_perf_data_t 	*data;
		int			ret = FAIL;

		data = (trx_vmware_perf_data_t *)trx_malloc(NULL, sizeof(trx_vmware_perf_data_t));

		data->id = trx_xml_read_node_value(xdoc, nodeset->nodeTab[i], "*[local-name()='entity']");
		data->type = trx_xml_read_node_value(xdoc, nodeset->nodeTab[i], "*[local-name()='entity']/@type");
		data->error = NULL;
		trx_vector_ptr_create(&data->values);

		if (NULL != data->type && NULL != data->id)
			ret = vmware_service_process_perf_entity_data(&data->values, xdoc, nodeset->nodeTab[i]);

		if (SUCCEED == ret)
			trx_vector_ptr_append(perfdata, data);
		else
			vmware_free_perfdata(data);
	}
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_perf_data_add_error                                       *
 *                                                                            *
 * Purpose: adds error for the specified perf entity                          *
 *                                                                            *
 * Parameters: perfdata - [OUT] the collected performance counter data        *
 *             type     - [IN] the performance entity type (HostSystem,       *
 *                             (Datastore, VirtualMachine...)                 *
 *             id       - [IN] the performance entity id                      *
 *             error    - [IN] the error to add                               *
 *                                                                            *
 * Comments: The performance counters are specified by their path:            *
 *             <group>/<key>[<rollup type>]                                   *
 *                                                                            *
 ******************************************************************************/
static void	vmware_perf_data_add_error(trx_vector_ptr_t *perfdata, const char *type, const char *id,
		const char *error)
{
	trx_vmware_perf_data_t	*data;

	data = trx_malloc(NULL, sizeof(trx_vmware_perf_data_t));

	data->type = trx_strdup(NULL, type);
	data->id = trx_strdup(NULL, id);
	data->error = trx_strdup(NULL, error);
	trx_vector_ptr_create(&data->values);

	trx_vector_ptr_append(perfdata, data);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_copy_perf_data                                    *
 *                                                                            *
 * Purpose: copies vmware performance statistics of specified service         *
 *                                                                            *
 * Parameters: service  - [IN] the vmware service                             *
 *             perfdata - [IN/OUT] the performance data                       *
 *                                                                            *
 ******************************************************************************/
static void	vmware_service_copy_perf_data(trx_vmware_service_t *service, trx_vector_ptr_t *perfdata)
{
	int				i, j, index;
	trx_vmware_perf_data_t		*data;
	trx_vmware_perf_value_t		*value;
	trx_vmware_perf_entity_t	*entity;
	trx_vmware_perf_counter_t	*perfcounter;
	trx_str_uint64_pair_t		perfvalue;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < perfdata->values_num; i++)
	{
		data = (trx_vmware_perf_data_t *)perfdata->values[i];

		if (NULL == (entity = trx_vmware_service_get_perf_entity(service, data->type, data->id)))
			continue;

		if (NULL != data->error)
		{
			entity->error = vmware_shared_strdup(data->error);
			continue;
		}

		for (j = 0; j < data->values.values_num; j++)
		{
			value = (trx_vmware_perf_value_t *)data->values.values[j];

			if (FAIL == (index = trx_vector_ptr_bsearch(&entity->counters, &value->counterid,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				continue;
			}

			perfcounter = (trx_vmware_perf_counter_t *)entity->counters.values[index];

			perfvalue.name = vmware_shared_strdup(value->instance);
			perfvalue.value = value->value;

			trx_vector_str_uint64_pair_append_ptr(&perfcounter->values, &perfvalue);
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_retrieve_perf_counters                            *
 *                                                                            *
 * Purpose: retrieves performance counter values from vmware service          *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] prepared cURL connection handle            *
 *             entities     - [IN] the performance collector entities to      *
 *                                 retrieve counters for                      *
 *             counters_max - [IN] the maximum number of counters per query.  *
 *             perfdata     - [OUT] the performance counter values            *
 *                                                                            *
 ******************************************************************************/
static void	vmware_service_retrieve_perf_counters(trx_vmware_service_t *service, CURL *easyhandle,
		trx_vector_ptr_t *entities, int counters_max, trx_vector_ptr_t *perfdata)
{
	char				*tmp = NULL, *error = NULL;
	size_t				tmp_alloc = 0, tmp_offset;
	int				i, j, start_counter = 0;
	trx_vmware_perf_entity_t	*entity;
	xmlDoc				*doc = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() counters_max:%d", __func__, counters_max);

	while (0 != entities->values_num)
	{
		int	counters_num = 0;

		tmp_offset = 0;
		trx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, TRX_POST_VSPHERE_HEADER);
		trx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset, "<ns0:QueryPerf>"
				"<ns0:_this type=\"PerformanceManager\">%s</ns0:_this>",
				vmware_service_objects[service->type].performance_manager);

		trx_vmware_lock();

		for (i = entities->values_num - 1; 0 <= i && counters_num < counters_max;)
		{
			char	*id_esc;

			entity = (trx_vmware_perf_entity_t *)entities->values[i];

			id_esc = xml_escape_dyn(entity->id);

			/* add entity performance counter request */
			trx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset, "<ns0:querySpec>"
					"<ns0:entity type=\"%s\">%s</ns0:entity>", entity->type, id_esc);

			trx_free(id_esc);

			if (TRX_VMWARE_PERF_INTERVAL_NONE == entity->refresh)
			{
				time_t	st_raw;
				struct	tm st;
				char	st_str[TRX_XML_DATETIME];

				/* add startTime for entity performance counter request for decrease XML data load */
				st_raw = trx_time() - SEC_PER_HOUR;
				gmtime_r(&st_raw, &st);
				strftime(st_str, sizeof(st_str), "%Y-%m-%dT%TZ", &st);
				trx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset, "<ns0:startTime>%s</ns0:startTime>",
						st_str);
			}

			trx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset, "<ns0:maxSample>1</ns0:maxSample>");

			for (j = start_counter; j < entity->counters.values_num && counters_num < counters_max; j++)
			{
				trx_vmware_perf_counter_t	*counter;

				counter = (trx_vmware_perf_counter_t *)entity->counters.values[j];

				trx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset,
						"<ns0:metricId><ns0:counterId>" TRX_FS_UI64
						"</ns0:counterId><ns0:instance>%s</ns0:instance></ns0:metricId>",
						counter->counterid, entity->query_instance);

				counter->state |= TRX_VMWARE_COUNTER_UPDATING;

				counters_num++;
			}

			if (j == entity->counters.values_num)
			{
				start_counter = 0;
				i--;
			}
			else
				start_counter = j;


			if (TRX_VMWARE_PERF_INTERVAL_NONE != entity->refresh)
			{
				trx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset, "<ns0:intervalId>%d</ns0:intervalId>",
					entity->refresh);
			}

			trx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset, "</ns0:querySpec>");
		}

		trx_vmware_unlock();
		trx_xml_free_doc(doc);
		doc = NULL;

		trx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, "</ns0:QueryPerf>");
		trx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, TRX_POST_VSPHERE_FOOTER);

		treegix_log(LOG_LEVEL_TRACE, "%s() SOAP request: %s", __func__, tmp);

		if (SUCCEED != trx_soap_post(__func__, easyhandle, tmp, &doc, &error))
		{
			for (j = i + 1; j < entities->values_num; j++)
			{
				entity = (trx_vmware_perf_entity_t *)entities->values[j];
				vmware_perf_data_add_error(perfdata, entity->type, entity->id, error);
			}

			trx_free(error);
			break;
		}

		/* parse performance data into local memory */
		vmware_service_parse_perf_data(perfdata, doc);

		while (entities->values_num > i + 1)
			trx_vector_ptr_remove_noorder(entities, entities->values_num - 1);
	}

	trx_free(tmp);
	trx_xml_free_doc(doc);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_update_perf                                       *
 *                                                                            *
 * Purpose: updates vmware statistics data                                    *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *                                                                            *
 ******************************************************************************/
static void	vmware_service_update_perf(trx_vmware_service_t *service)
{
#	define INIT_PERF_XML_SIZE	200 * TRX_KIBIBYTE

	CURL				*easyhandle = NULL;
	CURLoption			opt;
	CURLcode			err;
	struct curl_slist		*headers = NULL;
	int				i, ret = FAIL;
	char				*error = NULL;
	trx_vector_ptr_t		entities, hist_entities;
	trx_vmware_perf_entity_t	*entity;
	trx_hashset_iter_t		iter;
	trx_vector_ptr_t		perfdata;
	static TRX_HTTPPAGE		page;	/* 173K */

	treegix_log(LOG_LEVEL_DEBUG, "In %s() '%s'@'%s'", __func__, service->username, service->url);

	trx_vector_ptr_create(&entities);
	trx_vector_ptr_create(&hist_entities);
	trx_vector_ptr_create(&perfdata);
	page.alloc = 0;

	if (NULL == (easyhandle = curl_easy_init()))
	{
		error = trx_strdup(error, "cannot initialize cURL library");
		goto out;
	}

	page.alloc = INIT_PERF_XML_SIZE;
	page.data = (char *)trx_malloc(NULL, page.alloc);
	headers = curl_slist_append(headers, TRX_XML_HEADER1);
	headers = curl_slist_append(headers, TRX_XML_HEADER2);
	headers = curl_slist_append(headers, TRX_XML_HEADER3);

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_HTTPHEADER, headers)))
	{
		error = trx_dsprintf(error, "Cannot set cURL option %d: %s.", (int)opt, curl_easy_strerror(err));
		goto clean;
	}

	if (SUCCEED != vmware_service_authenticate(service, easyhandle, &page, &error))
		goto clean;

	/* update performance counter refresh rate for entities */

	trx_vmware_lock();

	trx_hashset_iter_reset(&service->entities, &iter);
	while (NULL != (entity = (trx_vmware_perf_entity_t *)trx_hashset_iter_next(&iter)))
	{
		/* remove old entities */
		if (0 != entity->last_seen && entity->last_seen < service->lastcheck)
		{
			vmware_shared_perf_entity_clean(entity);
			trx_hashset_iter_remove(&iter);
			continue;
		}

		if (TRX_VMWARE_PERF_INTERVAL_UNKNOWN != entity->refresh)
			continue;

		/* Entities are removed only during performance counter update and no two */
		/* performance counter updates for one service can happen simultaneously. */
		/* This means for refresh update we can safely use reference to entity    */
		/* outside vmware lock.                                                   */
		trx_vector_ptr_append(&entities, entity);
	}

	trx_vmware_unlock();

	/* get refresh rates */
	for (i = 0; i < entities.values_num; i++)
	{
		entity = entities.values[i];

		if (SUCCEED != vmware_service_get_perf_counter_refreshrate(service, easyhandle, entity->type,
				entity->id, &entity->refresh, &error))
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot get refresh rate for %s \"%s\": %s", entity->type,
					entity->id, error);
			trx_free(error);
		}
	}

	trx_vector_ptr_clear(&entities);

	trx_vmware_lock();

	trx_hashset_iter_reset(&service->entities, &iter);
	while (NULL != (entity = (trx_vmware_perf_entity_t *)trx_hashset_iter_next(&iter)))
	{
		if (TRX_VMWARE_PERF_INTERVAL_UNKNOWN == entity->refresh)
		{
			treegix_log(LOG_LEVEL_DEBUG, "skipping performance entity with zero refresh rate "
					"type:%s id:%s", entity->type, entity->id);
			continue;
		}

		if (TRX_VMWARE_PERF_INTERVAL_NONE == entity->refresh)
			trx_vector_ptr_append(&hist_entities, entity);
		else
			trx_vector_ptr_append(&entities, entity);
	}

	trx_vmware_unlock();

	vmware_service_retrieve_perf_counters(service, easyhandle, &entities, TRX_MAXQUERYMETRICS_UNLIMITED, &perfdata);
	vmware_service_retrieve_perf_counters(service, easyhandle, &hist_entities, service->data->max_query_metrics,
			&perfdata);

	if (SUCCEED != vmware_service_logout(service, easyhandle, &error))
	{
		treegix_log(LOG_LEVEL_DEBUG, "Cannot close vmware connection: %s.", error);
		trx_free(error);
	}

	ret = SUCCEED;
clean:
	curl_slist_free_all(headers);
	curl_easy_cleanup(easyhandle);
	trx_free(page.data);
out:
	trx_vmware_lock();

	if (FAIL == ret)
	{
		trx_hashset_iter_reset(&service->entities, &iter);
		while (NULL != (entity = trx_hashset_iter_next(&iter)))
			entity->error = vmware_shared_strdup(error);

		trx_free(error);
	}
	else
	{
		/* clean old performance data and copy the new data into shared memory */
		vmware_entities_shared_clean_stats(&service->entities);
		vmware_service_copy_perf_data(service, &perfdata);
	}

	service->state &= ~(TRX_VMWARE_STATE_UPDATING_PERF);
	service->lastperfcheck = time(NULL);

	trx_vmware_unlock();

	trx_vector_ptr_clear_ext(&perfdata, (trx_mem_free_func_t)vmware_free_perfdata);
	trx_vector_ptr_destroy(&perfdata);

	trx_vector_ptr_destroy(&hist_entities);
	trx_vector_ptr_destroy(&entities);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s \tprocessed " TRX_FS_SIZE_T " bytes of data", __func__,
			trx_result_string(ret), (trx_fs_size_t)page.alloc);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_remove                                            *
 *                                                                            *
 * Purpose: removes vmware service                                            *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *                                                                            *
 ******************************************************************************/
static void	vmware_service_remove(trx_vmware_service_t *service)
{
	int	index;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() '%s'@'%s'", __func__, service->username, service->url);

	trx_vmware_lock();

	if (FAIL != (index = trx_vector_ptr_search(&vmware->services, service, TRX_DEFAULT_PTR_COMPARE_FUNC)))
	{
		trx_vector_ptr_remove(&vmware->services, index);
		vmware_service_shared_free(service);
	}

	trx_vmware_unlock();

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/*
 * Public API
 */

/******************************************************************************
 *                                                                            *
 * Function: trx_vmware_get_service                                           *
 *                                                                            *
 * Purpose: gets vmware service object                                        *
 *                                                                            *
 * Parameters: url      - [IN] the vmware service URL                         *
 *             username - [IN] the vmware service username                    *
 *             password - [IN] the vmware service password                    *
 *                                                                            *
 * Return value: the requested service object or NULL if the object is not    *
 *               yet ready.                                                   *
 *                                                                            *
 * Comments: vmware lock must be locked with trx_vmware_lock() function       *
 *           before calling this function.                                    *
 *           If the service list does not contain the requested service object*
 *           then a new object is created, marked as new, added to the list   *
 *           and a NULL value is returned.                                    *
 *           If the object is in list, but is not yet updated also a NULL     *
 *           value is returned.                                               *
 *                                                                            *
 ******************************************************************************/
trx_vmware_service_t	*trx_vmware_get_service(const char* url, const char* username, const char* password)
{
	int			i, now;
	trx_vmware_service_t	*service = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() '%s'@'%s'", __func__, username, url);

	if (NULL == vmware)
		goto out;

	now = time(NULL);

	for (i = 0; i < vmware->services.values_num; i++)
	{
		service = (trx_vmware_service_t *)vmware->services.values[i];

		if (0 == strcmp(service->url, url) && 0 == strcmp(service->username, username) &&
				0 == strcmp(service->password, password))
		{
			service->lastaccess = now;

			/* return NULL if the service is not ready yet */
			if (0 == (service->state & (TRX_VMWARE_STATE_READY | TRX_VMWARE_STATE_FAILED)))
				service = NULL;

			goto out;
		}
	}

	service = (trx_vmware_service_t *)__vm_mem_malloc_func(NULL, sizeof(trx_vmware_service_t));
	memset(service, 0, sizeof(trx_vmware_service_t));

	service->url = vmware_shared_strdup(url);
	service->username = vmware_shared_strdup(username);
	service->password = vmware_shared_strdup(password);
	service->type = TRX_VMWARE_TYPE_UNKNOWN;
	service->state = TRX_VMWARE_STATE_NEW;
	service->lastaccess = now;
	service->eventlog.last_key = TRX_VMWARE_EVENT_KEY_UNINITIALIZED;
	service->eventlog.skip_old = 0;

	trx_hashset_create_ext(&service->entities, 100, vmware_perf_entity_hash_func,  vmware_perf_entity_compare_func,
			NULL, __vm_mem_malloc_func, __vm_mem_realloc_func, __vm_mem_free_func);

	trx_hashset_create_ext(&service->counters, TRX_VMWARE_COUNTERS_INIT_SIZE, vmware_counter_hash_func,
			vmware_counter_compare_func, NULL, __vm_mem_malloc_func, __vm_mem_realloc_func,
			__vm_mem_free_func);

	trx_vector_ptr_append(&vmware->services, service);

	/* new service does not have any data - return NULL */
	service = NULL;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__,
			trx_result_string(NULL != service ? SUCCEED : FAIL));

	return service;
}


/******************************************************************************
 *                                                                            *
 * Function: trx_vmware_service_get_counterid                                 *
 *                                                                            *
 * Purpose: gets vmware performance counter id by the path                    *
 *                                                                            *
 * Parameters: service   - [IN] the vmware service                            *
 *             path      - [IN] the path of counter to retrieve in format     *
 *                              <group>/<key>[<rollup type>]                  *
 *             counterid - [OUT] the counter id                               *
 *                                                                            *
 * Return value: SUCCEED if the counter was found, FAIL otherwise             *
 *                                                                            *
 ******************************************************************************/
int	trx_vmware_service_get_counterid(trx_vmware_service_t *service, const char *path,
		trx_uint64_t *counterid)
{
#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)
	trx_vmware_counter_t	*counter;
	int			ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() path:%s", __func__, path);

	if (NULL == (counter = (trx_vmware_counter_t *)trx_hashset_search(&service->counters, &path)))
		goto out;

	*counterid = counter->id;

	treegix_log(LOG_LEVEL_DEBUG, "%s() counterid:" TRX_FS_UI64, __func__, *counterid);

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
#else
	return FAIL;
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: trx_vmware_service_add_perf_counter                              *
 *                                                                            *
 * Purpose: start monitoring performance counter of the specified entity      *
 *                                                                            *
 * Parameters: service   - [IN] the vmware service                            *
 *             type      - [IN] the entity type                               *
 *             id        - [IN] the entity id                                 *
 *             counterid - [IN] the performance counter id                    *
 *             instance  - [IN] the performance counter instance name         *
 *                                                                            *
 * Return value: SUCCEED - the entity counter was added to monitoring list.   *
 *               FAIL    - the performance counter of the specified entity    *
 *                         is already being monitored.                        *
 *                                                                            *
 ******************************************************************************/
int	trx_vmware_service_add_perf_counter(trx_vmware_service_t *service, const char *type, const char *id,
		trx_uint64_t counterid, const char *instance)
{
	trx_vmware_perf_entity_t	*pentity, entity;
	int				ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() type:%s id:%s counterid:" TRX_FS_UI64, __func__, type, id,
			counterid);

	if (NULL == (pentity = trx_vmware_service_get_perf_entity(service, type, id)))
	{
		entity.refresh = TRX_VMWARE_PERF_INTERVAL_UNKNOWN;
		entity.last_seen = 0;
		entity.query_instance = vmware_shared_strdup(instance);
		entity.type = vmware_shared_strdup(type);
		entity.id = vmware_shared_strdup(id);
		entity.error = NULL;
		trx_vector_ptr_create_ext(&entity.counters, __vm_mem_malloc_func, __vm_mem_realloc_func,
				__vm_mem_free_func);

		pentity = (trx_vmware_perf_entity_t *)trx_hashset_insert(&service->entities, &entity,
				sizeof(trx_vmware_perf_entity_t));
	}

	if (FAIL == trx_vector_ptr_search(&pentity->counters, &counterid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC))
	{
		vmware_counters_add_new(&pentity->counters, counterid);
		trx_vector_ptr_sort(&pentity->counters, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

		ret = SUCCEED;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_vmware_service_get_perf_entity                               *
 *                                                                            *
 * Purpose: gets performance entity by type and id                            *
 *                                                                            *
 * Parameters: service - [IN] the vmware service                              *
 *             type    - [IN] the performance entity type                     *
 *             id      - [IN] the performance entity id                       *
 *                                                                            *
 * Return value: the performance entity or NULL if not found                  *
 *                                                                            *
 ******************************************************************************/
trx_vmware_perf_entity_t	*trx_vmware_service_get_perf_entity(trx_vmware_service_t *service, const char *type,
		const char *id)
{
	trx_vmware_perf_entity_t	*pentity, entity = {.type = (char *)type, .id = (char *)id};

	treegix_log(LOG_LEVEL_DEBUG, "In %s() type:%s id:%s", __func__, type, id);

	pentity = (trx_vmware_perf_entity_t *)trx_hashset_search(&service->entities, &entity);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() entity:%p", __func__, (void *)pentity);

	return pentity;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_vmware_init                                                  *
 *                                                                            *
 * Purpose: initializes vmware collector service                              *
 *                                                                            *
 * Comments: This function must be called before worker threads are forked.   *
 *                                                                            *
 ******************************************************************************/
int	trx_vmware_init(char **error)
{
	int		ret = FAIL;
	trx_uint64_t	size_reserved;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != trx_mutex_create(&vmware_lock, TRX_MUTEX_VMWARE, error))
		goto out;

	size_reserved = trx_mem_required_size(1, "vmware cache size", "VMwareCacheSize");

	CONFIG_VMWARE_CACHE_SIZE -= size_reserved;

	if (SUCCEED != trx_mem_create(&vmware_mem, CONFIG_VMWARE_CACHE_SIZE, "vmware cache size", "VMwareCacheSize", 0,
			error))
	{
		goto out;
	}

	vmware = (trx_vmware_t *)__vm_mem_malloc_func(NULL, sizeof(trx_vmware_t));
	memset(vmware, 0, sizeof(trx_vmware_t));

	VMWARE_VECTOR_CREATE(&vmware->services, ptr);
#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)
	trx_hashset_create_ext(&vmware->strpool, 100, vmware_strpool_hash_func, vmware_strpool_compare_func, NULL,
		__vm_mem_malloc_func, __vm_mem_realloc_func, __vm_mem_free_func);
#endif
	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_vmware_destroy                                               *
 *                                                                            *
 * Purpose: destroys vmware collector service                                 *
 *                                                                            *
 ******************************************************************************/
void	trx_vmware_destroy(void)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);
#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)
	trx_hashset_destroy(&vmware->strpool);
#endif
	trx_mutex_destroy(&vmware_lock);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

#define	TRX_VMWARE_TASK_IDLE		1
#define	TRX_VMWARE_TASK_UPDATE		2
#define	TRX_VMWARE_TASK_UPDATE_PERF	3
#define	TRX_VMWARE_TASK_REMOVE		4

/******************************************************************************
 *                                                                            *
 * Function: main_vmware_loop                                                 *
 *                                                                            *
 * Purpose: the vmware collector main loop                                    *
 *                                                                            *
 ******************************************************************************/
TRX_THREAD_ENTRY(vmware_thread, args)
{
#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)
	int			i, now, task, next_update, updated_services = 0, removed_services = 0,
				old_updated_services = 0, old_removed_services = 0, sleeptime = -1;
	trx_vmware_service_t	*service = NULL;
	double			sec, total_sec = 0.0, old_total_sec = 0.0;
	time_t			last_stat_time;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

#define STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	last_stat_time = time(NULL);

	while (TRX_IS_RUNNING())
	{
		sec = trx_time();
		trx_update_env(sec);

		if (0 != sleeptime)
		{
			trx_setproctitle("%s #%d [updated %d, removed %d VMware services in " TRX_FS_DBL " sec, "
					"querying VMware services]", get_process_type_string(process_type), process_num,
					old_updated_services, old_removed_services, old_total_sec);
		}

		do
		{
			task = TRX_VMWARE_TASK_IDLE;

			now = time(NULL);
			next_update = now + POLLER_DELAY;

			trx_vmware_lock();

			/* find a task to be performed on a vmware service */
			for (i = 0; i < vmware->services.values_num; i++)
			{
				service = (trx_vmware_service_t *)vmware->services.values[i];

				/* check if the service isn't used and should be removed */
				if (0 == (service->state & TRX_VMWARE_STATE_BUSY) &&
						now - service->lastaccess > TRX_VMWARE_SERVICE_TTL)
				{
					service->state |= TRX_VMWARE_STATE_REMOVING;
					task = TRX_VMWARE_TASK_REMOVE;
					break;
				}

				/* check if the performance statistics should be updated */
				if (0 != (service->state & TRX_VMWARE_STATE_READY) &&
						0 == (service->state & TRX_VMWARE_STATE_UPDATING_PERF) &&
						now - service->lastperfcheck >= TRX_VMWARE_PERF_UPDATE_PERIOD)
				{
					service->state |= TRX_VMWARE_STATE_UPDATING_PERF;
					task = TRX_VMWARE_TASK_UPDATE_PERF;
					break;
				}

				/* check if the service data should be updated */
				if (0 == (service->state & TRX_VMWARE_STATE_UPDATING) &&
						now - service->lastcheck >= TRX_VMWARE_CACHE_UPDATE_PERIOD)
				{
					service->state |= TRX_VMWARE_STATE_UPDATING;
					task = TRX_VMWARE_TASK_UPDATE;
					break;
				}

				/* don't calculate nextcheck for services that are already updating something */
				if (0 != (service->state & TRX_VMWARE_STATE_BUSY))
						continue;

				/* calculate next service update time */

				if (service->lastcheck + TRX_VMWARE_CACHE_UPDATE_PERIOD < next_update)
					next_update = service->lastcheck + TRX_VMWARE_CACHE_UPDATE_PERIOD;

				if (0 != (service->state & TRX_VMWARE_STATE_READY))
				{
					if (service->lastperfcheck + TRX_VMWARE_PERF_UPDATE_PERIOD < next_update)
						next_update = service->lastperfcheck + TRX_VMWARE_PERF_UPDATE_PERIOD;
				}
			}

			trx_vmware_unlock();

			switch (task)
			{
				case TRX_VMWARE_TASK_UPDATE:
					vmware_service_update(service);
					updated_services++;
					break;
				case TRX_VMWARE_TASK_UPDATE_PERF:
					vmware_service_update_perf(service);
					updated_services++;
					break;
				case TRX_VMWARE_TASK_REMOVE:
					vmware_service_remove(service);
					removed_services++;
					break;
			}
		}
		while (TRX_VMWARE_TASK_IDLE != task && TRX_IS_RUNNING());

		total_sec += trx_time() - sec;
		now = time(NULL);

		sleeptime = 0 < next_update - now ? next_update - now : 0;

		if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
		{
			if (0 == sleeptime)
			{
				trx_setproctitle("%s #%d [updated %d, removed %d VMware services in " TRX_FS_DBL " sec,"
						" querying VMware services]", get_process_type_string(process_type),
						process_num, updated_services, removed_services, total_sec);
			}
			else
			{
				trx_setproctitle("%s #%d [updated %d, removed %d VMware services in " TRX_FS_DBL " sec,"
						" idle %d sec]", get_process_type_string(process_type), process_num,
						updated_services, removed_services, total_sec, sleeptime);
				old_updated_services = updated_services;
				old_removed_services = removed_services;
				old_total_sec = total_sec;
			}
			updated_services = 0;
			removed_services = 0;
			total_sec = 0.0;
			last_stat_time = time(NULL);
		}

		trx_sleep_loop(sleeptime);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
#undef STAT_INTERVAL
#else
	TRX_UNUSED(args);
	THIS_SHOULD_NEVER_HAPPEN;
	trx_thread_exit(EXIT_SUCCESS);
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: trx_vmware_lock                                                  *
 *                                                                            *
 * Purpose: locks vmware collector                                            *
 *                                                                            *
 ******************************************************************************/
void	trx_vmware_lock(void)
{
	trx_mutex_lock(vmware_lock);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_vmware_unlock                                                *
 *                                                                            *
 * Purpose: unlocks vmware collector                                          *
 *                                                                            *
 ******************************************************************************/
void	trx_vmware_unlock(void)
{
	trx_mutex_unlock(vmware_lock);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_vmware_get_statistics                                        *
 *                                                                            *
 * Purpose: gets vmware collector statistics                                  *
 *                                                                            *
 * Parameters: stats   - [OUT] the vmware collector statistics                *
 *                                                                            *
 * Return value: SUCCEEED - the statistics were retrieved successfully        *
 *               FAIL     - no vmware collectors are running                  *
 *                                                                            *
 ******************************************************************************/
int	trx_vmware_get_statistics(trx_vmware_stats_t *stats)
{
	if (NULL == vmware_mem)
		return FAIL;

	trx_vmware_lock();

	stats->memory_total = vmware_mem->total_size;
	stats->memory_used = vmware_mem->total_size - vmware_mem->free_size;

	trx_vmware_unlock();

	return SUCCEED;
}

#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)

/*
 * XML support
 */
/******************************************************************************
 *                                                                            *
 * Function: libxml_handle_error                                              *
 *                                                                            *
 * Purpose: libxml2 callback function for error handle                        *
 *                                                                            *
 * Parameters: user_data - [IN/OUT] the user context                          *
 *             err       - [IN] the libxml2 error message                     *
 *                                                                            *
 ******************************************************************************/
static void	libxml_handle_error(void *user_data, xmlErrorPtr err)
{
	TRX_UNUSED(user_data);
	TRX_UNUSED(err);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_xml_try_read_value                                           *
 *                                                                            *
 * Purpose: retrieve a value from xml data and return status of operation     *
 *                                                                            *
 * Parameters: data   - [IN] XML data                                         *
 *             len    - [IN] XML data length (optional)                       *
 *             xpath  - [IN] XML XPath                                        *
 *             xdoc   - [OUT] parsed xml document                             *
 *             value  - [OUT] selected xml node value                         *
 *             error  - [OUT] error of xml or xpath formats                   *
 *                                                                            *
 * Return: SUCCEED - select xpath successfully, result stored in 'value'      *
 *         FAIL - failed select xpath expression                              *
 *                                                                            *
 ******************************************************************************/
static int	trx_xml_try_read_value(const char *data, size_t len, const char *xpath, xmlDoc **xdoc, char **value,
		char **error)
{
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	xmlChar		*val;
	int		ret = FAIL;

	if (NULL == data)
		goto out;

	xmlSetStructuredErrorFunc(NULL, &libxml_handle_error);

	if (NULL == (*xdoc = xmlReadMemory(data, (0 == len ? strlen(data) : len), TRX_VM_NONAME_XML, NULL,
			TRX_XML_PARSE_OPTS)))
	{
		if (NULL != error)
			*error = trx_dsprintf(*error, "Received response has no valid XML data.");

		xmlSetStructuredErrorFunc(NULL, NULL);
		goto out;
	}

	xpathCtx = xmlXPathNewContext(*xdoc);

	if (NULL == (xpathObj = xmlXPathEvalExpression((const xmlChar *)xpath, xpathCtx)))
	{
		if (NULL != error)
			*error = trx_dsprintf(*error, "Invalid xpath expression: \"%s\".", xpath);

		goto clean;
	}

	ret = SUCCEED;

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto clean;

	nodeset = xpathObj->nodesetval;

	if (NULL != (val = xmlNodeListGetString(*xdoc, nodeset->nodeTab[0]->xmlChildrenNode, 1)))
	{
		*value = trx_strdup(*value, (const char *)val);
		xmlFree(val);
	}
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlSetStructuredErrorFunc(NULL, NULL);
	xmlXPathFreeContext(xpathCtx);
	xmlResetLastError();
out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_xml_read_node_value                                          *
 *                                                                            *
 * Purpose: retrieve a value from xml data relative to the specified node     *
 *                                                                            *
 * Parameters: doc    - [IN] the XML document                                 *
 *             node   - [IN] the XML node                                     *
 *             xpath  - [IN] the XML XPath                                    *
 *                                                                            *
 * Return: The allocated value string or NULL if the xml data does not        *
 *         contain the value specified by xpath.                              *
 *                                                                            *
 ******************************************************************************/
static char	*trx_xml_read_node_value(xmlDoc *doc, xmlNode *node, const char *xpath)
{
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	xmlChar		*val;
	char		*value = NULL;

	xpathCtx = xmlXPathNewContext(doc);

	xpathCtx->node = node;

	if (NULL == (xpathObj = xmlXPathEvalExpression((const xmlChar *)xpath, xpathCtx)))
		goto clean;

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto clean;

	nodeset = xpathObj->nodesetval;

	if (NULL != (val = xmlNodeListGetString(doc, nodeset->nodeTab[0]->xmlChildrenNode, 1)))
	{
		value = trx_strdup(NULL, (const char *)val);
		xmlFree(val);
	}
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);

	return value;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_xml_read_doc_value                                           *
 *                                                                            *
 * Purpose: retrieve a value from xml document relative to the root node      *
 *                                                                            *
 * Parameters: xdoc   - [IN] the XML document                                 *
 *             xpath  - [IN] the XML XPath                                    *
 *                                                                            *
 * Return: The allocated value string or NULL if the xml data does not        *
 *         contain the value specified by xpath.                              *
 *                                                                            *
 ******************************************************************************/
static char	*trx_xml_read_doc_value(xmlDoc *xdoc, const char *xpath)
{
	xmlNode	*root_element;

	root_element = xmlDocGetRootElement(xdoc);
	return trx_xml_read_node_value(xdoc, root_element, xpath);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_xml_read_values                                              *
 *                                                                            *
 * Purpose: populate array of values from a xml data                          *
 *                                                                            *
 * Parameters: xdoc   - [IN] XML document                                     *
 *             xpath  - [IN] XML XPath                                        *
 *             values - [OUT] list of requested values                        *
 *                                                                            *
 * Return: Upon successful completion the function return SUCCEED.            *
 *         Otherwise, FAIL is returned.                                       *
 *                                                                            *
 ******************************************************************************/
static int	trx_xml_read_values(xmlDoc *xdoc, const char *xpath, trx_vector_str_t *values)
{
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	xmlChar		*val;
	int		i, ret = FAIL;

	if (NULL == xdoc)
		goto out;

	xpathCtx = xmlXPathNewContext(xdoc);

	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)xpath, xpathCtx)))
		goto clean;

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto clean;

	nodeset = xpathObj->nodesetval;

	for (i = 0; i < nodeset->nodeNr; i++)
	{
		if (NULL != (val = xmlNodeListGetString(xdoc, nodeset->nodeTab[i]->xmlChildrenNode, 1)))
		{
			trx_vector_str_append(values, trx_strdup(NULL, (const char *)val));
			xmlFree(val);
		}
	}

	ret = SUCCEED;
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);
out:
	return ret;
}

#endif
