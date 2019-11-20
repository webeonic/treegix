<?php


require_once dirname(__FILE__).'/include/config.inc.php';
require_once dirname(__FILE__).'/include/graphs.inc.php';

$page['file'] = 'chart3.php';
$page['type'] = PAGE_TYPE_IMAGE;

require_once dirname(__FILE__).'/include/page_header.php';

// VAR	TYPE	OPTIONAL	FLAGS	VALIDATION	EXCEPTION
$fields = [
	'from' =>			[T_TRX_RANGE_TIME,	O_OPT, P_SYS,		null,				null],
	'to' =>				[T_TRX_RANGE_TIME,	O_OPT, P_SYS,		null,				null],
	'profileIdx' =>		[T_TRX_STR,			O_OPT, null,		null,				null],
	'profileIdx2' =>	[T_TRX_STR,			O_OPT, null,		null,				null],
	'httptestid' =>		[T_TRX_INT,			O_OPT, P_NZERO,	null,				null],
	'http_item_type' =>	[T_TRX_INT,			O_OPT, null,		null,				null],
	'name' =>			[T_TRX_STR,			O_OPT, null,		null,				null],
	'width' =>			[T_TRX_INT,			O_OPT, null,	BETWEEN(CLineGraphDraw::GRAPH_WIDTH_MIN, 65535),	null],
	'height' =>			[T_TRX_INT,			O_OPT, null,	BETWEEN(CLineGraphDraw::GRAPH_HEIGHT_MIN, 65535),	null],
	'ymin_type' =>		[T_TRX_INT,			O_OPT, null,		IN('0,1,2'),		null],
	'ymax_type' =>		[T_TRX_INT,			O_OPT, null,		IN('0,1,2'),		null],
	'ymin_itemid' =>	[T_TRX_INT,			O_OPT, null,		DB_ID,				null],
	'ymax_itemid' =>	[T_TRX_INT,			O_OPT, null,		DB_ID,				null],
	'legend' =>			[T_TRX_INT,			O_OPT, null,		IN('0,1'),			null],
	'showworkperiod' =>	[T_TRX_INT,			O_OPT, null,		IN('0,1'),			null],
	'showtriggers' =>	[T_TRX_INT,			O_OPT, null,		IN('0,1'),			null],
	'graphtype' =>		[T_TRX_INT,			O_OPT, null,		IN('0,1'),			null],
	'yaxismin' =>		[T_TRX_DBL,			O_OPT, null,		null,				null],
	'yaxismax' =>		[T_TRX_DBL,			O_OPT, null,		null,				null],
	'percent_left' =>	[T_TRX_DBL,			O_OPT, null,		BETWEEN(0, 100),	null],
	'percent_right' =>	[T_TRX_DBL,			O_OPT, null,		BETWEEN(0, 100),	null],
	'outer' =>			[T_TRX_INT,			O_OPT, null,		IN('0,1'),			null],
	'items' =>			[T_TRX_STR,			O_OPT, null,		null,				null],
	'onlyHeight' =>		[T_TRX_INT,			O_OPT, null,		IN('0,1'),			null],
	'widget_view' =>	[T_TRX_INT,			O_OPT, null,		IN('0,1'),			null]
];
if (!check_fields($fields)) {
	exit();
}
validateTimeSelectorPeriod(getRequest('from'), getRequest('to'));

$graph_items = [];

