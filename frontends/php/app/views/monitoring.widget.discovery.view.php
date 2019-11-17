<?php



if ($data['error'] !== null) {
	$table = (new CTableInfo())->setNoDataMessage($data['error']);
}
else {
	$table = (new CTableInfo())
		->setHeader([
			_('Discovery rule'),
			_x('Up', 'discovery results in dashboard'),
			_x('Down', 'discovery results in dashboard')
		])
		->setHeadingColumn(0);

	foreach ($data['drules'] as $drule) {
		$table->addRow([
			new CLink($drule['name'], (new CUrl('treegix.php'))
				->setArgument('action', 'discovery.view')
				->setArgument('filter_set', 1)
				->setArgument('filter_druleids', [$drule['druleid']])
			),
			($drule['up'] != 0) ? (new CSpan($drule['up']))->addClass(TRX_STYLE_GREEN) : '',
			($drule['down'] != 0) ? (new CSpan($drule['down']))->addClass(TRX_STYLE_RED) : ''
		]);
	}
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
