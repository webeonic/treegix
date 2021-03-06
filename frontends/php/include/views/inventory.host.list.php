<?php



$hostInventoryWidget = (new CWidget())
	->setTitle(_('Host inventory'))
	->setControls((new CForm('get'))
		->setAttribute('aria-label', _('Main filter'))
		->addItem((new CList())
			->addItem([
				new CLabel(_('Group'), 'groupid'),
				(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
				$this->data['pageFilter']->getGroupsCB()
			])
		)
	);

// getting inventory fields to make a drop down
$inventoryFields = getHostInventories(true); // 'true' means list should be ordered by title
$inventoryFieldsComboBox = (new CComboBox('filter_field', $this->data['filterField']))
	->setAttribute('autofocus', 'autofocus');
foreach ($inventoryFields as $inventoryField) {
	$inventoryFieldsComboBox->addItem($inventoryField['db_field'], $inventoryField['title']);
}

// filter
$hostInventoryWidget->addItem(
	(new CFilter(new CUrl('hostinventories.php')))
		->setProfile($data['profileIdx'])
		->setActiveTab($data['active_tab'])
		->addFilterTab(_('Filter'), [
			(new CFormList())->addRow(_('Field'), [
				$inventoryFieldsComboBox,
				(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
				new CComboBox('filter_exact', $this->data['filterExact'], null, [
					0 => _('contains'),
					1 => _('equals')
				]),
				(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
				(new CTextBox('filter_field_value', $this->data['filterFieldValue']))->setWidth(TRX_TEXTAREA_SMALL_WIDTH)
			])
		])
);

$table = (new CTableInfo())
	->setHeader([
		make_sorting_header(_('Host'), 'name', $this->data['sort'], $this->data['sortorder']),
		_('Group'),
		make_sorting_header(_('Name'), 'pr_name', $this->data['sort'], $this->data['sortorder']),
		make_sorting_header(_('Type'), 'pr_type', $this->data['sort'], $this->data['sortorder']),
		make_sorting_header(_('OS'), 'pr_os', $this->data['sort'], $this->data['sortorder']),
		make_sorting_header(_('Serial number A'), 'pr_serialno_a', $this->data['sort'], $this->data['sortorder']),
		make_sorting_header(_('Tag'), 'pr_tag', $this->data['sort'], $this->data['sortorder']),
		make_sorting_header(_('MAC address A'), 'pr_macaddress_a', $this->data['sort'], $this->data['sortorder'])
	]);

foreach ($this->data['hosts'] as $host) {
	$hostGroups = [];
	foreach ($host['groups'] as $group) {
		$hostGroups[] = $group['name'];
	}
	natsort($hostGroups);
	$hostGroups = implode(', ', $hostGroups);

	$row = [
		(new CLink($host['name'], '?hostid='.$host['hostid'].url_param('groupid')))
			->addClass($host['status'] == HOST_STATUS_NOT_MONITORED ? TRX_STYLE_RED : null),
		$hostGroups,
		trx_str2links($host['inventory']['name']),
		trx_str2links($host['inventory']['type']),
		trx_str2links($host['inventory']['os']),
		trx_str2links($host['inventory']['serialno_a']),
		trx_str2links($host['inventory']['tag']),
		trx_str2links($host['inventory']['macaddress_a'])
	];

	$table->addRow($row);
}

$table = [$table, $this->data['paging']];
$hostInventoryWidget->addItem($table);

return $hostInventoryWidget;
