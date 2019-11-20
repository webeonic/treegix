

#include "common.h"
#include "db.h"
#include "log.h"
#include "daemon.h"
#include "trxserver.h"
#include "trxself.h"
#include "trxtasks.h"

#include "escalator.h"
#include "../operations.h"
#include "../actions.h"
#include "../events.h"
#include "../scripts/scripts.h"
#include "../../libs/trxcrypto/tls.h"
#include "comms.h"

extern int	CONFIG_ESCALATOR_FORKS;

#define CONFIG_ESCALATOR_FREQUENCY	3

#define TRX_ESCALATION_SOURCE_DEFAULT	0
#define TRX_ESCALATION_SOURCE_ITEM	1
#define TRX_ESCALATION_SOURCE_TRIGGER	2

#define TRX_ESCALATION_CANCEL		0
#define TRX_ESCALATION_DELETE		1
#define TRX_ESCALATION_SKIP		2
#define TRX_ESCALATION_PROCESS		3
#define TRX_ESCALATION_SUPPRESS		4

#define TRX_ESCALATIONS_PER_STEP	1000

typedef struct
{
	trx_uint64_t	userid;
	trx_uint64_t	mediatypeid;
	char		*subject;
	char		*message;
	void		*next;
}
TRX_USER_MSG;

typedef struct
{
	trx_uint64_t	hostgroupid;
	char		*tag;
	char		*value;
}
trx_tag_filter_t;

static void	trx_tag_filter_free(trx_tag_filter_t *tag_filter)
{
	trx_free(tag_filter->tag);
	trx_free(tag_filter->value);
	trx_free(tag_filter);
}

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

static void	add_message_alert(const DB_EVENT *event, const DB_EVENT *r_event, trx_uint64_t actionid, int esc_step,
		trx_uint64_t userid, trx_uint64_t mediatypeid, const char *subject, const char *message,
		const DB_ACKNOWLEDGE *ack);

/******************************************************************************
 *                                                                            *
 * Function: check_perm2system                                                *
 *                                                                            *
 * Purpose: Check user permissions to access system                           *
 *                                                                            *
 * Parameters: userid - user ID                                               *
 *                                                                            *
 * Return value: SUCCEED - access allowed, FAIL - otherwise                   *
 *                                                                            *
 ******************************************************************************/
static int	check_perm2system(trx_uint64_t userid)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		res = SUCCEED;

	result = DBselect(
			"select count(*)"
			" from usrgrp g,users_groups ug"
			" where ug.userid=" TRX_FS_UI64
				" and g.usrgrpid=ug.usrgrpid"
				" and g.users_status=%d",
			userid, GROUP_STATUS_DISABLED);

	if (NULL != (row = DBfetch(result)) && SUCCEED != DBis_null(row[0]) && atoi(row[0]) > 0)
		res = FAIL;

	DBfree_result(result);

	return res;
}

static	int	get_user_type(trx_uint64_t userid)
{
	int		user_type = -1;
	DB_RESULT	result;
	DB_ROW		row;

	result = DBselect("select type from users where userid=" TRX_FS_UI64, userid);

	if (NULL != (row = DBfetch(result)) && FAIL == DBis_null(row[0]))
		user_type = atoi(row[0]);

	DBfree_result(result);

	return user_type;
}

/******************************************************************************
 *                                                                            *
 * Function: get_hostgroups_permission                                        *
 *                                                                            *
 * Purpose: Return user permissions for access to the host                    *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: PERM_DENY - if host or user not found,                       *
 *                   or permission otherwise                                  *
 *                                                                            *
 ******************************************************************************/
static int	get_hostgroups_permission(trx_uint64_t userid, trx_vector_uint64_t *hostgroupids)
{
	int		perm = PERM_DENY;
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;
	DB_RESULT	result;
	DB_ROW		row;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == hostgroupids->values_num)
		goto out;

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select min(r.permission)"
			" from rights r"
			" join users_groups ug on ug.usrgrpid=r.groupid"
				" where ug.userid=" TRX_FS_UI64 " and", userid);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "r.id",
			hostgroupids->values, hostgroupids->values_num);
	result = DBselect("%s", sql);

	if (NULL != (row = DBfetch(result)) && FAIL == DBis_null(row[0]))
		perm = atoi(row[0]);

	DBfree_result(result);
	trx_free(sql);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_permission_string(perm));

	return perm;
}

/******************************************************************************
 *                                                                            *
 * Function: check_tag_based_permission                                       *
 *                                                                            *
 * Purpose: Check user access to event by tags                                *
 *                                                                            *
 * Parameters: userid       - user id                                         *
 *             hostgroupids - list of host groups in which trigger was to     *
 *                            be found                                        *
 *             event        - checked event for access                        *
 *                                                                            *
 * Return value: SUCCEED - user has access                                    *
 *               FAIL    - user does not have access                          *
 *                                                                            *
 ******************************************************************************/
static int	check_tag_based_permission(trx_uint64_t userid, trx_vector_uint64_t *hostgroupids,
		const DB_EVENT *event)
{
	char			*sql = NULL, hostgroupid[TRX_MAX_UINT64_LEN + 1];
	size_t			sql_alloc = 0, sql_offset = 0;
	DB_RESULT		result;
	DB_ROW			row;
	int			ret = FAIL, i;
	trx_vector_ptr_t	tag_filters;
	trx_tag_filter_t	*tag_filter;
	DB_CONDITION		condition;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&tag_filters);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select tf.groupid,tf.tag,tf.value from tag_filter tf"
			" join users_groups ug on ug.usrgrpid=tf.usrgrpid"
				" where ug.userid=" TRX_FS_UI64, userid);
	result = DBselect("%s order by tf.groupid", sql);

	while (NULL != (row = DBfetch(result)))
	{
		tag_filter = (trx_tag_filter_t *)trx_malloc(NULL, sizeof(trx_tag_filter_t));
		TRX_STR2UINT64(tag_filter->hostgroupid, row[0]);
		tag_filter->tag = trx_strdup(NULL, row[1]);
		tag_filter->value = trx_strdup(NULL, row[2]);
		trx_vector_ptr_append(&tag_filters, tag_filter);
	}
	trx_free(sql);
	DBfree_result(result);

	if (0 < tag_filters.values_num)
		condition.op = CONDITION_OPERATOR_EQUAL;
	else
		ret = SUCCEED;

	for (i = 0; i < tag_filters.values_num && SUCCEED != ret; i++)
	{
		tag_filter = (trx_tag_filter_t *)tag_filters.values[i];

		if (FAIL == trx_vector_uint64_search(hostgroupids, tag_filter->hostgroupid,
				TRX_DEFAULT_UINT64_COMPARE_FUNC))
		{
			continue;
		}

		if (NULL != tag_filter->tag && 0 != strlen(tag_filter->tag))
		{
			trx_snprintf(hostgroupid, sizeof(hostgroupid), TRX_FS_UI64, tag_filter->hostgroupid);

			if (NULL != tag_filter->value && 0 != strlen(tag_filter->value))
			{
				condition.conditiontype = CONDITION_TYPE_EVENT_TAG_VALUE;
				condition.value2 = tag_filter->tag;
				condition.value = tag_filter->value;
			}
			else
			{
				condition.conditiontype = CONDITION_TYPE_EVENT_TAG;
				condition.value = tag_filter->tag;
			}

			ret = check_action_condition(event, &condition);
		}
		else
			ret = SUCCEED;
	}
	trx_vector_ptr_clear_ext(&tag_filters, (trx_clean_func_t)trx_tag_filter_free);
	trx_vector_ptr_destroy(&tag_filters);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_trigger_permission                                           *
 *                                                                            *
 * Purpose: Return user permissions for access to trigger                     *
 *                                                                            *
 * Return value: PERM_DENY - if host or user not found,                       *
 *                   or permission otherwise                                  *
 *                                                                            *
 ******************************************************************************/
static int	get_trigger_permission(trx_uint64_t userid, const DB_EVENT *event)
{
	int			perm = PERM_DENY;
	DB_RESULT		result;
	DB_ROW			row;
	trx_vector_uint64_t	hostgroupids;
	trx_uint64_t		hostgroupid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (USER_TYPE_SUPER_ADMIN == get_user_type(userid))
	{
		perm = PERM_READ_WRITE;
		goto out;
	}

	trx_vector_uint64_create(&hostgroupids);

	result = DBselect(
			"select distinct hg.groupid from items i"
			" join functions f on i.itemid=f.itemid"
			" join hosts_groups hg on hg.hostid = i.hostid"
				" and f.triggerid=" TRX_FS_UI64,
			event->objectid);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(hostgroupid, row[0]);
		trx_vector_uint64_append(&hostgroupids, hostgroupid);
	}
	DBfree_result(result);

	trx_vector_uint64_sort(&hostgroupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	if (PERM_DENY < (perm = get_hostgroups_permission(userid, &hostgroupids)) &&
			FAIL == check_tag_based_permission(userid, &hostgroupids, event))
	{
		perm = PERM_DENY;
	}

	trx_vector_uint64_destroy(&hostgroupids);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_permission_string(perm));

	return perm;
}

/******************************************************************************
 *                                                                            *
 * Function: get_item_permission                                              *
 *                                                                            *
 * Purpose: Return user permissions for access to item                        *
 *                                                                            *
 * Return value: PERM_DENY - if host or user not found,                       *
 *                   or permission otherwise                                  *
 *                                                                            *
 ******************************************************************************/
static int	get_item_permission(trx_uint64_t userid, trx_uint64_t itemid)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			perm = PERM_DENY;
	trx_vector_uint64_t	hostgroupids;
	trx_uint64_t		hostgroupid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&hostgroupids);
	trx_vector_uint64_sort(&hostgroupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	if (USER_TYPE_SUPER_ADMIN == get_user_type(userid))
	{
		perm = PERM_READ_WRITE;
		goto out;
	}

	result = DBselect(
			"select hg.groupid from items i"
			" join hosts_groups hg on hg.hostid=i.hostid"
			" where i.itemid=" TRX_FS_UI64,
			itemid);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(hostgroupid, row[0]);
		trx_vector_uint64_append(&hostgroupids, hostgroupid);
	}
	DBfree_result(result);

	perm = get_hostgroups_permission(userid, &hostgroupids);
out:
	trx_vector_uint64_destroy(&hostgroupids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_permission_string(perm));

	return perm;
}

