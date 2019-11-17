<?php



$output = [
	'body' => $data['svg']
];

if (!$data['preview']) {
	$output += [
		'header' => $data['name'],
		'script_inline' => $data['script_inline']
	];

	if ($data['info']) {
		$output += [
			'info' => $data['info']
		];
	}
}

if (($messages = getMessages()) !== null) {
	$output['messages'] = $messages->toString();
}

if (!$data['preview'] && $data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
