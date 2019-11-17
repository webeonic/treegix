<?php


$table = (new CTableInfo())->setNoDataMessage(_('No graphs added.'));

foreach ($data['graphs'] as $graph) {
	$url = $graph['simple']
		? (new CUrl('history.php'))
			->setArgument('action', HISTORY_GRAPH)
			->setArgument('itemids', [$graph['itemid']])
		: (new CUrl('charts.php'))->setArgument('graphid', $graph['graphid']);
	$on_click = $graph['simple']
		? "rm4favorites('itemid','".$graph['itemid']."')"
		: "rm4favorites('graphid','".$graph['graphid']."')";

	$table->addRow([
		new CLink($graph['label'], $url),
		(new CButton())
			->onClick($on_click)
			->addClass(TRX_STYLE_REMOVE_BTN)
			->setAttribute('aria-label', _xs('Remove, %1$s', 'screen reader', $graph['label']))
	]);
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