static void	add_user_msg(trx_uint64_t userid, trx_uint64_t mediatypeid, TRX_USER_MSG **user_msg,
		const char *subject, const char *message)
{
	TRX_USER_MSG	*p, **pnext;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == mediatypeid)
	{
		for (pnext = user_msg, p = *user_msg; NULL != p; p = *pnext)
		{
			if (p->userid == userid && 0 == strcmp(p->subject, subject) &&
					0 == strcmp(p->message, message) && 0 != p->mediatypeid)
			{
				*pnext = (TRX_USER_MSG *)p->next;

				trx_free(p->subject);
				trx_free(p->message);
				trx_free(p);
			}
			else
				pnext = (TRX_USER_MSG **)&p->next;
		}
	}

	for (p = *user_msg; NULL != p; p = (TRX_USER_MSG *)p->next)
	{
		if (p->userid == userid && 0 == strcmp(p->subject, subject) &&
				0 == strcmp(p->message, message) &&
				(0 == p->mediatypeid || mediatypeid == p->mediatypeid))
		{
			break;
		}
	}

	if (NULL == p)
	{
		p = (TRX_USER_MSG *)trx_malloc(p, sizeof(TRX_USER_MSG));

		p->userid = userid;
		p->mediatypeid = mediatypeid;
		p->subject = trx_strdup(NULL, subject);
		p->message = trx_strdup(NULL, message);
		p->next = *user_msg;

		*user_msg = p;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	add_object_msg(trx_uint64_t actionid, trx_uint64_t operationid, trx_uint64_t mediatypeid,
		TRX_USER_MSG **user_msg, const char *subject, const char *message, const DB_EVENT *event,
		const DB_EVENT *r_event, const DB_ACKNOWLEDGE *ack, int macro_type)
{
	DB_RESULT	result;
	DB_ROW		row;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select userid"
			" from opmessage_usr"
			" where operationid=" TRX_FS_UI64
			" union "
			"select g.userid"
			" from opmessage_grp m,users_groups g"
			" where m.usrgrpid=g.usrgrpid"
				" and m.operationid=" TRX_FS_UI64,
			operationid, operationid);

	while (NULL != (row = DBfetch(result)))
	{
		trx_uint64_t	userid;
		char		*subject_dyn, *message_dyn;

		TRX_STR2UINT64(userid, row[0]);

		/* exclude acknowledgement author from the recipient list */
		if (NULL != ack && ack->userid == userid)
			continue;

		if (SUCCEED != check_perm2system(userid))
			continue;

		switch (event->object)
		{
			case EVENT_OBJECT_TRIGGER:
				if (PERM_READ > get_trigger_permission(userid, event))
					continue;
				break;
			case EVENT_OBJECT_ITEM:
			case EVENT_OBJECT_LLDRULE:
				if (PERM_READ > get_item_permission(userid, event->objectid))
					continue;
				break;
		}

		subject_dyn = trx_strdup(NULL, subject);
		message_dyn = trx_strdup(NULL, message);

		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL, NULL, ack,
				&subject_dyn, macro_type, NULL, 0);
		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL, NULL, ack,
				&message_dyn, macro_type, NULL, 0);

		add_user_msg(userid, mediatypeid, user_msg, subject_dyn, message_dyn);

		trx_free(subject_dyn);
		trx_free(message_dyn);
	}
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: add_sentusers_msg                                                *
 *                                                                            *
 * Purpose: adds message to be sent to all recipients of messages previously  *
 *          generated by action operations or acknowledgement operations,     *
 *          which is related with an event or recovery event                  *
 *                                                                            *
 * Parameters: user_msg - [IN/OUT] the message list                           *
 *             actionid - [IN] the action identifier                          *
 *             event    - [IN] the event                                      *
 *             r_event  - [IN] the recover event (optional, can be NULL)      *
 *             subject  - [IN] the message subject                            *
 *             message  - [IN] the message body                               *
 *             ack      - [IN] the acknowledge (optional, can be NULL)        *
 *                                                                            *
 ******************************************************************************/
static void	add_sentusers_msg(TRX_USER_MSG **user_msg, trx_uint64_t actionid, const DB_EVENT *event,
		const DB_EVENT *r_event, const char *subject, const char *message, const DB_ACKNOWLEDGE *ack)
{
	char		*subject_dyn, *message_dyn, *sql = NULL;
	DB_RESULT	result;
	DB_ROW		row;
	trx_uint64_t	userid, mediatypeid;
	int		message_type;
	size_t		sql_alloc = 0, sql_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct userid,mediatypeid"
			" from alerts"
			" where actionid=" TRX_FS_UI64
				" and mediatypeid is not null"
				" and alerttype=%d"
				" and acknowledgeid is null"
				" and (eventid=" TRX_FS_UI64,
				actionid, ALERT_TYPE_MESSAGE, event->eventid);

	if (NULL != r_event)
	{
		message_type = MACRO_TYPE_MESSAGE_RECOVERY;
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " or eventid=" TRX_FS_UI64, r_event->eventid);
	}
	else
		message_type = MACRO_TYPE_MESSAGE_NORMAL;

	trx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');

	if (NULL != ack)
		message_type = MACRO_TYPE_MESSAGE_ACK;

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_DBROW2UINT64(userid, row[0]);

		/* exclude acknowledgement author from the recipient list */
		if (NULL != ack && ack->userid == userid)
			continue;

		if (SUCCEED != check_perm2system(userid))
			continue;

		TRX_STR2UINT64(mediatypeid, row[1]);

		switch (event->object)
		{
			case EVENT_OBJECT_TRIGGER:
				if (PERM_READ > get_trigger_permission(userid, event))
					continue;
				break;
			case EVENT_OBJECT_ITEM:
			case EVENT_OBJECT_LLDRULE:
				if (PERM_READ > get_item_permission(userid, event->objectid))
					continue;
				break;
		}

		subject_dyn = trx_strdup(NULL, subject);
		message_dyn = trx_strdup(NULL, message);

		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL, NULL,
				ack, &subject_dyn, message_type, NULL, 0);
		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL, NULL,
				ack, &message_dyn, message_type, NULL, 0);

		add_user_msg(userid, mediatypeid, user_msg, subject_dyn, message_dyn);

		trx_free(subject_dyn);
		trx_free(message_dyn);
	}
	DBfree_result(result);

	trx_free(sql);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: add_sentusers_ack_msg                                            *
 *                                                                            *
 * Purpose: adds message to be sent to all who added acknowlegment and        *
 *          involved in discussion                                            *
 *                                                                            *
 * Parameters: user_msg    - [IN/OUT] the message list                        *
 *             actionid    - [IN] the action identifie                        *
 *             mediatypeid - [IN] the media type id defined for the operation *
 *             event       - [IN] the event                                   *
 *             ack         - [IN] the acknowlegment                           *
 *             subject     - [IN] the message subject                         *
 *             message     - [IN] the message body                            *
 *                                                                            *
 ******************************************************************************/
static void	add_sentusers_ack_msg(TRX_USER_MSG **user_msg, trx_uint64_t actionid, trx_uint64_t mediatypeid,
		const DB_EVENT *event, const DB_EVENT *r_event, const DB_ACKNOWLEDGE *ack, const char *subject,
		const char *message)
{
	char		*subject_dyn, *message_dyn;
	DB_RESULT	result;
	DB_ROW		row;
	trx_uint64_t	userid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select distinct userid"
			" from acknowledges"
			" where eventid=" TRX_FS_UI64,
			event->eventid);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_DBROW2UINT64(userid, row[0]);

		/* exclude acknowledgement author from the recipient list */
		if (ack->userid == userid)
			continue;

		if (SUCCEED != check_perm2system(userid) || PERM_READ > get_trigger_permission(userid, event))
			continue;

		subject_dyn = trx_strdup(NULL, subject);
		message_dyn = trx_strdup(NULL, message);

		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL,
				NULL, ack, &subject_dyn, MACRO_TYPE_MESSAGE_ACK, NULL, 0);
		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL,
				NULL, ack, &message_dyn, MACRO_TYPE_MESSAGE_ACK, NULL, 0);

		add_user_msg(userid, mediatypeid, user_msg, subject_dyn, message_dyn);

		trx_free(subject_dyn);
		trx_free(message_dyn);
	}
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	flush_user_msg(TRX_USER_MSG **user_msg, int esc_step, const DB_EVENT *event, const DB_EVENT *r_event,
		trx_uint64_t actionid, const DB_ACKNOWLEDGE *ack)
{
	TRX_USER_MSG	*p;

	while (NULL != *user_msg)
	{
		p = *user_msg;
		*user_msg = (TRX_USER_MSG *)(*user_msg)->next;

		add_message_alert(event, r_event, actionid, esc_step, p->userid, p->mediatypeid, p->subject,
				p->message, ack);

		trx_free(p->subject);
		trx_free(p->message);
		trx_free(p);
	}
}

static void	add_command_alert(trx_db_insert_t *db_insert, int alerts_num, trx_uint64_t alertid, const DC_HOST *host,
		const DB_EVENT *event, const DB_EVENT *r_event, trx_uint64_t actionid, int esc_step,
		const char *command, trx_alert_status_t status, const char *error)
{
	int	now, alerttype = ALERT_TYPE_COMMAND, alert_status = status;
	char	*tmp = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == alerts_num)
	{
		trx_db_insert_prepare(db_insert, "alerts", "alertid", "actionid", "eventid", "clock", "message",
				"status", "error", "esc_step", "alerttype", (NULL != r_event ? "p_eventid" : NULL),
				NULL);
	}

	now = (int)time(NULL);
	tmp = trx_dsprintf(tmp, "%s:%s", host->host, TRX_NULL2EMPTY_STR(command));

	if (NULL == r_event)
	{
		trx_db_insert_add_values(db_insert, alertid, actionid, event->eventid, now, tmp, alert_status,
				error, esc_step, (int)alerttype);
	}
	else
	{
		trx_db_insert_add_values(db_insert, alertid, actionid, r_event->eventid, now, tmp, alert_status,
				error, esc_step, (int)alerttype, event->eventid);
	}

	trx_free(tmp);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

#ifdef HAVE_OPENIPMI
#	define TRX_IPMI_FIELDS_NUM	4	/* number of selected IPMI-related fields in functions */
						/* get_dynamic_hostid() and execute_commands() */
#else
#	define TRX_IPMI_FIELDS_NUM	0
#endif

