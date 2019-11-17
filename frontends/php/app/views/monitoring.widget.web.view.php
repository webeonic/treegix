<?php



// indicator of sort field
$sort_div = (new CSpan())->addClass(ZBX_STYLE_ARROW_UP);

$table = (new CTableInfo())
	->setHeader([[_('Host group'), $sort_div], _('Ok'), _('Failed'), _('Unknown')])
	->setHeadingColumn(0);

$url = (new CUrl('treegix.php'))
	->setArgument('action', 'web.view')
	->setArgument('groupid', '')
	->setArgument('hostid', '0');

foreach ($data['groups'] as $group) {
	$url->setArgument('groupid', $group['groupid']);

	$table->addRow([
		new CLink($group['name'], $url->getUrl()),
		($group['ok'] != 0) ? (new CSpan($group['ok']))->addClass(ZBX_STYLE_GREEN) : '',
		($group['failed'] != 0) ? (new CSpan($group['failed']))->addClass(ZBX_STYLE_RED) : '',
		($group['unknown'] != 0) ? (new CSpan($group['unknown']))->addClass(ZBX_STYLE_GREY) : ''
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
