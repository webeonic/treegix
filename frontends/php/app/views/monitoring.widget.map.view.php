<?php



$item = new CDashboardWidgetMap($data['sysmap_data'], $data['widget_settings']);

$output = [
	'header' => $data['name'],
	'body' => $item->toString(),
	'script_inline' => $item->getScriptRun()
];

if (($messages = getMessages()) !== null) {
	$output['messages'] = $messages->toString();
}

if ($this->data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