static int	get_dynamic_hostid(const DB_EVENT *event, DC_HOST *host, char *error, size_t max_error_len)
{
	DB_RESULT	result;
	DB_ROW		row;
	char		sql[512];	/* do not forget to adjust size if SQLs change */
	size_t		offset;
	int		ret = SUCCEED;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	offset = trx_snprintf(sql, sizeof(sql), "select distinct h.hostid,h.proxy_hostid,h.host,h.tls_connect");
#ifdef HAVE_OPENIPMI
	offset += trx_snprintf(sql + offset, sizeof(sql) - offset,
			/* do not forget to update TRX_IPMI_FIELDS_NUM if number of selected IPMI fields changes */
			",h.ipmi_authtype,h.ipmi_privilege,h.ipmi_username,h.ipmi_password");
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	offset += trx_snprintf(sql + offset, sizeof(sql) - offset,
			",h.tls_issuer,h.tls_subject,h.tls_psk_identity,h.tls_psk");
#endif
	switch (event->source)
	{
		case EVENT_SOURCE_TRIGGERS:
			trx_snprintf(sql + offset, sizeof(sql) - offset,
					" from functions f,items i,hosts h"
					" where f.itemid=i.itemid"
						" and i.hostid=h.hostid"
						" and h.status=%d"
						" and f.triggerid=" TRX_FS_UI64,
					HOST_STATUS_MONITORED, event->objectid);

			break;
		case EVENT_SOURCE_DISCOVERY:
			offset += trx_snprintf(sql + offset, sizeof(sql) - offset,
					" from hosts h,interface i,dservices ds"
					" where h.hostid=i.hostid"
						" and i.ip=ds.ip"
						" and i.useip=1"
						" and h.status=%d",
						HOST_STATUS_MONITORED);

			switch (event->object)
			{
				case EVENT_OBJECT_DHOST:
					trx_snprintf(sql + offset, sizeof(sql) - offset,
							" and ds.dhostid=" TRX_FS_UI64, event->objectid);
					break;
				case EVENT_OBJECT_DSERVICE:
					trx_snprintf(sql + offset, sizeof(sql) - offset,
							" and ds.dserviceid=" TRX_FS_UI64, event->objectid);
					break;
			}
			break;
		case EVENT_SOURCE_AUTO_REGISTRATION:
			trx_snprintf(sql + offset, sizeof(sql) - offset,
					" from autoreg_host a,hosts h"
					" where " TRX_SQL_NULLCMP("a.proxy_hostid", "h.proxy_hostid")
						" and a.host=h.host"
						" and h.status=%d"
						" and h.flags<>%d"
						" and a.autoreg_hostid=" TRX_FS_UI64,
					HOST_STATUS_MONITORED, TRX_FLAG_DISCOVERY_PROTOTYPE, event->objectid);
			break;
		default:
			trx_snprintf(error, max_error_len, "Unsupported event source [%d]", event->source);
			return FAIL;
	}

	host->hostid = 0;

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		if (0 != host->hostid)
		{
			switch (event->source)
			{
				case EVENT_SOURCE_TRIGGERS:
					trx_strlcpy(error, "Too many hosts in a trigger expression", max_error_len);
					break;
				case EVENT_SOURCE_DISCOVERY:
					trx_strlcpy(error, "Too many hosts with same IP addresses", max_error_len);
					break;
			}
			ret = FAIL;
			break;
		}

		TRX_STR2UINT64(host->hostid, row[0]);
		TRX_DBROW2UINT64(host->proxy_hostid, row[1]);
		strscpy(host->host, row[2]);
		TRX_STR2UCHAR(host->tls_connect, row[3]);

#ifdef HAVE_OPENIPMI
		host->ipmi_authtype = (signed char)atoi(row[4]);
		host->ipmi_privilege = (unsigned char)atoi(row[5]);
		strscpy(host->ipmi_username, row[6]);
		strscpy(host->ipmi_password, row[7]);
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		strscpy(host->tls_issuer, row[4 + TRX_IPMI_FIELDS_NUM]);
		strscpy(host->tls_subject, row[5 + TRX_IPMI_FIELDS_NUM]);
		strscpy(host->tls_psk_identity, row[6 + TRX_IPMI_FIELDS_NUM]);
		strscpy(host->tls_psk, row[7 + TRX_IPMI_FIELDS_NUM]);
#endif
	}
	DBfree_result(result);

	if (FAIL == ret)
	{
		host->hostid = 0;
		*host->host = '\0';
	}
	else if (0 == host->hostid)
	{
		*host->host = '\0';

		trx_strlcpy(error, "Cannot find a corresponding host", max_error_len);
		ret = FAIL;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_operation_groupids                                           *
 *                                                                            *
 * Purpose: get groups (including nested groups) used by an operation         *
 *                                                                            *
 * Parameters: operationid - [IN] the operation id                            *
 *             groupids    - [OUT] the group ids                              *
 *                                                                            *
 ******************************************************************************/
static void	get_operation_groupids(trx_uint64_t operationid, trx_vector_uint64_t *groupids)
{
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_vector_uint64_t	parent_groupids;

	trx_vector_uint64_create(&parent_groupids);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select groupid from opcommand_grp where operationid=" TRX_FS_UI64, operationid);

	DBselect_uint64(sql, &parent_groupids);

	trx_dc_get_nested_hostgroupids(parent_groupids.values, parent_groupids.values_num, groupids);

	trx_free(sql);
	trx_vector_uint64_destroy(&parent_groupids);
}

static void	execute_commands(const DB_EVENT *event, const DB_EVENT *r_event, const DB_ACKNOWLEDGE *ack,
		trx_uint64_t actionid, trx_uint64_t operationid, int esc_step, int macro_type)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_db_insert_t		db_insert;
	int			alerts_num = 0;
	char			*buffer = NULL;
	size_t			buffer_alloc = 2 * TRX_KIBIBYTE, buffer_offset = 0;
	trx_vector_uint64_t	executed_on_hosts, groupids;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	buffer = (char *)trx_malloc(buffer, buffer_alloc);

	/* get hosts operation's hosts */

	trx_vector_uint64_create(&groupids);
	get_operation_groupids(operationid, &groupids);

	if (0 != groupids.values_num)
	{
		trx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
				/* the 1st 'select' works if remote command target is "Host group" */
				"select distinct h.hostid,h.proxy_hostid,h.host,o.type,o.scriptid,o.execute_on,o.port"
					",o.authtype,o.username,o.password,o.publickey,o.privatekey,o.command,h.tls_connect"
#ifdef HAVE_OPENIPMI
				/* do not forget to update TRX_IPMI_FIELDS_NUM if number of selected IPMI fields changes */
				",h.ipmi_authtype,h.ipmi_privilege,h.ipmi_username,h.ipmi_password"
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
				",h.tls_issuer,h.tls_subject,h.tls_psk_identity,h.tls_psk"
#endif
				);

		trx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset,
				" from opcommand o,hosts_groups hg,hosts h"
				" where o.operationid=" TRX_FS_UI64
					" and hg.hostid=h.hostid"
					" and h.status=%d"
					" and",
				operationid, HOST_STATUS_MONITORED);

		DBadd_condition_alloc(&buffer, &buffer_alloc, &buffer_offset, "hg.groupid", groupids.values,
				groupids.values_num);

		trx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset, " union ");
	}

	trx_vector_uint64_destroy(&groupids);

	trx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
			/* the 2nd 'select' works if remote command target is "Host" */
			"select distinct h.hostid,h.proxy_hostid,h.host,o.type,o.scriptid,o.execute_on,o.port"
				",o.authtype,o.username,o.password,o.publickey,o.privatekey,o.command,h.tls_connect"
#ifdef HAVE_OPENIPMI
			",h.ipmi_authtype,h.ipmi_privilege,h.ipmi_username,h.ipmi_password"
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
			",h.tls_issuer,h.tls_subject,h.tls_psk_identity,h.tls_psk"
#endif
			);
	trx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset,
			" from opcommand o,opcommand_hst oh,hosts h"
			" where o.operationid=oh.operationid"
				" and oh.hostid=h.hostid"
				" and o.operationid=" TRX_FS_UI64
				" and h.status=%d"
			" union "
			/* the 3rd 'select' works if remote command target is "Current host" */
			"select distinct 0,0,null,o.type,o.scriptid,o.execute_on,o.port"
				",o.authtype,o.username,o.password,o.publickey,o.privatekey,o.command,%d",
			operationid, HOST_STATUS_MONITORED, TRX_TCP_SEC_UNENCRYPTED);
#ifdef HAVE_OPENIPMI
	trx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
				",0,2,null,null");
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
				",null,null,null,null");
#endif
	trx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset,
			" from opcommand o,opcommand_hst oh"
			" where o.operationid=oh.operationid"
				" and o.operationid=" TRX_FS_UI64
				" and oh.hostid is null",
			operationid);

	result = DBselect("%s", buffer);

	trx_free(buffer);
	trx_vector_uint64_create(&executed_on_hosts);

	while (NULL != (row = DBfetch(result)))
	{
		int			rc = SUCCEED;
		char			error[ALERT_ERROR_LEN_MAX];
		DC_HOST			host;
		trx_script_t		script;
		trx_alert_status_t	status = ALERT_STATUS_NOT_SENT;
		trx_uint64_t		alertid;

		*error = '\0';
		memset(&host, 0, sizeof(host));
		trx_script_init(&script);

		script.type = (unsigned char)atoi(row[3]);

		if (TRX_SCRIPT_TYPE_GLOBAL_SCRIPT != script.type)
		{
			script.command = trx_strdup(script.command, row[12]);
			substitute_simple_macros(&actionid, event, r_event, NULL, NULL,
					NULL, NULL, NULL, ack, &script.command, macro_type, NULL, 0);
		}

		if (TRX_SCRIPT_TYPE_CUSTOM_SCRIPT == script.type)
			script.execute_on = (unsigned char)atoi(row[5]);

		TRX_STR2UINT64(host.hostid, row[0]);
		TRX_DBROW2UINT64(host.proxy_hostid, row[1]);

		if (TRX_SCRIPT_EXECUTE_ON_SERVER != script.execute_on)
		{
			if (0 != host.hostid)
			{
				if (FAIL != trx_vector_uint64_search(&executed_on_hosts, host.hostid,
						TRX_DEFAULT_UINT64_COMPARE_FUNC))
				{
					goto skip;
				}

				trx_vector_uint64_append(&executed_on_hosts, host.hostid);
				strscpy(host.host, row[2]);
				host.tls_connect = (unsigned char)atoi(row[13]);
#ifdef HAVE_OPENIPMI
				host.ipmi_authtype = (signed char)atoi(row[14]);
				host.ipmi_privilege = (unsigned char)atoi(row[15]);
				strscpy(host.ipmi_username, row[16]);
				strscpy(host.ipmi_password, row[17]);
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
				strscpy(host.tls_issuer, row[14 + TRX_IPMI_FIELDS_NUM]);
				strscpy(host.tls_subject, row[15 + TRX_IPMI_FIELDS_NUM]);
				strscpy(host.tls_psk_identity, row[16 + TRX_IPMI_FIELDS_NUM]);
				strscpy(host.tls_psk, row[17 + TRX_IPMI_FIELDS_NUM]);
#endif
			}
			else if (SUCCEED == (rc = get_dynamic_hostid((NULL != r_event ? r_event : event), &host, error,
						sizeof(error))))
			{
				if (FAIL != trx_vector_uint64_search(&executed_on_hosts, host.hostid,
						TRX_DEFAULT_UINT64_COMPARE_FUNC))
				{
					goto skip;
				}

				trx_vector_uint64_append(&executed_on_hosts, host.hostid);
			}
		}
		else
			trx_strlcpy(host.host, "Treegix server", sizeof(host.host));

		alertid = DBget_maxid("alerts");

		if (SUCCEED == rc)
		{
			switch (script.type)
			{
				case TRX_SCRIPT_TYPE_SSH:
					script.authtype = (unsigned char)atoi(row[7]);
					script.publickey = trx_strdup(script.publickey, row[10]);
					script.privatekey = trx_strdup(script.privatekey, row[11]);
					TRX_FALLTHROUGH;
				case TRX_SCRIPT_TYPE_TELNET:
					script.port = trx_strdup(script.port, row[6]);
					script.username = trx_strdup(script.username, row[8]);
					script.password = trx_strdup(script.password, row[9]);
					break;
				case TRX_SCRIPT_TYPE_GLOBAL_SCRIPT:
					TRX_DBROW2UINT64(script.scriptid, row[4]);
					break;
			}

			if (SUCCEED == (rc = trx_script_prepare(&script, &host, NULL, error, sizeof(error))))
			{
				if (0 == host.proxy_hostid || TRX_SCRIPT_EXECUTE_ON_SERVER == script.execute_on)
				{
					rc = trx_script_execute(&script, &host, NULL, error, sizeof(error));
					status = ALERT_STATUS_SENT;
				}
				else
				{
					if (0 == trx_script_create_task(&script, &host, alertid, time(NULL)))
						rc = FAIL;
				}
			}
		}

		if (FAIL == rc)
			status = ALERT_STATUS_FAILED;

		add_command_alert(&db_insert, alerts_num++, alertid, &host, event, r_event, actionid, esc_step,
				script.command, status, error);
skip:
		trx_script_clean(&script);
	}
	DBfree_result(result);
	trx_vector_uint64_destroy(&executed_on_hosts);

	if (0 < alerts_num)
	{
		trx_db_insert_execute(&db_insert);
		trx_db_insert_clean(&db_insert);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

#undef TRX_IPMI_FIELDS_NUM

static void	get_mediatype_params(const DB_EVENT *event, const DB_EVENT *r_event, trx_uint64_t actionid,
		trx_uint64_t userid, trx_uint64_t mediatypeid, const char *sendto, const char *subject,
		const char *message, const DB_ACKNOWLEDGE *ack, char **params)
{
	DB_RESULT	result;
	DB_ROW		row;
	DB_ALERT	alert = {.sendto = (char *)sendto, .subject = (char *)subject, .message = (char *)message};
	struct trx_json	json;
	char		*name, *value;
	int		message_type;

	if (NULL != ack)
		message_type = MACRO_TYPE_MESSAGE_ACK;
	else
		message_type = (NULL != r_event ? MACRO_TYPE_MESSAGE_RECOVERY : MACRO_TYPE_MESSAGE_NORMAL);

	trx_json_init(&json, 1024);

	result = DBselect("select name,value from media_type_param where mediatypeid=" TRX_FS_UI64, mediatypeid);
	while (NULL != (row = DBfetch(result)))
	{
		name = trx_strdup(NULL, row[0]);
		value = trx_strdup(NULL, row[1]);

		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL, &alert,
				ack, &name, message_type, NULL, 0);
		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL, &alert,
				ack, &value, message_type, NULL, 0);

		trx_json_addstring(&json, name, value, TRX_JSON_TYPE_STRING);
		trx_free(name);
		trx_free(value);

	}
	DBfree_result(result);

	*params = trx_strdup(NULL, json.buffer);
	trx_json_free(&json);
}

