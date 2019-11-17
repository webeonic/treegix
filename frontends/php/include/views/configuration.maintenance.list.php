<?php


$widget = (new CWidget())
	->setTitle(_('Maintenance periods'))
	->setControls(new CList([
		(new CForm('get'))
			->cleanItems()
			->setAttribute('aria-label', _('Main filter'))
			->addItem((new CList())
				->addItem([
					new CLabel(_('Group'), 'groupid'),
					(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
					$this->data['pageFilter']->getGroupsCB()
				])
			),
		(new CTag('nav', true, new CRedirectButton(_('Create maintenance period'), (new CUrl('maintenance.php'))
			->removeArgument('maintenanceid')
			->setArgument('groupid', $data['pageFilter']->groupid)
			->setArgument('form', 'create')
			->getUrl()
		)))->setAttribute('aria-label', _('Content controls'))
	]))
	->addItem((new CFilter(new CUrl('maintenance.php')))
		->setProfile($data['profileIdx'])
		->setActiveTab($data['active_tab'])
		->addFilterTab(_('Filter'), [
			(new CFormList())->addRow(_('Name'),
				(new CTextBox('filter_name', $data['filter']['name']))
					->setWidth(TRX_TEXTAREA_FILTER_SMALL_WIDTH)
					->setAttribute('autofocus', 'autofocus')
			),
			(new CFormList())->addRow(_('State'),
				(new CRadioButtonList('filter_status', (int) $data['filter']['status']))
					->addValue(_('Any'), -1)
					->addValue(_x('Active', 'maintenance status'), MAINTENANCE_STATUS_ACTIVE)
					->addValue(_x('Approaching', 'maintenance status'), MAINTENANCE_STATUS_APPROACH)
					->addValue(_x('Expired', 'maintenance status'), MAINTENANCE_STATUS_EXPIRED)
					->setModern(true)
			)
		])
	);

// create form
$maintenanceForm = (new CForm())->setName('maintenanceForm');

// create table
$maintenanceTable = (new CTableInfo())
	->setHeader([
		(new CColHeader(
			(new CCheckBox('all_maintenances'))->onClick("checkAll('".$maintenanceForm->getName()."', 'all_maintenances', 'maintenanceids');")
		))->addClass(TRX_STYLE_CELL_WIDTH),
		make_sorting_header(_('Name'), 'name', $this->data['sort'], $this->data['sortorder']),
		make_sorting_header(_('Type'), 'maintenance_type', $this->data['sort'], $this->data['sortorder']),
		make_sorting_header(_('Active since'), 'active_since', $this->data['sort'], $this->data['sortorder']),
		make_sorting_header(_('Active till'), 'active_till', $this->data['sort'], $this->data['sortorder']),
		_('State'),
		_('Description')
	]);

foreach ($this->data['maintenances'] as $maintenance) {
	$maintenanceid = $maintenance['maintenanceid'];

	switch ($maintenance['status']) {
		case MAINTENANCE_STATUS_EXPIRED:
			$maintenanceStatus = (new CSpan(_x('Expired', 'maintenance status')))->addClass(TRX_STYLE_RED);
			break;
		case MAINTENANCE_STATUS_APPROACH:
			$maintenanceStatus = (new CSpan(_x('Approaching', 'maintenance status')))->addClass(TRX_STYLE_ORANGE);
			break;
		case MAINTENANCE_STATUS_ACTIVE:
			$maintenanceStatus = (new CSpan(_x('Active', 'maintenance status')))->addClass(TRX_STYLE_GREEN);
			break;
	}

	$maintenanceTable->addRow([
		new CCheckBox('maintenanceids['.$maintenanceid.']', $maintenanceid),
		new CLink($maintenance['name'], 'maintenance.php?form=update&maintenanceid='.$maintenanceid),
		$maintenance['maintenance_type'] ? _('No data collection') : _('With data collection'),
		zbx_date2str(DATE_TIME_FORMAT, $maintenance['active_since']),
		zbx_date2str(DATE_TIME_FORMAT, $maintenance['active_till']),
		$maintenanceStatus,
		$maintenance['description']
	]);
}

// append table to form
$maintenanceForm->addItem([
	$maintenanceTable,
	$this->data['paging'],
	new CActionButtonList('action', 'maintenanceids', [
		'maintenance.massdelete' => ['name' => _('Delete'), 'confirm' => _('Delete selected maintenance periods?')]
	])
]);

// append form to widget
$widget->addItem($maintenanceForm);

return $widget;
