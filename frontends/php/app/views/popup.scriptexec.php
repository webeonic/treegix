<?php



$output = [
	'header' => $data['title'],
	'body' => (new CForm())
		->addItem([
			$data['errors'],
			(new CTabView())->addTab('scriptTab', null,
				(new CPre(
					(new CList([bold($data['command']), SPACE, $data['message']]))
				))
			)
		])
		->toString(),
	'buttons' => null
];

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