static void	add_message_alert(const DB_EVENT *event, const DB_EVENT *r_event, trx_uint64_t actionid, int esc_step,
		trx_uint64_t userid, trx_uint64_t mediatypeid, const char *subject, const char *message,
		const DB_ACKNOWLEDGE *ack)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		now, priority, have_alerts = 0, res;
	trx_db_insert_t	db_insert;
	trx_uint64_t	ackid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	now = time(NULL);
	ackid = (NULL == ack ? 0 : ack->acknowledgeid);

	if (0 == mediatypeid)
	{
		result = DBselect(
				"select m.mediatypeid,m.sendto,m.severity,m.period,mt.status,m.active"
				" from media m,media_type mt"
				" where m.mediatypeid=mt.mediatypeid"
					" and m.userid=" TRX_FS_UI64,
				userid);
	}
	else
	{
		result = DBselect(
				"select m.mediatypeid,m.sendto,m.severity,m.period,mt.status,m.active"
				" from media m,media_type mt"
				" where m.mediatypeid=mt.mediatypeid"
					" and m.userid=" TRX_FS_UI64
					" and m.mediatypeid=" TRX_FS_UI64,
				userid, mediatypeid);
	}

	mediatypeid = 0;
	priority = EVENT_SOURCE_TRIGGERS == event->source ? event->trigger.priority : TRIGGER_SEVERITY_NOT_CLASSIFIED;

	while (NULL != (row = DBfetch(result)))
	{
		int		severity, status;
		const char	*perror;
		char		*period = NULL, *params;

		TRX_STR2UINT64(mediatypeid, row[0]);
		severity = atoi(row[2]);
		period = trx_strdup(period, row[3]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &period,
				MACRO_TYPE_COMMON, NULL, 0);

		treegix_log(LOG_LEVEL_DEBUG, "severity:%d, media severity:%d, period:'%s', userid:" TRX_FS_UI64,
				priority, severity, period, userid);

		if (MEDIA_STATUS_DISABLED == atoi(row[5]))
		{
			treegix_log(LOG_LEVEL_DEBUG, "will not send message (user media disabled)");
			continue;
		}

		if (((1 << priority) & severity) == 0)
		{
			treegix_log(LOG_LEVEL_DEBUG, "will not send message (severity)");
			continue;
		}

		if (SUCCEED != trx_check_time_period(period, time(NULL), &res))
		{
			status = ALERT_STATUS_FAILED;
			perror = "Invalid media activity period";
		}
		else if (SUCCEED != res)
		{
			treegix_log(LOG_LEVEL_DEBUG, "will not send message (period)");
			continue;
		}
		else if (MEDIA_TYPE_STATUS_ACTIVE == atoi(row[4]))
		{
			status = ALERT_STATUS_NEW;
			perror = "";
		}
		else
		{
			status = ALERT_STATUS_FAILED;
			perror = "Media type disabled.";
		}

		if (0 == have_alerts)
		{
			have_alerts = 1;
			trx_db_insert_prepare(&db_insert, "alerts", "alertid", "actionid", "eventid", "userid",
					"clock", "mediatypeid", "sendto", "subject", "message", "status", "error",
					"esc_step", "alerttype", "acknowledgeid", "parameters",
					(NULL != r_event ? "p_eventid" : NULL), NULL);
		}

		get_mediatype_params(event, r_event, actionid, userid, mediatypeid, row[1], subject, message, ack,
				&params);

		if (NULL != r_event)
		{
			trx_db_insert_add_values(&db_insert, __UINT64_C(0), actionid, r_event->eventid, userid,
					now, mediatypeid, row[1], subject, message, status, perror, esc_step,
					(int)ALERT_TYPE_MESSAGE, ackid, params, event->eventid);
		}
		else
		{
			trx_db_insert_add_values(&db_insert, __UINT64_C(0), actionid, event->eventid, userid,
					now, mediatypeid, row[1], subject, message, status, perror, esc_step,
					(int)ALERT_TYPE_MESSAGE, ackid, params);
		}

		trx_free(params);
		trx_free(period);
	}

	DBfree_result(result);

	if (0 == mediatypeid)
	{
		char	error[MAX_STRING_LEN];

		have_alerts = 1;

		trx_snprintf(error, sizeof(error), "No media defined for user.");

		trx_db_insert_prepare(&db_insert, "alerts", "alertid", "actionid", "eventid", "userid", "clock",
				"subject", "message", "status", "retries", "error", "esc_step", "alerttype",
				"acknowledgeid", (NULL != r_event ? "p_eventid" : NULL), NULL);

		if (NULL != r_event)
		{
			trx_db_insert_add_values(&db_insert, __UINT64_C(0), actionid, r_event->eventid, userid,
					now, subject, message, (int)ALERT_STATUS_FAILED, (int)ALERT_MAX_RETRIES, error,
					esc_step, (int)ALERT_TYPE_MESSAGE, ackid, event->eventid);
		}
		else
		{
			trx_db_insert_add_values(&db_insert, __UINT64_C(0), actionid, event->eventid, userid,
					now, subject, message, (int)ALERT_STATUS_FAILED, (int)ALERT_MAX_RETRIES, error,
					esc_step, (int)ALERT_TYPE_MESSAGE, ackid);
		}
	}

	if (0 != have_alerts)
	{
		trx_db_insert_autoincrement(&db_insert, "alertid");
		trx_db_insert_execute(&db_insert);
		trx_db_insert_clean(&db_insert);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: check_operation_conditions                                       *
 *                                                                            *
 * Purpose:                                                                   *
 *                                                                            *
 * Parameters: event    - event to check                                      *
 *             actionid - action ID for matching                              *
 *                                                                            *
 * Return value: SUCCEED - matches, FAIL - otherwise                          *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
static int	check_operation_conditions(const DB_EVENT *event, trx_uint64_t operationid, unsigned char evaltype)
{
	DB_RESULT	result;
	DB_ROW		row;
	DB_CONDITION	condition;

	int		ret = SUCCEED; /* SUCCEED required for CONDITION_EVAL_TYPE_AND_OR */
	int		cond, exit = 0;
	unsigned char	old_type = 0xff;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() operationid:" TRX_FS_UI64, __func__, operationid);

	result = DBselect("select conditiontype,operator,value"
				" from opconditions"
				" where operationid=" TRX_FS_UI64
				" order by conditiontype",
			operationid);

	while (NULL != (row = DBfetch(result)) && 0 == exit)
	{
		memset(&condition, 0, sizeof(condition));
		condition.conditiontype	= (unsigned char)atoi(row[0]);
		condition.op = (unsigned char)atoi(row[1]);
		condition.value = row[2];

		switch (evaltype)
		{
			case CONDITION_EVAL_TYPE_AND_OR:
				if (old_type == condition.conditiontype)	/* OR conditions */
				{
					if (SUCCEED == check_action_condition(event, &condition))
						ret = SUCCEED;
				}
				else						/* AND conditions */
				{
					/* Break if PREVIOUS AND condition is FALSE */
					if (ret == FAIL)
						exit = 1;
					else if (FAIL == check_action_condition(event, &condition))
						ret = FAIL;
				}
				old_type = condition.conditiontype;
				break;
			case CONDITION_EVAL_TYPE_AND:
				cond = check_action_condition(event, &condition);
				/* Break if any of AND conditions is FALSE */
				if (cond == FAIL)
				{
					ret = FAIL;
					exit = 1;
				}
				else
					ret = SUCCEED;
				break;
			case CONDITION_EVAL_TYPE_OR:
				cond = check_action_condition(event, &condition);
				/* Break if any of OR conditions is TRUE */
				if (cond == SUCCEED)
				{
					ret = SUCCEED;
					exit = 1;
				}
				else
					ret = FAIL;
				break;
			default:
				ret = FAIL;
				exit = 1;
				break;
		}
	}
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static void	escalation_execute_operations(DB_ESCALATION *escalation, const DB_EVENT *event, const DB_ACTION *action)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		next_esc_period = 0, esc_period, default_esc_period;
	TRX_USER_MSG	*user_msg = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	default_esc_period = 0 == action->esc_period ? SEC_PER_HOUR : action->esc_period;
	escalation->esc_step++;

	result = DBselect(
			"select o.operationid,o.operationtype,o.esc_period,o.evaltype,m.operationid,m.default_msg,"
				"m.subject,m.message,m.mediatypeid"
			" from operations o"
				" left join opmessage m"
					" on m.operationid=o.operationid"
			" where o.actionid=" TRX_FS_UI64
				" and o.operationtype in (%d,%d)"
				" and o.esc_step_from<=%d"
				" and (o.esc_step_to=0 or o.esc_step_to>=%d)"
				" and o.recovery=%d",
			action->actionid,
			OPERATION_TYPE_MESSAGE, OPERATION_TYPE_COMMAND,
			escalation->esc_step,
			escalation->esc_step,
			TRX_OPERATION_MODE_NORMAL);

	while (NULL != (row = DBfetch(result)))
	{
		char		*tmp;
		trx_uint64_t	operationid;

		TRX_STR2UINT64(operationid, row[0]);

		tmp = trx_strdup(NULL, row[2]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &tmp, MACRO_TYPE_COMMON,
				NULL, 0);
		if (SUCCEED != is_time_suffix(tmp, &esc_period, TRX_LENGTH_UNLIMITED))
		{
			treegix_log(LOG_LEVEL_WARNING, "Invalid step duration \"%s\" for operation of action \"%s\","
					" using default operation step duration of the action", tmp, action->name);
			esc_period = 0;
		}
		trx_free(tmp);

		if (0 == esc_period)
			esc_period = default_esc_period;

		if (0 == next_esc_period || next_esc_period > esc_period)
			next_esc_period = esc_period;

		if (SUCCEED == check_operation_conditions(event, operationid, (unsigned char)atoi(row[3])))
		{
			char		*subject, *message;
			trx_uint64_t	mediatypeid;

			treegix_log(LOG_LEVEL_DEBUG, "Conditions match our event. Execute operation.");

			switch (atoi(row[1]))
			{
				case OPERATION_TYPE_MESSAGE:
					if (SUCCEED == DBis_null(row[4]))
						break;

					TRX_DBROW2UINT64(mediatypeid, row[8]);

					if (0 == atoi(row[5]))
					{
						subject = row[6];
						message = row[7];
					}
					else
					{
						subject = action->shortdata;
						message = action->longdata;
					}

					add_object_msg(action->actionid, operationid, mediatypeid, &user_msg,
							subject, message, event, NULL, NULL, MACRO_TYPE_MESSAGE_NORMAL);
					break;
				case OPERATION_TYPE_COMMAND:
					execute_commands(event, NULL, NULL, action->actionid, operationid,
							escalation->esc_step, MACRO_TYPE_MESSAGE_NORMAL);
					break;
			}
		}
		else
			treegix_log(LOG_LEVEL_DEBUG, "Conditions do not match our event. Do not execute operation.");
	}
	DBfree_result(result);

	flush_user_msg(&user_msg, escalation->esc_step, event, NULL, action->actionid, NULL);

	if (EVENT_SOURCE_TRIGGERS == action->eventsource || EVENT_SOURCE_INTERNAL == action->eventsource)
	{
		char	*sql;

		sql = trx_dsprintf(NULL,
				"select null"
				" from operations"
				" where actionid=" TRX_FS_UI64
					" and (esc_step_to>%d or esc_step_to=0)"
					" and recovery=%d",
					action->actionid, escalation->esc_step, TRX_OPERATION_MODE_NORMAL);
		result = DBselectN(sql, 1);

		if (NULL != DBfetch(result))
		{
			next_esc_period = (0 != next_esc_period) ? next_esc_period : default_esc_period;
			escalation->nextcheck = time(NULL) + next_esc_period;
		}
		else if (TRX_ACTION_RECOVERY_OPERATIONS == action->recovery)
		{
			escalation->status = ESCALATION_STATUS_SLEEP;
			escalation->nextcheck = time(NULL) + default_esc_period;
		}
		else
			escalation->status = ESCALATION_STATUS_COMPLETED;

		DBfree_result(result);
		trx_free(sql);
	}
	else
		escalation->status = ESCALATION_STATUS_COMPLETED;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: escalation_execute_recovery_operations                           *
 *                                                                            *
 * Purpose: execute escalation recovery operations                            *
 *                                                                            *
 * Parameters: event      - [IN] the event                                    *
 *             r_event    - [IN] the recovery event                           *
 *             action     - [IN] the action                                   *
 *                                                                            *
 * Comments: Action recovery operations have a single escalation step, so     *
 *           alerts created by escalation recovery operations must have       *
 *           esc_step field set to 1.                                         *
 *                                                                            *
 ******************************************************************************/
static void	escalation_execute_recovery_operations(const DB_EVENT *event, const DB_EVENT *r_event,
		const DB_ACTION *action)
{
	DB_RESULT	result;
	DB_ROW		row;
	TRX_USER_MSG	*user_msg = NULL;
	trx_uint64_t	operationid;
	unsigned char	operationtype, default_msg;
	char		*subject, *message;
	trx_uint64_t	mediatypeid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select o.operationid,o.operationtype,"
				"m.operationid,m.default_msg,m.subject,m.message,m.mediatypeid"
			" from operations o"
				" left join opmessage m"
					" on m.operationid=o.operationid"
			" where o.actionid=" TRX_FS_UI64
				" and o.operationtype in (%d,%d,%d)"
				" and o.recovery=%d",
			action->actionid,
			OPERATION_TYPE_MESSAGE, OPERATION_TYPE_COMMAND, OPERATION_TYPE_RECOVERY_MESSAGE,
			TRX_OPERATION_MODE_RECOVERY);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(operationid, row[0]);
		operationtype = (unsigned char)atoi(row[1]);

		switch (operationtype)
		{
			case OPERATION_TYPE_MESSAGE:
				if (SUCCEED == DBis_null(row[2]))
					break;

				TRX_STR2UCHAR(default_msg, row[3]);
				TRX_DBROW2UINT64(mediatypeid, row[6]);

				if (0 == default_msg)
				{
					subject = row[4];
					message = row[5];
				}
				else
				{
					subject = action->r_shortdata;
					message = action->r_longdata;
				}

				add_object_msg(action->actionid, operationid, mediatypeid, &user_msg, subject,
						message, event, r_event, NULL, MACRO_TYPE_MESSAGE_RECOVERY);
				break;
			case OPERATION_TYPE_RECOVERY_MESSAGE:
				if (SUCCEED == DBis_null(row[2]))
					break;

				TRX_STR2UCHAR(default_msg, row[3]);

				if (0 == default_msg)
				{
					subject = row[4];
					message = row[5];
				}
				else
				{
					subject = action->r_shortdata;
					message = action->r_longdata;
				}

				add_sentusers_msg(&user_msg, action->actionid, event, r_event, subject, message, NULL);
				break;
			case OPERATION_TYPE_COMMAND:
				execute_commands(event, r_event, NULL, action->actionid, operationid, 1,
						MACRO_TYPE_MESSAGE_RECOVERY);
				break;
		}
	}
	DBfree_result(result);

	flush_user_msg(&user_msg, 1, event, r_event, action->actionid, NULL);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: escalation_execute_acknowledge_operations                        *
 *                                                                            *
 * Purpose: execute escalation acknowledge operations                         *
 *                                                                            *
 * Parameters: event  - [IN] the event                                        *
 *             action - [IN] the action                                       *
 *             ack    - [IN] the acknowledge                                  *
 *                                                                            *
 * Comments: Action acknowledge operations have a single escalation step, so  *
 *           alerts created by escalation acknowledge operations must have    *
 *           esc_step field set to 1.                                         *
 *                                                                            *
 ******************************************************************************/
static void	escalation_execute_acknowledge_operations(const DB_EVENT *event, const DB_EVENT *r_event,
		const DB_ACTION *action, const DB_ACKNOWLEDGE *ack)
{
	DB_RESULT	result;
	DB_ROW		row;
	TRX_USER_MSG	*user_msg = NULL;
	trx_uint64_t	operationid, mediatypeid;
	unsigned char	operationtype, default_msg;
	char		*subject, *message;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select o.operationid,o.operationtype,m.operationid,m.default_msg,"
				"m.subject,m.message,m.mediatypeid"
			" from operations o"
				" left join opmessage m"
					" on m.operationid=o.operationid"
			" where o.actionid=" TRX_FS_UI64
				" and o.operationtype in (%d,%d,%d)"
				" and o.recovery=%d",
			action->actionid,
			OPERATION_TYPE_MESSAGE, OPERATION_TYPE_COMMAND, OPERATION_TYPE_ACK_MESSAGE,
			TRX_OPERATION_MODE_ACK);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(operationid, row[0]);
		operationtype = (unsigned char)atoi(row[1]);

		switch (operationtype)
		{
			case OPERATION_TYPE_MESSAGE:
				if (SUCCEED == DBis_null(row[2]))
					break;

				TRX_STR2UCHAR(default_msg, row[3]);
				TRX_DBROW2UINT64(mediatypeid, row[6]);

				if (0 == default_msg)
				{
					subject = row[4];
					message = row[5];
				}
				else
				{
					subject = action->ack_shortdata;
					message = action->ack_longdata;
				}

				add_object_msg(action->actionid, operationid, mediatypeid, &user_msg, subject,
						message, event, r_event, ack, MACRO_TYPE_MESSAGE_ACK);
				break;
			case OPERATION_TYPE_ACK_MESSAGE:
				if (SUCCEED == DBis_null(row[2]))
					break;

				TRX_STR2UCHAR(default_msg, row[3]);
				TRX_DBROW2UINT64(mediatypeid, row[6]);

				if (0 == default_msg)
				{
					subject = row[4];
					message = row[5];
				}
				else
				{
					subject = action->ack_shortdata;
					message = action->ack_longdata;
				}

				add_sentusers_msg(&user_msg, action->actionid, event, r_event, subject, message, ack);
				add_sentusers_ack_msg(&user_msg, action->actionid, mediatypeid, event, r_event, ack,
						subject, message);
				break;
			case OPERATION_TYPE_COMMAND:
				execute_commands(event, r_event, ack, action->actionid, operationid, 1,
						MACRO_TYPE_MESSAGE_ACK);
				break;
		}
	}
	DBfree_result(result);

	flush_user_msg(&user_msg, 1, event, NULL, action->actionid, ack);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: check_escalation_trigger                                         *
 *                                                                            *
 * Purpose: check whether the escalation trigger and related items, hosts are *
 *          not deleted or disabled.                                          *
 *                                                                            *
 * Parameters: triggerid   - [IN] the id of trigger to check                  *
 *             source      - [IN] the escalation event source                 *
 *             ignore      - [OUT] 1 - the escalation must be ignored because *
 *                                     of dependent trigger being in PROBLEM  *
 *                                     state,                                 *
 *                                 0 - otherwise                              *
 *             error       - [OUT] message in case escalation is cancelled    *
 *                                                                            *
 * Return value: FAIL if dependent trigger is in PROBLEM state                *
 *               SUCCEED otherwise                                            *
 *                                                                            *
 ******************************************************************************/
