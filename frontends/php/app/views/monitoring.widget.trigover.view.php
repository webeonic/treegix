<?php


$back_url = (new CUrl('treegix.php'))->setArgument('action', 'dashboard.view');

$table = getTriggersOverview($data['hosts'], $data['triggers'], $back_url->getUrl(), $data['style']);

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
