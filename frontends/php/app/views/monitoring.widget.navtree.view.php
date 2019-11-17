<?php

$item = new CNavigationTree([
	'problems' => $data['problems'],
	'severity_config' => $data['severity_config'],
	'initial_load' => $data['initial_load'],
	'uniqueid' => $data['uniqueid'],
	'maps_accessible' => $data['maps_accessible'],
	'navtree_item_selected' => $data['navtree_item_selected'],
	'navtree_items_opened' => $data['navtree_items_opened'],
	'show_unavailable' => $data['show_unavailable']
]);

if ($data['error'] !== null) {
	$item->setError($data['error']);
}

$output = [
	'header' => $data['name'],
	'body' => $item->toString(),
	'script_inline' => $item->getScriptRun()
];

if (($messages = getMessages()) !== null) {
	$output['messages'] = $messages->toString();
}

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