static int	check_escalation_trigger(trx_uint64_t triggerid, unsigned char source, unsigned char *ignore,
		char **error)
{
	DC_TRIGGER		trigger;
	trx_vector_uint64_t	functionids, itemids;
	DC_ITEM			*items = NULL;
	DC_FUNCTION		*functions = NULL;
	int			i, errcode, *errcodes = NULL, ret = FAIL;

	/* trigger disabled or deleted? */
	DCconfig_get_triggers_by_triggerids(&trigger, &triggerid, &errcode, 1);

	if (SUCCEED != errcode)
	{
		goto out;
	}
	else if (TRIGGER_STATUS_DISABLED == trigger.status)
	{
		*error = trx_dsprintf(*error, "trigger \"%s\" disabled.", trigger.description);
		goto out;
	}

	if (EVENT_SOURCE_TRIGGERS != source)
	{
		/* don't check dependency for internal trigger events */
		ret = SUCCEED;
		goto out;
	}

	/* check items and hosts referenced by trigger expression */
	trx_vector_uint64_create(&functionids);
	trx_vector_uint64_create(&itemids);

	get_functionids(&functionids, trigger.expression_orig);

	functions = (DC_FUNCTION *)trx_malloc(functions, sizeof(DC_FUNCTION) * functionids.values_num);
	errcodes = (int *)trx_malloc(errcodes, sizeof(int) * functionids.values_num);

	DCconfig_get_functions_by_functionids(functions, functionids.values, errcodes, functionids.values_num);

	for (i = 0; i < functionids.values_num; i++)
	{
		if (SUCCEED == errcodes[i])
			trx_vector_uint64_append(&itemids, functions[i].itemid);
	}

	DCconfig_clean_functions(functions, errcodes, functionids.values_num);
	trx_free(functions);

	trx_vector_uint64_sort(&itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_uniq(&itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	items = (DC_ITEM *)trx_malloc(items, sizeof(DC_ITEM) * itemids.values_num);
	errcodes = (int *)trx_realloc(errcodes, sizeof(int) * itemids.values_num);

	DCconfig_get_items_by_itemids(items, itemids.values, errcodes, itemids.values_num);

	for (i = 0; i < itemids.values_num; i++)
	{
		if (SUCCEED != errcodes[i])
		{
			*error = trx_dsprintf(*error, "item id:" TRX_FS_UI64 " deleted.", itemids.values[i]);
			break;
		}

		if (ITEM_STATUS_DISABLED == items[i].status)
		{
			*error = trx_dsprintf(*error, "item \"%s\" disabled.", items[i].key_orig);
			break;
		}
		if (HOST_STATUS_NOT_MONITORED == items[i].host.status)
		{
			*error = trx_dsprintf(*error, "host \"%s\" disabled.", items[i].host.host);
			break;
		}
	}

	DCconfig_clean_items(items, errcodes, itemids.values_num);
	trx_free(items);
	trx_free(errcodes);

	trx_vector_uint64_destroy(&itemids);
	trx_vector_uint64_destroy(&functionids);

	if (NULL != *error)
		goto out;

	*ignore = (SUCCEED == DCconfig_check_trigger_dependencies(trigger.triggerid) ? 0 : 1);

	ret = SUCCEED;
out:
	DCconfig_clean_triggers(&trigger, &errcode, 1);

	return ret;
}

static const char	*check_escalation_result_string(int result)
{
	switch (result)
	{
		case TRX_ESCALATION_CANCEL:
			return "cancel";
		case TRX_ESCALATION_DELETE:
			return "delete";
		case TRX_ESCALATION_SKIP:
			return "skip";
		case TRX_ESCALATION_PROCESS:
			return "process";
		case TRX_ESCALATION_SUPPRESS:
			return "suppress";
		default:
			return "unknown";
	}
}

/******************************************************************************
 *                                                                            *
 * Function: check_escalation                                                 *
 *                                                                            *
 * Purpose: check whether escalation must be cancelled, deleted, skipped or   *
 *          processed.                                                        *
 *                                                                            *
 * Parameters: escalation - [IN]  escalation to check                         *
 *             action     - [IN]  action responsible for the escalation       *
 *             error      - [OUT] message in case escalation is cancelled     *
 *                                                                            *
 * Return value: TRX_ESCALATION_CANCEL   - the relevant event, item, trigger  *
 *                                         or host is disabled or deleted     *
 *               TRX_ESCALATION_DELETE   - escalations was created and        *
 *                                         recovered during maintenance       *
 *               TRX_ESCALATION_SKIP     - escalation is paused during        *
 *                                         maintenance or dependable trigger  *
 *                                         in problem state                   *
 *               TRX_ESCALATION_SUPPRESS - escalation was created before      *
 *                                         maintenance period                 *
 *               TRX_ESCALATION_PROCESS  - otherwise                          *
 *                                                                            *
 ******************************************************************************/
static int	check_escalation(const DB_ESCALATION *escalation, const DB_ACTION *action, const DB_EVENT *event,
		char **error)
{
	DC_ITEM		item;
	int		errcode, ret = TRX_ESCALATION_CANCEL;
	unsigned char	maintenance = HOST_MAINTENANCE_STATUS_OFF, skip = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() escalationid:" TRX_FS_UI64 " status:%s",
			__func__, escalation->escalationid, trx_escalation_status_string(escalation->status));

	if (EVENT_OBJECT_TRIGGER == event->object)
	{
		if (SUCCEED != check_escalation_trigger(escalation->triggerid, event->source, &skip, error))
			goto out;

		maintenance = (TRX_PROBLEM_SUPPRESSED_TRUE == event->suppressed ? HOST_MAINTENANCE_STATUS_ON :
				HOST_MAINTENANCE_STATUS_OFF);
	}
	else if (EVENT_SOURCE_INTERNAL == event->source)
	{
		if (EVENT_OBJECT_ITEM == event->object || EVENT_OBJECT_LLDRULE == event->object)
		{
			/* item disabled or deleted? */
			DCconfig_get_items_by_itemids(&item, &escalation->itemid, &errcode, 1);

			if (SUCCEED != errcode)
			{
				*error = trx_dsprintf(*error, "item id:" TRX_FS_UI64 " deleted.", escalation->itemid);
			}
			else if (ITEM_STATUS_DISABLED == item.status)
			{
				*error = trx_dsprintf(*error, "item \"%s\" disabled.", item.key_orig);
			}
			else if (HOST_STATUS_NOT_MONITORED == item.host.status)
			{
				*error = trx_dsprintf(*error, "host \"%s\" disabled.", item.host.host);
			}
			else
				maintenance = item.host.maintenance_status;

			DCconfig_clean_items(&item, &errcode, 1);

			if (NULL != *error)
				goto out;
		}
	}

	if (EVENT_SOURCE_TRIGGERS == action->eventsource &&
			ACTION_PAUSE_SUPPRESSED_TRUE == action->pause_suppressed &&
			HOST_MAINTENANCE_STATUS_ON == maintenance &&
			escalation->acknowledgeid == 0)
	{
		/* remove paused escalations that were created and recovered */
		/* during maintenance period                                 */
		if (0 == escalation->esc_step && 0 != escalation->r_eventid)
		{
			ret = TRX_ESCALATION_DELETE;
			goto out;
		}

		/* suppress paused escalations created before maintenance period */
		/* until maintenance ends or the escalations are recovered       */
		if (0 == escalation->r_eventid)
		{
			ret = TRX_ESCALATION_SUPPRESS;
			goto out;
		}
	}

	if (0 != skip)
	{
		/* one of trigger dependencies is in PROBLEM state, process escalation later */
		ret = TRX_ESCALATION_SKIP;
		goto out;
	}

	ret = TRX_ESCALATION_PROCESS;
out:

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s error:'%s'", __func__, check_escalation_result_string(ret),
			TRX_NULL2EMPTY_STR(*error));


	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: escalation_log_cancel_warning                                    *
 *                                                                            *
 * Purpose: write escalation cancellation warning message into log file       *
 *                                                                            *
 * Parameters: escalation - [IN] the escalation                               *
 *             error      - [IN] the error message                            *
 *                                                                            *
 ******************************************************************************/
static void	escalation_log_cancel_warning(const DB_ESCALATION *escalation, const char *error)
{
	if (0 != escalation->esc_step)
		treegix_log(LOG_LEVEL_WARNING, "escalation cancelled: %s", error);
}

/******************************************************************************
 *                                                                            *
 * Function: escalation_cancel                                                *
 *                                                                            *
 * Purpose: cancel escalation with the specified error message                *
 *                                                                            *
 * Parameters: escalation - [IN/OUT] the escalation to cancel                 *
 *             action     - [IN]     the action                               *
 *             event      - [IN]     the event                                *
 *             error      - [IN]     the error message                        *
 *                                                                            *
 ******************************************************************************/
static void	escalation_cancel(DB_ESCALATION *escalation, const DB_ACTION *action, const DB_EVENT *event,
		const char *error)
{
	TRX_USER_MSG	*user_msg = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() escalationid:" TRX_FS_UI64 " status:%s",
			__func__, escalation->escalationid, trx_escalation_status_string(escalation->status));

	if (0 != escalation->esc_step)
	{
		char	*message;

		message = trx_dsprintf(NULL, "NOTE: Escalation cancelled: %s\n%s", error, action->longdata);
		add_sentusers_msg(&user_msg, action->actionid, event, NULL, action->shortdata, message, NULL);
		flush_user_msg(&user_msg, escalation->esc_step, event, NULL, action->actionid, NULL);

		trx_free(message);
	}

	escalation_log_cancel_warning(escalation, error);
	escalation->status = ESCALATION_STATUS_COMPLETED;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: escalation_execute                                               *
 *                                                                            *
 * Purpose: execute next escalation step                                      *
 *                                                                            *
 * Parameters: escalation - [IN/OUT] the escalation to execute                *
 *             action     - [IN]     the action                               *
 *             event      - [IN]     the event                                *
 *                                                                            *
 ******************************************************************************/
static void	escalation_execute(DB_ESCALATION *escalation, const DB_ACTION *action, const DB_EVENT *event)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s() escalationid:" TRX_FS_UI64 " status:%s",
			__func__, escalation->escalationid, trx_escalation_status_string(escalation->status));

	escalation_execute_operations(escalation, event, action);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: escalation_recover                                               *
 *                                                                            *
 * Purpose: process escalation recovery                                       *
 *                                                                            *
 * Parameters: escalation - [IN/OUT] the escalation to recovery               *
 *             action     - [IN]     the action                               *
 *             event      - [IN]     the event                                *
 *             r_event    - [IN]     the recovery event                       *
 *                                                                            *
 ******************************************************************************/
