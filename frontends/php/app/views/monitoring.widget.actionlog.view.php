<?php



// indicator of sort field
$sort_div = (new CSpan())
	->addClass(($data['sortorder'] === TRX_SORT_DOWN) ? TRX_STYLE_ARROW_DOWN : TRX_STYLE_ARROW_UP);

// create alert table
$table = (new CTableInfo())
	->setHeader([
		($data['sortfield'] === 'clock') ? [_('Time'), $sort_div] : _('Time'),
		_('Action'),
		($data['sortfield'] === 'mediatypeid') ? [_('Type'), $sort_div] : _('Type'),
		($data['sortfield'] === 'sendto') ? [_('Recipient'), $sort_div] : _('Recipient'),
		_('Message'),
		($data['sortfield'] === 'status') ? [_('Status'), $sort_div] : _('Status'),
		_('Info')
	]);

foreach ($data['alerts'] as $alert) {
	if ($alert['alerttype'] == ALERT_TYPE_MESSAGE && array_key_exists('maxattempts', $alert)
			&& ($alert['status'] == ALERT_STATUS_NOT_SENT || $alert['status'] == ALERT_STATUS_NEW)) {
		$info_icons = makeWarningIcon(_n('%1$s retry left', '%1$s retries left',
			$alert['maxattempts'] - $alert['retries'])
		);
	}
	elseif ($alert['error'] !== '') {
		$info_icons = makeErrorIcon($alert['error']);
	}
	else {
		$info_icons = null;
	}

	$table->addRow([
		trx_date2str(DATE_TIME_FORMAT_SECONDS, $alert['clock']),
		array_key_exists($alert['actionid'], $data['actions']) ? $data['actions'][$alert['actionid']]['name'] : '',
		$alert['description'],
		makeEventDetailsTableUser($alert, $data['db_users']),
		[bold($alert['subject']), BR(), BR(), trx_nl2br($alert['message'])],
		makeActionTableStatus($alert),
		makeInformationList($info_icons)
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
