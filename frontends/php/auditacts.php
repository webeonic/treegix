<?php
 


require_once dirname(__FILE__).'/include/config.inc.php';
require_once dirname(__FILE__).'/include/audit.inc.php';
require_once dirname(__FILE__).'/include/actions.inc.php';
require_once dirname(__FILE__).'/include/users.inc.php';

$page['title'] = _('Action log');
$page['file'] = 'auditacts.php';
$page['scripts'] = ['class.calendar.js', 'gtlc.js', 'flickerfreescreen.js'];
$page['type'] = detect_page_type(PAGE_TYPE_HTML);

require_once dirname(__FILE__).'/include/page_header.php';

// VAR	TYPE	OPTIONAL	FLAGS	VALIDATION	EXCEPTION
$fields = [
	// filter
	'filter_rst' =>	[T_TRX_STR,			O_OPT, P_SYS,	null,	null],
	'filter_set' =>	[T_TRX_STR,			O_OPT, P_SYS,	null,	null],
	'alias' =>		[T_TRX_STR,			O_OPT, P_SYS,	null,	null],
	'from' =>		[T_TRX_RANGE_TIME,	O_OPT, P_SYS,	null,	null],
	'to' =>			[T_TRX_RANGE_TIME,	O_OPT, P_SYS,	null,	null]
];
check_fields($fields);
validateTimeSelectorPeriod(getRequest('from'), getRequest('to'));

if ($page['type'] == PAGE_TYPE_JS || $page['type'] == PAGE_TYPE_HTML_BLOCK) {
	require_once dirname(__FILE__).'/include/page_footer.php';
	exit;
}

/*
 * Filter
 */
if (hasRequest('filter_set')) {
	CProfile::update('web.auditacts.filter.alias', getRequest('alias', ''), PROFILE_TYPE_STR);
}
elseif (hasRequest('filter_rst')) {
	DBStart();
	CProfile::delete('web.auditacts.filter.alias');
	DBend();
}

/*
 * Display
 */
$timeselector_options = [
	'profileIdx' => 'web.auditacts.filter',
	'profileIdx2' => 0,
	'from' => getRequest('from'),
	'to' => getRequest('to')
];
updateTimeSelectorPeriod($timeselector_options);

$data = [
	'alias' => CProfile::get('web.auditacts.filter.alias', ''),
	'users' => [],
	'alerts' => [],
	'paging' => null,
	'timeline' => getTimeSelectorPeriod($timeselector_options),
	'active_tab' => CProfile::get('web.auditacts.filter.active', 1)
];

$userid = null;

if ($data['alias']) {
	$data['users'] = API::User()->get([
		'output' => ['userid', 'alias', 'name', 'surname'],
		'filter' => ['alias' => $data['alias']],
		'preservekeys' => true
	]);

	if ($data['users']) {
		$user = reset($data['users']);

		$userid = $user['userid'];
	}
}

if (!$data['alias'] || $data['users']) {
	$config = select_config();

	// fetch alerts for different objects and sources and combine them in a single stream
	foreach (eventSourceObjects() as $eventSource) {
		$data['alerts'] = array_merge($data['alerts'], API::Alert()->get([
			'output' => ['alertid', 'actionid', 'userid', 'clock', 'sendto', 'subject', 'message', 'status',
				'retries', 'error', 'alerttype'
			],
			'selectMediatypes' => ['mediatypeid', 'name', 'maxattempts'],
			'userids' => $userid,
			// API::Alert operates with 'open' time interval therefore before call have to alter 'from' and 'to' values.
			'time_from' => $data['timeline']['from_ts'] - 1,
			'time_till' => $data['timeline']['to_ts'] + 1,
			'eventsource' => $eventSource['source'],
			'eventobject' => $eventSource['object'],
			'sortfield' => 'alertid',
			'sortorder' => TRX_SORT_DOWN,
			'limit' => $config['search_limit'] + 1
		]));
	}

	CArrayHelper::sort($data['alerts'], [
		['field' => 'alertid', 'order' => TRX_SORT_DOWN]
	]);

	$data['alerts'] = array_slice($data['alerts'], 0, $config['search_limit'] + 1);

	// paging
	$data['paging'] = getPagingLine($data['alerts'], TRX_SORT_DOWN, new CUrl('auditacts.php'));

	// get users
	if (!$data['alias']) {
		$data['users'] = API::User()->get([
			'output' => ['userid', 'alias', 'name', 'surname'],
			'userids' => zbx_objectValues($data['alerts'], 'userid'),
			'preservekeys' => true
		]);
	}
}

// get actions names
if ($data['alerts']) {
	$data['actions'] = API::Action()->get([
		'output' => ['actionid', 'name'],
		'actionids' => array_unique(zbx_objectValues($data['alerts'], 'actionid')),
		'preservekeys' => true
	]);
}

// render view
$auditView = new CView('administration.auditacts.list', $data);
$auditView->render();
$auditView->show();

require_once dirname(__FILE__).'/include/page_footer.php';