static void	escalation_recover(DB_ESCALATION *escalation, const DB_ACTION *action, const DB_EVENT *event,
		const DB_EVENT *r_event)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s() escalationid:" TRX_FS_UI64 " status:%s",
			__func__, escalation->escalationid, trx_escalation_status_string(escalation->status));

	escalation_execute_recovery_operations(event, r_event, action);

	escalation->status = ESCALATION_STATUS_COMPLETED;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: escalation_acknowledge                                           *
 *                                                                            *
 * Purpose: process escalation acknowledge                                    *
 *                                                                            *
 * Parameters: escalation - [IN/OUT] the escalation to recovery               *
 *             action     - [IN]     the action                               *
 *             event      - [IN]     the event                                *
 *             r_event    - [IN]     the recovery event                       *
 *                                                                            *
 ******************************************************************************/
static void	escalation_acknowledge(DB_ESCALATION *escalation, const DB_ACTION *action, const DB_EVENT *event,
		const DB_EVENT *r_event)
{
	DB_ROW		row;
	DB_RESULT	result;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() escalationid:" TRX_FS_UI64 " acknowledgeid:" TRX_FS_UI64 " status:%s",
			__func__, escalation->escalationid, escalation->acknowledgeid,
			trx_escalation_status_string(escalation->status));

	result = DBselect(
			"select message,userid,clock,action,old_severity,new_severity from acknowledges"
			" where acknowledgeid=" TRX_FS_UI64,
			escalation->acknowledgeid);

	if (NULL != (row = DBfetch(result)))
	{
		DB_ACKNOWLEDGE	ack;

		ack.message = row[0];
		TRX_STR2UINT64(ack.userid, row[1]);
		ack.clock = atoi(row[2]);
		ack.acknowledgeid = escalation->acknowledgeid;
		ack.action = atoi(row[3]);
		ack.old_severity = atoi(row[4]);
		ack.new_severity = atoi(row[5]);

		escalation_execute_acknowledge_operations(event, r_event, action, &ack);
	}

	DBfree_result(result);

	escalation->status = ESCALATION_STATUS_COMPLETED;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

