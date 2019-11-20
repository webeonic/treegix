<?php



require_once dirname(__FILE__).'/include/config.inc.php';
require_once dirname(__FILE__).'/include/graphs.inc.php';
require_once dirname(__FILE__).'/include/screens.inc.php';
require_once dirname(__FILE__).'/include/blocks.inc.php';

$page['title'] = _('Host screens');
$page['file'] = 'screens.php';
$page['scripts'] = ['effects.js', 'dragdrop.js', 'class.calendar.js', 'gtlc.js', 'flickerfreescreen.js',
	'layout.mode.js'];
$page['type'] = detect_page_type(PAGE_TYPE_HTML);

CView::$has_web_layout_mode = true;
$page['web_layout_mode'] = CView::getLayoutMode();

define('TRX_PAGE_DO_JS_REFRESH', 1);

require_once dirname(__FILE__).'/include/page_header.php';

// VAR	TYPE	OPTIONAL	FLAGS	VALIDATION	EXCEPTION
$fields = [
	'hostid' =>		[T_TRX_INT,			O_OPT, P_SYS, DB_ID,		null],
	'tr_groupid' =>	[T_TRX_INT,			O_OPT, P_SYS, DB_ID,		null],
	'tr_hostid' =>	[T_TRX_INT,			O_OPT, P_SYS, DB_ID,		null],
	'screenid' =>	[T_TRX_INT,			O_OPT, P_SYS|P_NZERO, DB_ID, null],
	'step' =>		[T_TRX_INT,			O_OPT, P_SYS, BETWEEN(0, 65535), null],
	'from' =>		[T_TRX_RANGE_TIME,	O_OPT, P_SYS, null,		null],
	'to' =>			[T_TRX_RANGE_TIME,	O_OPT, P_SYS, null,		null],
	'reset' =>		[T_TRX_STR,			O_OPT, P_SYS, IN('"reset"'), null]
];
check_fields($fields);
validateTimeSelectorPeriod(getRequest('from'), getRequest('to'));

if ($page['type'] == PAGE_TYPE_JS || $page['type'] == PAGE_TYPE_HTML_BLOCK) {
	require_once dirname(__FILE__).'/include/page_footer.php';
	exit;
}

/*
 * Display
 */
$data = [
	'hostid' => getRequest('hostid', 0),
	'screenid' => getRequest('screenid', CProfile::get('web.hostscreen.screenid', null)),
	'active_tab' => CProfile::get('web.screens.filter.active', 1)
];
CProfile::update('web.hostscreen.screenid', $data['screenid'], PROFILE_TYPE_ID);

$host = API::Host()->get([
	'output' => [],
	'hostids' => $data['hostid']
]);

if (!$host) {
	access_deny();
}

// get screen list
$data['screens'] = API::TemplateScreen()->get([
	'hostids' => $data['hostid'],
	'output' => API_OUTPUT_EXTEND
]);
$data['screens'] = trx_toHash($data['screens'], 'screenid');
order_result($data['screens'], 'name');

// get screen
$screenid = null;
if (!empty($data['screens'])) {
	$screen = !isset($data['screens'][$data['screenid']]) ? reset($data['screens']) : $data['screens'][$data['screenid']];
	if (!empty($screen['screenid'])) {
		$screenid = $screen['screenid'];
	}
}

$data['screen'] = API::TemplateScreen()->get([
	'screenids' => $screenid,
	'hostids' => $data['hostid'],
	'output' => API_OUTPUT_EXTEND,
	'selectScreenItems' => API_OUTPUT_EXTEND
]);
$data['screen'] = reset($data['screen']);

// get host
if (!empty($data['screen']['hostid'])) {
	$data['host'] = get_host_by_hostid($data['screen']['hostid']);
}

if ($data['screen']) {
	$timeselector_options = [
		'profileIdx' => 'web.screens.filter',
		'profileIdx2' => $data['screen']['screenid'],
		'from' => getRequest('from'),
		'to' => getRequest('to')
	];
	updateTimeSelectorPeriod($timeselector_options);

	$data += $timeselector_options;
}

// render view
$screenView = new CView('monitoring.hostscreen', $data);
$screenView->render();
$screenView->show();

require_once dirname(__FILE__).'/include/page_footer.php';
