<?php



$data['backurl'] = (new CUrl('treegix.php'))
	->setArgument('action', 'dashboard.view')
	->getUrl();

if ($data['filter']['show_type'] == WIDGET_PROBLEMS_BY_SV_SHOW_TOTALS) {
	$table = makeSeverityTotals($data)
		->addClass(ZBX_STYLE_BY_SEVERITY_WIDGET)
		->addClass(ZBX_STYLE_TOTALS_LIST)
		->addClass(($data['filter']['layout'] == STYLE_HORIZONTAL)
			? ZBX_STYLE_TOTALS_LIST_HORIZONTAL
			: ZBX_STYLE_TOTALS_LIST_VERTICAL
		);
}
else {
	$filter_severities = (array_key_exists('severities', $data['filter']) && $data['filter']['severities'])
		? $data['filter']['severities']
		: range(TRIGGER_SEVERITY_NOT_CLASSIFIED, TRIGGER_SEVERITY_COUNT - 1);

	$header = [[_('Host group'), (new CSpan())->addClass(ZBX_STYLE_ARROW_UP)]];

	for ($severity = TRIGGER_SEVERITY_COUNT - 1; $severity >= TRIGGER_SEVERITY_NOT_CLASSIFIED; $severity--) {
		if (in_array($severity, $filter_severities)) {
			$header[] = getSeverityName($severity, $data['severity_names']);
		}
	}

	$hide_empty_groups = array_key_exists('hide_empty_groups', $data['filter'])
		? $data['filter']['hide_empty_groups']
		: 0;

	$groupurl = (new CUrl('treegix.php'))
		->setArgument('action', 'problem.view')
		->setArgument('filter_set', 1)
		->setArgument('filter_show', TRIGGERS_OPTION_RECENT_PROBLEM)
		->setArgument('filter_groupids', null)
		->setArgument('filter_hostids',
			array_key_exists('hostids', $data['filter']) ? $data['filter']['hostids'] : null
		)
		->setArgument('filter_name', array_key_exists('problem', $data['filter']) ? $data['filter']['problem'] : null)
		->setArgument('filter_show_suppressed',
			(array_key_exists('show_suppressed', $data['filter']) && $data['filter']['show_suppressed'] == 1) ? 1 : null
		);

	$table = makeSeverityTable($data, $hide_empty_groups, $groupurl)
		->addClass(ZBX_STYLE_BY_SEVERITY_WIDGET)
		->setHeader($header)
		->setHeadingColumn(0);
}

$output = [
	'header' => $data['name'],
	'body' => $table->toString()
];

if (($messages = getMessages()) !== null) {
	$output['messages'] = $messages->toString();
}

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
