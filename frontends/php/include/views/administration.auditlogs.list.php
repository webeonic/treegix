<?php


$auditWidget = (new CWidget())->setTitle(_('Audit log'));

// header

$filterColumn = new CFormList();
$filterColumn->addRow(_('User'), [
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
				'dstfrm' => 'trx_filter',
				'dstfld1' => 'alias'
			]).', null, this);'
		)
]);
$filterColumn->addRow(_('Action'), new CComboBox('action', $this->data['action'], null, [
	-1 => _('All'),
	AUDIT_ACTION_LOGIN => _('Login'),
	AUDIT_ACTION_LOGOUT => _('Logout'),
	AUDIT_ACTION_ADD => _('Add'),
	AUDIT_ACTION_UPDATE => _('Update'),
	AUDIT_ACTION_DELETE => _('Delete'),
	AUDIT_ACTION_ENABLE => _('Enable'),
	AUDIT_ACTION_DISABLE => _('Disable')
]));
$filterColumn->addRow(_('Resource'), new CComboBox('resourcetype', $this->data['resourcetype'], null,
	[-1 => _('All')] + audit_resource2str()
));

$auditWidget->addItem(
	(new CFilter(new CUrl('auditlogs.php')))
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
		_('User'),
		_('IP'),
		_('Resource'),
		_('Action'),
		_('ID'),
		_('Description'),
		_('Details')
	]);
foreach ($this->data['actions'] as $action) {
	$details = [];
	if (is_array($action['details'])) {
		foreach ($action['details'] as $detail) {
			$details[] = [$detail['table_name'].'.'.$detail['field_name'].NAME_DELIMITER.$detail['oldvalue'].' => '.$detail['newvalue'], BR()];
		}
	}
	else {
		$details = $action['details'];
	}

	$auditTable->addRow([
		trx_date2str(DATE_TIME_FORMAT_SECONDS, $action['clock']),
		$action['alias'],
		$action['ip'],
		$action['resourcetype'],
		$action['action'],
		$action['resourceid'],
		$action['resourcename'],
		$details
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
trx_add_post_js('timeControl.addObject("events", '.trx_jsvalue($this->data['timeline']).', '.trx_jsvalue($objData).');');
trx_add_post_js('timeControl.processObjects();');

// append form to widget
$auditWidget->addItem($auditForm);

return $auditWidget;
