<?php


$auditWidget = (new CWidget())->setTitle(_('Action log'));

// create filter
$filterColumn = new CFormList();
$filterColumn->addRow(_('Recipient'), [
	(new CTextBox('alias', $this->data['alias']))
		->setWidth(TRX_TEXTAREA_FILTER_STANDARD_WIDTH)
		->setAttribute('autofocus', 'autofocus'),
	(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
	(new CButton('btn1', _('Select')))
		->addClass(TRX_STYLE_BTN_GREY)
		->onClick('return PopUp("popup.generic",'.
			CJs::encodeJson([
				'srctbl' => 'users',
				'srcfld1' => 'alias',
				'dstfrm' => 'zbx_filter',
				'dstfld1' => 'alias'
			]).', null, this);'
		)
]);

$auditWidget->addItem(
	(new CFilter(new CUrl('auditacts.php')))
		->setProfile($data['timeline']['profileIdx'])
		->setActiveTab($data['active_tab'])
		->addTimeSelector($data['timeline']['from'], $data['timeline']['to'])
		->addFilterTab(_('Filter'), [$filterColumn])
);

// create form
$auditForm = (new CForm('get'))->setName('auditForm');

// create table
$auditTable = (new CTableInfo())
	->setHeader([
		_('Time'),
		_('Action'),
		_('Type'),
		_('Recipient'),
		_('Message'),
		_('Status'),
		_('Info')
	]);

foreach ($this->data['alerts'] as $alert) {
	$mediatype = array_pop($alert['mediatypes']);

	if ($alert['status'] == ALERT_STATUS_SENT) {
		$status = ($alert['alerttype'] == ALERT_TYPE_MESSAGE)
			? (new CSpan(_('Sent')))->addClass(TRX_STYLE_GREEN)
			: (new CSpan(_('Executed')))->addClass(TRX_STYLE_GREEN);
	}
	elseif ($alert['status'] == ALERT_STATUS_NOT_SENT || $alert['status'] == ALERT_STATUS_NEW) {
		$status = (new CSpan([
			_('In progress').':',
			BR(),
			_n('%1$s retry left', '%1$s retries left', $mediatype['maxattempts'] - $alert['retries']),
		]))->addClass(TRX_STYLE_YELLOW);
	}
	else {
		$status = (new CSpan(_('Failed')))->addClass(TRX_STYLE_RED);
	}

	$message = ($alert['alerttype'] == ALERT_TYPE_MESSAGE)
		? [
			bold(_('Subject').':'),
			BR(),
			$alert['subject'],
			BR(),
			BR(),
			bold(_('Message').':'),
			BR(),
			zbx_nl2br($alert['message'])
		]
		: [
			bold(_('Command').':'),
			BR(),
			zbx_nl2br($alert['message'])
		];

	$info_icons = [];
	if ($alert['error'] !== '') {
		$info_icons[] = makeErrorIcon($alert['error']);
	}

	$recipient = (isset($alert['userid']) && $alert['userid'])
		? makeEventDetailsTableUser($alert + ['action_type' => TRX_EVENT_HISTORY_ALERT], $data['users'])
		: zbx_nl2br($alert['sendto']);

	$auditTable->addRow([
		zbx_date2str(DATE_TIME_FORMAT_SECONDS, $alert['clock']),
		$this->data['actions'][$alert['actionid']]['name'],
		($mediatype) ? $mediatype['name'] : '',
		$recipient,
		$message,
		$status,
		makeInformationList($info_icons)
	]);
}

// append table to form
$auditForm->addItem([$auditTable, $this->data['paging']]);

// append navigation bar js
$objData = [
	'id' => 'timeline_1',
	'domid' => 'events',
	'loadSBox' => 0,
	'loadImage' => 0,
	'dynamic' => 0,
	'mainObject' => 1
];
zbx_add_post_js('timeControl.addObject("events", '.zbx_jsvalue($data['timeline']).', '.zbx_jsvalue($objData).');');
zbx_add_post_js('timeControl.processObjects();');

// append form to widget
$auditWidget->addItem($auditForm);

return $auditWidget;
