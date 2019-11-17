<?php



$table = (new CTableInfo())->setNoDataMessage(_('No maps added.'));

foreach ($data['maps'] as $map) {
	$table->addRow([
		new CLink($map['label'], (new CUrl('treegix.php'))
			->setArgument('action', 'map.view')
			->setArgument('sysmapid', $map['sysmapid'])
		),
		(new CButton())
			->onClick("rm4favorites('sysmapid','".$map['sysmapid']."')")
			->addClass(TRX_STYLE_REMOVE_BTN)
			->setAttribute('aria-label', _xs('Remove, %1$s', 'screen reader', $map['label']))
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