typedef struct
{
	trx_uint64_t		escalationid;

	int			nextcheck;
	int			esc_step;
	trx_escalation_status_t	status;

#define TRX_DIFF_ESCALATION_UNSET			__UINT64_C(0x0000)
#define TRX_DIFF_ESCALATION_UPDATE_NEXTCHECK		__UINT64_C(0x0001)
#define TRX_DIFF_ESCALATION_UPDATE_ESC_STEP		__UINT64_C(0x0002)
#define TRX_DIFF_ESCALATION_UPDATE_STATUS		__UINT64_C(0x0004)
#define TRX_DIFF_ESCALATION_UPDATE 								\
		(TRX_DIFF_ESCALATION_UPDATE_NEXTCHECK | TRX_DIFF_ESCALATION_UPDATE_ESC_STEP |	\
		TRX_DIFF_ESCALATION_UPDATE_STATUS)
	trx_uint64_t		flags;
}
trx_escalation_diff_t;

static trx_escalation_diff_t	*escalation_create_diff(const DB_ESCALATION *escalation)
{
	trx_escalation_diff_t	*diff;

	diff = (trx_escalation_diff_t *)trx_malloc(NULL, sizeof(trx_escalation_diff_t));
	diff->escalationid = escalation->escalationid;
	diff->nextcheck = escalation->nextcheck;
	diff->esc_step = escalation->esc_step;
	diff->status = escalation->status;
	diff->flags = TRX_DIFF_ESCALATION_UNSET;

	return diff;
}

