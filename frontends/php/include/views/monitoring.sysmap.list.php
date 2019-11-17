<?php



$widget = (new CWidget())
	->setTitle(_('Maps'))
	->setControls((new CTag('nav', true,
		(new CForm('get'))
			->cleanItems()
			->addItem((new CList())
				->addItem(new CSubmit('form', _('Create map')))
				->addItem(
					(new CButton('form', _('Import')))
						->onClick('redirect("map.import.php?rules_preset=map")')
						->removeId()
				)
		)))->setAttribute('aria-label', _('Content controls'))
	)
	->addItem(
		(new CFilter(new CUrl('sysmaps.php')))
			->setProfile($data['profileIdx'])
			->setActiveTab($data['active_tab'])
			->addFilterTab(_('Filter'), [
				(new CFormList())->addRow(_('Name'),
					(new CTextBox('filter_name', $data['filter']['name']))
						->setWidth(TRX_TEXTAREA_FILTER_STANDARD_WIDTH)
						->setAttribute('autofocus', 'autofocus')
				)
			])
	);

// create form
$sysmapForm = (new CForm())->setName('frm_maps');

// create table
$sysmapTable = (new CTableInfo())
	->setHeader([
		(new CColHeader(
			(new CCheckBox('all_maps'))->onClick("checkAll('".$sysmapForm->getName()."', 'all_maps', 'maps');")
		))->addClass(TRX_STYLE_CELL_WIDTH),
		make_sorting_header(_('Name'), 'name', $this->data['sort'], $this->data['sortorder']),
		make_sorting_header(_('Width'), 'width', $this->data['sort'], $this->data['sortorder']),
		make_sorting_header(_('Height'), 'height', $this->data['sort'], $this->data['sortorder']),
		_('Actions')
	]);

foreach ($this->data['maps'] as $map) {
	$user_type = CWebUser::getType();
	if ($user_type == USER_TYPE_SUPER_ADMIN || $map['editable']) {
		$checkbox = new CCheckBox('maps['.$map['sysmapid'].']', $map['sysmapid']);
		$action = new CLink(_('Properties'), 'sysmaps.php?form=update&sysmapid='.$map['sysmapid']);
		$constructor = new CLink(_('Constructor'), 'sysmap.php?sysmapid='.$map['sysmapid']);
	}
	else {
		$checkbox = (new CCheckBox('maps['.$map['sysmapid'].']', $map['sysmapid']))
			->setAttribute('disabled', 'disabled');
		$action = '';
		$constructor = '';
	}
	$sysmapTable->addRow([
		$checkbox,
		new CLink($map['name'], 'treegix.php?action=map.view&sysmapid='.$map['sysmapid']),
		$map['width'],
		$map['height'],
		new CHorList([$action, $constructor])
	]);
}

// append table to form
$sysmapForm->addItem([
	$sysmapTable,
	$this->data['paging'],
	new CActionButtonList('action', 'maps', [
		'map.export' => ['name' => _('Export'), 'redirect' =>
			(new CUrl('treegix.php'))
				->setArgument('action', 'export.sysmaps.xml')
				->setArgument('backurl', (new CUrl('sysmaps.php'))
					->setArgument('page', getPageNumber())
					->getUrl())
				->getUrl()
		],
		'map.massdelete' => ['name' => _('Delete'), 'confirm' => _('Delete selected maps?')]
	])
]);

// append form to widget
$widget->addItem($sysmapForm);

return $widget;
