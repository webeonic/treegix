<?php



$widget = (new CWidget())
	->setTitle(_('Discovery rules'))
	->setControls((new CTag('nav', true,
		(new CForm('get'))
			->cleanItems()
			->addItem((new CList())
				->addItem(new CSubmit('form', _('Create discovery rule')))
			)
		))
			->setAttribute('aria-label', _('Content controls'))
	)
	->addItem((new CFilter(new CUrl('discoveryconf.php')))
		->setProfile($data['profileIdx'])
		->setActiveTab($data['active_tab'])
		->addFilterTab(_('Filter'), [
			(new CFormList())->addRow(_('Name'),
				(new CTextBox('filter_name', $data['filter']['name']))
					->setWidth(TRX_TEXTAREA_FILTER_SMALL_WIDTH)
					->setAttribute('autofocus', 'autofocus')
			),
			(new CFormList())->addRow(_('Status'),
				(new CRadioButtonList('filter_status', (int) $data['filter']['status']))
					->addValue(_('Any'), -1)
					->addValue(_('Enabled'), DRULE_STATUS_ACTIVE)
					->addValue(_('Disabled'), DRULE_STATUS_DISABLED)
					->setModern(true)
			)
		])
	);

// create form
$discoveryForm = (new CForm())->setName('druleForm');

// create table
$discoveryTable = (new CTableInfo())
	->setHeader([
		(new CColHeader(
			(new CCheckBox('all_drules'))->onClick("checkAll('".$discoveryForm->getName()."', 'all_drules', 'g_druleid');")
		))->addClass(TRX_STYLE_CELL_WIDTH),
		make_sorting_header(_('Name'), 'name', $data['sort'], $data['sortorder'], 'discoveryconf.php'),
		_('IP range'),
		_('Proxy'),
		_('Interval'),
		_('Checks'),
		_('Status')
	]);

foreach ($data['drules'] as $drule) {
	$status = new CCol(
		(new CLink(
			discovery_status2str($drule['status']),
			'?g_druleid[]='.$drule['druleid'].
			'&action='.($drule['status'] == DRULE_STATUS_ACTIVE ? 'drule.massdisable' : 'drule.massenable')
		))
			->addClass(TRX_STYLE_LINK_ACTION)
			->addClass(discovery_status2style($drule['status']))
			->addSID()
	);

	$discoveryTable->addRow([
		new CCheckBox('g_druleid['.$drule['druleid'].']', $drule['druleid']),
		new CLink($drule['name'], '?form=update&druleid='.$drule['druleid']),
		$drule['iprange'],
		$drule['proxy'],
		$drule['delay'],
		!empty($drule['checks']) ? implode(', ', $drule['checks']) : '',
		$status
	]);
}

// append table to form
$discoveryForm->addItem([
	$discoveryTable,
	$this->data['paging'],
	new CActionButtonList('action', 'g_druleid', [
		'drule.massenable' => ['name' => _('Enable'), 'confirm' => _('Enable selected discovery rules?')],
		'drule.massdisable' => ['name' => _('Disable'), 'confirm' => _('Disable selected discovery rules?')],
		'drule.massdelete' => ['name' => _('Delete'), 'confirm' => _('Delete selected discovery rules?')]
	])
]);

// append form to widget
$widget->addItem($discoveryForm);

return $widget;