static void	escalation_update_diff(const DB_ESCALATION *escalation, trx_escalation_diff_t *diff)
{
	if (escalation->nextcheck != diff->nextcheck)
	{
		diff->nextcheck = escalation->nextcheck;
		diff->flags |= TRX_DIFF_ESCALATION_UPDATE_NEXTCHECK;
	}

	if (escalation->esc_step != diff->esc_step)
	{
		diff->esc_step = escalation->esc_step;
		diff->flags |= TRX_DIFF_ESCALATION_UPDATE_ESC_STEP;
	}

	if (escalation->status != diff->status)
	{
		diff->status = escalation->status;
		diff->flags |= TRX_DIFF_ESCALATION_UPDATE_STATUS;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: add_ack_escalation_r_eventids                                    *
 *                                                                            *
 * Purpose: check if acknowledgement events of current escalation has related *
 *          recovery events and add those recovery event IDs to array of      *
 *          event IDs if this escalation                                      *
 *                                                                            *
 * Parameters: escalations - [IN] array of escalations to be processed        *
 *             eventids    - [OUT] array of events of current escalation      *
 *             event_pairs - [OUT] the array of event ID and recovery event   *
 *                                 pairs                                      *
 *                                                                            *
 * Comments: additionally acknowledgement event IDs are mapped with related   *
 *           recovery event IDs in get_db_eventid_r_eventid_pairs()           *
 *                                                                            *
 ******************************************************************************/
static void	add_ack_escalation_r_eventids(trx_vector_ptr_t *escalations, trx_vector_uint64_t *eventids,
		trx_vector_uint64_pair_t *event_pairs)
{
	int			i;
	trx_vector_uint64_t	ack_eventids, r_eventids;

	trx_vector_uint64_create(&ack_eventids);
	trx_vector_uint64_create(&r_eventids);

	for (i = 0; i < escalations->values_num; i++)
	{
		DB_ESCALATION	*escalation;

		escalation = (DB_ESCALATION *)escalations->values[i];

		if (0 != escalation->acknowledgeid)
			trx_vector_uint64_append(&ack_eventids, escalation->eventid);
	}

	if (0 < ack_eventids.values_num)
	{
		trx_db_get_eventid_r_eventid_pairs(&ack_eventids, event_pairs, &r_eventids);

		if (0 < r_eventids.values_num)
			trx_vector_uint64_append_array(eventids, r_eventids.values, r_eventids.values_num);
	}

	trx_vector_uint64_destroy(&ack_eventids);
	trx_vector_uint64_destroy(&r_eventids);
}

static int	process_db_escalations(int now, int *nextcheck, trx_vector_ptr_t *escalations,
		trx_vector_uint64_t *eventids, trx_vector_uint64_t *actionids)
{
	int				i, ret;
	trx_vector_uint64_t		escalationids;
	trx_vector_ptr_t		diffs, actions, events;
	trx_escalation_diff_t		*diff;
	trx_vector_uint64_pair_t	event_pairs;

	trx_vector_uint64_create(&escalationids);
	trx_vector_ptr_create(&diffs);
	trx_vector_ptr_create(&actions);
	trx_vector_ptr_create(&events);
	trx_vector_uint64_pair_create(&event_pairs);

	add_ack_escalation_r_eventids(escalations, eventids, &event_pairs);

	get_db_actions_info(actionids, &actions);
	trx_db_get_events_by_eventids(eventids, &events);

	for (i = 0; i < escalations->values_num; i++)
	{
		int		index;
		char		*error = NULL;
		DB_ACTION	*action;
		DB_EVENT	*event, *r_event;
		DB_ESCALATION	*escalation;

		escalation = (DB_ESCALATION *)escalations->values[i];

		if (FAIL == (index = trx_vector_ptr_bsearch(&actions, &escalation->actionid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			error = trx_dsprintf(error, "action id:" TRX_FS_UI64 " deleted", escalation->actionid);
			goto cancel_warning;
		}

		action = (DB_ACTION *)actions.values[index];

		if (ACTION_STATUS_ACTIVE != action->status)
		{
			error = trx_dsprintf(error, "action '%s' disabled.", action->name);
			goto cancel_warning;
		}

		if (FAIL == (index = trx_vector_ptr_bsearch(&events, &escalation->eventid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			error = trx_dsprintf(error, "event id:" TRX_FS_UI64 " deleted.", escalation->eventid);
			goto cancel_warning;
		}

		event = (DB_EVENT *)events.values[index];

		if ((EVENT_SOURCE_TRIGGERS == event->source || EVENT_SOURCE_INTERNAL == event->source) &&
				EVENT_OBJECT_TRIGGER == event->object && 0 == event->trigger.triggerid)
		{
			error = trx_dsprintf(error, "trigger id:" TRX_FS_UI64 " deleted.", event->objectid);
			goto cancel_warning;
		}

		if (0 != escalation->r_eventid)
		{
			if (FAIL == (index = trx_vector_ptr_bsearch(&events, &escalation->r_eventid,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				error = trx_dsprintf(error, "event id:" TRX_FS_UI64 " deleted.", escalation->r_eventid);
				goto cancel_warning;
			}

			r_event = (DB_EVENT *)events.values[index];

			if (EVENT_SOURCE_TRIGGERS == event->source && 0 == r_event->trigger.triggerid)
			{
				error = trx_dsprintf(error, "trigger id:" TRX_FS_UI64 " deleted.", r_event->objectid);
				goto cancel_warning;
			}
		}
		else
			r_event = NULL;

		/* Handle escalation taking into account status of items, triggers, hosts, */
		/* maintenance and trigger dependencies.                                   */
		switch (check_escalation(escalation, action, event, &error))
		{
			case TRX_ESCALATION_CANCEL:
				escalation_cancel(escalation, action, event, error);
				trx_free(error);
				TRX_FALLTHROUGH;
			case TRX_ESCALATION_DELETE:
				trx_vector_uint64_append(&escalationids, escalation->escalationid);
				TRX_FALLTHROUGH;
			case TRX_ESCALATION_SKIP:
				goto cancel_warning;	/* error is NULL on skip */
			case TRX_ESCALATION_SUPPRESS:
				diff = escalation_create_diff(escalation);
				escalation->nextcheck = now + SEC_PER_MIN;
				escalation_update_diff(escalation, diff);
				trx_vector_ptr_append(&diffs, diff);
				continue;
			case TRX_ESCALATION_PROCESS:
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
		}

		/* Execute operations and recovery operations, mark changes in 'diffs' for batch saving in DB below. */
		diff = escalation_create_diff(escalation);

		if (0 != escalation->acknowledgeid)
		{
			trx_uint64_t		r_eventid = 0;
			trx_uint64_pair_t	event_pair;

			r_event = NULL;
			event_pair.first = event->eventid;

			if (FAIL != (index = trx_vector_uint64_pair_bsearch(&event_pairs, event_pair,
					TRX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				r_eventid = event_pairs.values[index].second;

				if (FAIL != (index = trx_vector_ptr_bsearch(&events, &r_eventid,
						TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
				{
					r_event = (DB_EVENT *)events.values[index];
				}

			}

			escalation_acknowledge(escalation, action, event, r_event);
		}
		else if (NULL != r_event)
		{
			if (0 == escalation->esc_step)
				escalation_execute(escalation, action, event);

			escalation_recover(escalation, action, event, r_event);
		}
		else if (escalation->nextcheck <= now)
		{
			if (ESCALATION_STATUS_ACTIVE == escalation->status)
			{
				escalation_execute(escalation, action, event);
			}
			else if (ESCALATION_STATUS_SLEEP == escalation->status)
			{
				escalation->nextcheck = now + (0 == action->esc_period ? SEC_PER_HOUR :
						action->esc_period);
			}
			else
			{
				THIS_SHOULD_NEVER_HAPPEN;
			}
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
		}

		escalation_update_diff(escalation, diff);
		trx_vector_ptr_append(&diffs, diff);
cancel_warning:
		if (NULL != error)
		{
			escalation_log_cancel_warning(escalation, error);
			trx_vector_uint64_append(&escalationids, escalation->escalationid);
			trx_free(error);
		}
	}

	if (0 == diffs.values_num && 0 == escalationids.values_num)
		goto out;

	DBbegin();

	/* 2. Update escalations in the DB. */
	if (0 != diffs.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = TRX_KIBIBYTE, sql_offset = 0;

		sql = (char *)trx_malloc(sql, sql_alloc);

		trx_vector_ptr_sort(&diffs, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		for (i = 0; i < diffs.values_num; i++)
		{
			char	separator = ' ';

			diff = (trx_escalation_diff_t *)diffs.values[i];

			if (ESCALATION_STATUS_COMPLETED == diff->status)
			{
				trx_vector_uint64_append(&escalationids, diff->escalationid);
				continue;
			}

			if (0 == (diff->flags & TRX_DIFF_ESCALATION_UPDATE))
				continue;

			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update escalations set");

			if (0 != (diff->flags & TRX_DIFF_ESCALATION_UPDATE_NEXTCHECK))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cnextcheck="
						"case when r_eventid is null then %d else 0 end", separator,
						diff->nextcheck);
				separator = ',';

				if (diff->nextcheck < *nextcheck)
				{
					*nextcheck = diff->nextcheck;
				}
			}

			if (0 != (diff->flags & TRX_DIFF_ESCALATION_UPDATE_ESC_STEP))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cesc_step=%d", separator,
						diff->esc_step);
				separator = ',';
			}

			if (0 != (diff->flags & TRX_DIFF_ESCALATION_UPDATE_STATUS))
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cstatus=%d", separator,
						(int)diff->status);
			}

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where escalationid=" TRX_FS_UI64 ";\n",
					diff->escalationid);

			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)	/* in ORACLE always present begin..end; */
			DBexecute("%s", sql);

		trx_free(sql);
	}

	/* 3. Delete cancelled, completed escalations. */
	if (0 != escalationids.values_num)
		DBexecute_multiple_query("delete from escalations where", "escalationid", &escalationids);

	DBcommit();
out:
	trx_vector_ptr_clear_ext(&diffs, trx_ptr_free);
	trx_vector_ptr_destroy(&diffs);

	trx_vector_ptr_clear_ext(&actions, (trx_clean_func_t)free_db_action);
	trx_vector_ptr_destroy(&actions);

	trx_vector_ptr_clear_ext(&events, (trx_clean_func_t)trx_db_free_event);
	trx_vector_ptr_destroy(&events);

	trx_vector_uint64_pair_destroy(&event_pairs);

	ret = escalationids.values_num; /* performance metric */

	trx_vector_uint64_destroy(&escalationids);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_escalations                                              *
 *                                                                            *
 * Purpose: execute escalation steps and recovery operations;                 *
 *          postpone escalations during maintenance and due to trigger dep.;  *
 *          delete completed escalations from the database;                   *
 *          cancel escalations due to changed configuration, etc.             *
 *                                                                            *
 * Parameters: now               - [IN] the current time                      *
 *             nextcheck         - [IN/OUT] time of the next invocation       *
 *             escalation_source - [IN] type of escalations to be handled     *
 *                                                                            *
 * Return value: the count of deleted escalations                             *
 *                                                                            *
 * Comments: actions.c:process_actions() creates pseudo-escalations also for  *
 *           EVENT_SOURCE_DISCOVERY, EVENT_SOURCE_AUTO_REGISTRATION events,   *
 *           this function handles message and command operations for these   *
 *           events while host, group, template operations are handled        *
 *           in process_actions().                                            *
 *                                                                            *
 ******************************************************************************/
static int	process_escalations(int now, int *nextcheck, unsigned int escalation_source)
{
	int			ret = 0;
	DB_RESULT		result;
	DB_ROW			row;
	char			*filter = NULL;
	size_t			filter_alloc = 0, filter_offset = 0;

	trx_vector_ptr_t	escalations;
	trx_vector_uint64_t	actionids, eventids;

	DB_ESCALATION		*escalation;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&escalations);
	trx_vector_uint64_create(&actionids);
	trx_vector_uint64_create(&eventids);

	/* Selection of escalations to be processed:                                                          */
	/*                                                                                                    */
	/* e - row in escalations table, E - escalations table, S - ordered* set of escalations to be proc.   */
	/*                                                                                                    */
	/* TRX_ESCALATION_SOURCE_TRIGGER: S = {e in E | e.triggerid    mod process_num == 0}                  */
	/* TRX_ESCALATION_SOURCE_ITEM::   S = {e in E | e.itemid       mod process_num == 0}                  */
	/* TRX_ESCALATION_SOURCE_DEFAULT: S = {e in E | e.escalationid mod process_num == 0}                  */
	/*                                                                                                    */
	/* Note that each escalator always handles all escalations from the same triggers and items.          */
	/* The rest of the escalations (e.g. not trigger or item based) are spread evenly between escalators. */
	/*                                                                                                    */
	/* * by e.actionid, e.triggerid, e.itemid, e.escalationid                                             */
	switch (escalation_source)
	{
		case TRX_ESCALATION_SOURCE_TRIGGER:
			trx_strcpy_alloc(&filter, &filter_alloc, &filter_offset, "triggerid is not null");
			if (1 < CONFIG_ESCALATOR_FORKS)
			{
				trx_snprintf_alloc(&filter, &filter_alloc, &filter_offset,
						" and " TRX_SQL_MOD(triggerid, %d) "=%d",
						CONFIG_ESCALATOR_FORKS, process_num - 1);
			}
			break;
		case TRX_ESCALATION_SOURCE_ITEM:
			trx_strcpy_alloc(&filter, &filter_alloc, &filter_offset, "triggerid is null and"
					" itemid is not null");
			if (1 < CONFIG_ESCALATOR_FORKS)
			{
				trx_snprintf_alloc(&filter, &filter_alloc, &filter_offset,
						" and " TRX_SQL_MOD(itemid, %d) "=%d",
						CONFIG_ESCALATOR_FORKS, process_num - 1);
			}
			break;
		case TRX_ESCALATION_SOURCE_DEFAULT:
			trx_strcpy_alloc(&filter, &filter_alloc, &filter_offset,
					"triggerid is null and itemid is null");
			if (1 < CONFIG_ESCALATOR_FORKS)
			{
				trx_snprintf_alloc(&filter, &filter_alloc, &filter_offset,
						" and " TRX_SQL_MOD(escalationid, %d) "=%d",
						CONFIG_ESCALATOR_FORKS, process_num - 1);
			}
			break;
	}

	result = DBselect("select escalationid,actionid,triggerid,eventid,r_eventid,nextcheck,esc_step,status,itemid,"
					"acknowledgeid"
				" from escalations"
				" where %s and nextcheck<=%d"
				" order by actionid,triggerid,itemid,escalationid", filter,
				now + CONFIG_ESCALATOR_FREQUENCY);
	trx_free(filter);

	while (NULL != (row = DBfetch(result)) && TRX_IS_RUNNING())
	{
		int	esc_nextcheck;

		esc_nextcheck = atoi(row[5]);

		/* skip escalations that must be checked in next CONFIG_ESCALATOR_FREQUENCY period */
		if (esc_nextcheck > now)
		{
			if (esc_nextcheck < *nextcheck)
				*nextcheck = esc_nextcheck;

			continue;
		}

		escalation = (DB_ESCALATION *)trx_malloc(NULL, sizeof(DB_ESCALATION));
		escalation->nextcheck = esc_nextcheck;
		TRX_DBROW2UINT64(escalation->r_eventid, row[4]);
		TRX_STR2UINT64(escalation->escalationid, row[0]);
		TRX_STR2UINT64(escalation->actionid, row[1]);
		TRX_DBROW2UINT64(escalation->triggerid, row[2]);
		TRX_DBROW2UINT64(escalation->eventid, row[3]);
		escalation->esc_step = atoi(row[6]);
		escalation->status = atoi(row[7]);
		TRX_DBROW2UINT64(escalation->itemid, row[8]);
		TRX_DBROW2UINT64(escalation->acknowledgeid, row[9]);

		trx_vector_ptr_append(&escalations, escalation);
		trx_vector_uint64_append(&actionids, escalation->actionid);
		trx_vector_uint64_append(&eventids, escalation->eventid);

		if (0 < escalation->r_eventid)
			trx_vector_uint64_append(&eventids, escalation->r_eventid);

		if (escalations.values_num >= TRX_ESCALATIONS_PER_STEP)
		{
			ret += process_db_escalations(now, nextcheck, &escalations, &eventids, &actionids);
			trx_vector_ptr_clear_ext(&escalations, trx_ptr_free);
			trx_vector_uint64_clear(&actionids);
			trx_vector_uint64_clear(&eventids);
		}
	}
	DBfree_result(result);

	if (0 < escalations.values_num)
	{
		ret += process_db_escalations(now, nextcheck, &escalations, &eventids, &actionids);
		trx_vector_ptr_clear_ext(&escalations, trx_ptr_free);
	}

	trx_vector_ptr_destroy(&escalations);
	trx_vector_uint64_destroy(&actionids);
	trx_vector_uint64_destroy(&eventids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret; /* performance metric */
}

/******************************************************************************
 *                                                                            *
 * Function: main_escalator_loop                                              *
 *                                                                            *
 * Purpose: periodically check table escalations and generate alerts          *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: never returns                                                    *
 *                                                                            *
 ******************************************************************************/
TRX_THREAD_ENTRY(escalator_thread, args)
{
	int	now, nextcheck, sleeptime = -1, escalations_count = 0, old_escalations_count = 0;
	double	sec, total_sec = 0.0, old_total_sec = 0.0;
	time_t	last_stat_time;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

#define STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_tls_init_child();
#endif
	trx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);
	last_stat_time = time(NULL);

	DBconnect(TRX_DB_CONNECT_NORMAL);

	while (TRX_IS_RUNNING())
	{
		sec = trx_time();
		trx_update_env(sec);

		if (0 != sleeptime)
		{
			trx_setproctitle("%s #%d [processed %d escalations in " TRX_FS_DBL
					" sec, processing escalations]", get_process_type_string(process_type),
					process_num, old_escalations_count, old_total_sec);
		}

		nextcheck = time(NULL) + CONFIG_ESCALATOR_FREQUENCY;
		escalations_count += process_escalations(time(NULL), &nextcheck, TRX_ESCALATION_SOURCE_TRIGGER);
		escalations_count += process_escalations(time(NULL), &nextcheck, TRX_ESCALATION_SOURCE_ITEM);
		escalations_count += process_escalations(time(NULL), &nextcheck, TRX_ESCALATION_SOURCE_DEFAULT);

		total_sec += trx_time() - sec;

		sleeptime = calculate_sleeptime(nextcheck, CONFIG_ESCALATOR_FREQUENCY);

		now = time(NULL);

		if (0 != sleeptime || STAT_INTERVAL <= now - last_stat_time)
		{
			if (0 == sleeptime)
			{
				trx_setproctitle("%s #%d [processed %d escalations in " TRX_FS_DBL
						" sec, processing escalations]", get_process_type_string(process_type),
						process_num, escalations_count, total_sec);
			}
			else
			{
				trx_setproctitle("%s #%d [processed %d escalations in " TRX_FS_DBL " sec, idle %d sec]",
						get_process_type_string(process_type), process_num, escalations_count,
						total_sec, sleeptime);

				old_escalations_count = escalations_count;
				old_total_sec = total_sec;
			}
			escalations_count = 0;
			total_sec = 0.0;
			last_stat_time = now;
		}

		trx_sleep_loop(sleeptime);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
}
