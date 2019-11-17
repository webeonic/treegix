<?php


if ($data['clock']['critical_error'] !== null) {
	$item = (new CTableInfo())->setNoDataMessage($data['clock']['critical_error']);

	$output = [
		'header' => $data['name'],
		'body' => $item->toString()
	];
}
else {
	$item = (new CClock());

	if ($data['clock']['error'] !== null) {
		$item->setError($data['clock']['error']);
	}

	if ($data['clock']['time'] !== null) {
		$item->setTime($data['clock']['time']);
	}

	if ($data['clock']['time_zone_offset'] !== null) {
		$item->setTimeZoneOffset($data['clock']['time_zone_offset']);
	}

	if ($data['clock']['time_zone_string'] !== null) {
		$item->setTimeZoneString($data['clock']['time_zone_string']);
	}

	$output = [
		'header' => $data['name'],
		'body' => $item->toString(),
		'script_inline' => $item->getScriptRun()
	];
}

if (($messages = getMessages()) !== null) {
	$output['messages'] = $messages->toString();
}

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
