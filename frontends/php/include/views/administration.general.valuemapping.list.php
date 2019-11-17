<?php



$widget = (new CWidget())
	->setTitle(_('Value mapping'))
	->setControls((new CTag('nav', true,
		(new CForm())
			->cleanItems()
			->addItem((new CList())
				->addItem(makeAdministrationGeneralMenu('adm.valuemapping.php'))
				->addItem(new CSubmit('form', _('Create value map')))
				->addItem((new CButton('form', _('Import')))
					->onClick('redirect("conf.import.php?rules_preset=valuemap")')
				)
			)
		))
			->setAttribute('aria-label', _('Content controls'))
	);

$form = (new CForm())
	->setName('valuemap_form');

$table = (new CTableInfo())
	->setHeader([
		(new CColHeader(
			(new CCheckBox('all_valuemaps'))
				->onClick("checkAll('".$form->getName()."', 'all_valuemaps', 'valuemapids');")
		))->addClass(TRX_STYLE_CELL_WIDTH),
		make_sorting_header(_('Name'), 'name', $data['sort'], $data['sortorder']),
		_('Value map'),
		_('Used in items')
	]);

foreach ($data['valuemaps'] as $valuemap) {
	$mappings = [];

	foreach ($valuemap['mappings'] as $mapping) {
		$mappings[] = $mapping['value'].' &rArr; '.$mapping['newvalue'];
		$mappings[] = BR();
	}
	array_pop($mappings);

	$table->addRow([
		new CCheckBox('valuemapids['.$valuemap['valuemapid'].']', $valuemap['valuemapid']),
		new CLink($valuemap['name'], 'adm.valuemapping.php?form=update&valuemapid='.$valuemap['valuemapid']),
		$mappings,
		$valuemap['used_in_items'] ? (new CCol(_('Yes')))->addClass(TRX_STYLE_GREEN) : ''
	]);
}

$form->addItem([
	$table,
	$data['paging'],
	new CActionButtonList('action', 'valuemapids', [
		'valuemap.export' => ['name' => _('Export'), 'redirect' =>
			(new CUrl('treegix.php'))
				->setArgument('action', 'export.valuemaps.xml')
				->setArgument('backurl', (new CUrl('adm.valuemapping.php'))
					->setArgument('page', getPageNumber())
					->getUrl())
				->getUrl()
		],
		'valuemap.delete' => ['name' => _('Delete'), 'confirm' => _('Delete selected value maps?')]
	])
]);

$widget->addItem($form);

return $widget;
