<?php



$table = (new CTableInfo())->setNoDataMessage(_('No screens added.'));

foreach ($data['screens'] as $screen) {
	$url = $screen['slideshow']
		? (new CUrl('slides.php'))->setArgument('elementid', $screen['slideshowid'])
		: (new CUrl('screens.php'))->setArgument('elementid', $screen['screenid']);
	$on_click = $screen['slideshow']
		? "rm4favorites('slideshowid','".$screen['slideshowid']."')"
		: "rm4favorites('screenid','".$screen['screenid']."')";

	$table->addRow([
		new CLink($screen['label'], $url),
		(new CButton())
			->onClick($on_click)
			->addClass(ZBX_STYLE_REMOVE_BTN)
			->setAttribute('aria-label', _xs('Remove, %1$s', 'screen reader', $screen['label']))
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