if ($httptestid = getRequest('httptestid', false)) {
	$httptests = API::HttpTest()->get([
		'output' => [],
		'httptestids' => $httptestid,
		'selectHosts' => ['hostid', 'name', 'host']
	]);

	if (!$httptests) {
		access_deny();
	}

	$colors = ['Red', 'Dark Green', 'Blue', 'Dark Yellow', 'Cyan', 'Gray', 'Dark Red', 'Green', 'Dark Blue', 'Yellow',
		'Black'
	];
	$color = false;
	$items = [];
	$hosts = trx_toHash($httptests[0]['hosts'], 'hostid');

	$dbItems = DBselect(
		'SELECT i.itemid,i.type,i.name,i.delay,i.units,i.hostid,i.history,i.trends,i.value_type,i.key_'.
		' FROM httpstepitem hi,items i,httpstep hs'.
		' WHERE i.itemid=hi.itemid'.
			' AND hs.httptestid='.trx_dbstr($httptestid).
			' AND hs.httpstepid=hi.httpstepid'.
			' AND hi.type='.trx_dbstr(getRequest('http_item_type', HTTPSTEP_ITEM_TYPE_TIME)).
		' ORDER BY hs.no DESC'
	);
	while ($item = DBfetch($dbItems)) {
		$graph_items[] = $item + [
			'color' => ($color === false) ? reset($colors) : $color,
			'host' => $hosts[$item['hostid']]['host'],
			'hostname' => $hosts[$item['hostid']]['name']
		];
		$color = next($colors);
	}

	$name = getRequest('name', '');
}
elseif ($items = getRequest('items', [])) {
	CArrayHelper::sort($items, ['sortorder']);

	$dbItems = API::Item()->get([
		'itemids' => trx_objectValues($items, 'itemid'),
		'output' => ['itemid', 'type', 'master_itemid', 'name', 'delay', 'units', 'hostid', 'history', 'trends',
			'value_type', 'key_'
		],
		'selectHosts' => ['hostid', 'name', 'host'],
		'filter' => [
			'flags' => [TRX_FLAG_DISCOVERY_NORMAL, TRX_FLAG_DISCOVERY_PROTOTYPE, TRX_FLAG_DISCOVERY_CREATED]
		],
		'webitems' => true,
		'preservekeys' => true
	]);

	foreach ($items as $item) {
		if (!array_key_exists($item['itemid'], $dbItems)) {
			access_deny();
		}
		$host = reset($dbItems[$item['itemid']]['hosts']);
		$graph_items[] = $dbItems[$item['itemid']] + $item + [
			'host' => $host['host'],
			'hostname' => $host['name']
		];
	}

	foreach ($graph_items as &$graph_item) {
		unset($graph_item['hosts']);
	}
	unset($graph_item);

	$name = getRequest('name', '');
}
else {
	show_error_message(_('No items defined.'));
	exit;
}

/*
 * Display
 */
$timeline = getTimeSelectorPeriod([
	'profileIdx' => getRequest('profileIdx', 'web.httpdetails.filter'),
	'profileIdx2' => getRequest('httptestid', getRequest('profileIdx2')),
	'from' => getRequest('from'),
	'to' => getRequest('to')
]);

CProfile::update($timeline['profileIdx'].'.httptestid', $timeline['profileIdx2'], PROFILE_TYPE_ID);

$graph = new CLineGraphDraw(getRequest('graphtype', GRAPH_TYPE_NORMAL));
$graph->setHeader($name);
$graph->setPeriod($timeline['to_ts'] - $timeline['from_ts']);
$graph->setSTime($timeline['from_ts']);
$graph->setWidth(getRequest('width', 900));
$graph->setHeight(getRequest('height', 200));
$graph->showLegend(getRequest('legend', 1));
$graph->showWorkPeriod(getRequest('showworkperiod', 1));
$graph->showTriggers(getRequest('showtriggers', 1));
$graph->setYMinAxisType(getRequest('ymin_type', GRAPH_YAXIS_TYPE_CALCULATED));
$graph->setYMaxAxisType(getRequest('ymax_type', GRAPH_YAXIS_TYPE_CALCULATED));
$graph->setYAxisMin(getRequest('yaxismin', 0.00));
$graph->setYAxisMax(getRequest('yaxismax', 100.00));
$graph->setYMinItemId(getRequest('ymin_itemid', 0));
$graph->setYMaxItemId(getRequest('ymax_itemid', 0));
$graph->setLeftPercentage(getRequest('percent_left', 0));
$graph->setRightPercentage(getRequest('percent_right', 0));
$graph->setOuter(getRequest('outer', 0));

if (getRequest('widget_view') === '1') {
	$graph->draw_header = false;
	$graph->with_vertical_padding = false;
}

foreach ($graph_items as $graph_item) {
	$graph->addItem($graph_item);
}

if (getRequest('onlyHeight', '0') === '1') {
	$graph->drawDimensions();
	header('X-TRX-SBOX-HEIGHT: '.($graph->getHeight() + 1));
}
else {
	$graph->draw();
}

require_once dirname(__FILE__).'/include/page_footer.php';
