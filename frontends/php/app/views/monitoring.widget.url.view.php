<?php



$item = ($data['url']['error'] !== null)
	? (new CTableInfo())->setNoDataMessage($data['url']['error'])
	: (new CIFrame($data['url']['url'], '100%', '98%', 'auto')); // height is 98% to remove vertical scroll on widget

$output = [
	'header' => $data['name'],
	'body' => $item->toString()
];

if (($messages = getMessages()) !== null) {
	$output['messages'] = $messages->toString();
}

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
