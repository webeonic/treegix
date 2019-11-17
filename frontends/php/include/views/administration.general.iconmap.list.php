<?php
 


$widget = (new CWidget())
	->setTitle(_('Icon mapping'))
	->setControls((new CTag('nav', true,
		(new CForm())
			->cleanItems()
			->addItem((new CList())
				->addItem(makeAdministrationGeneralMenu('adm.iconmapping.php'))
				->addItem(new CSubmit('form', _('Create icon map')))
			)
		))
			->setAttribute('aria-label', _('Content controls'))
	);

$iconMapTable = (new CTableInfo())
	->setHeader([
		_('Name'),
		_('Icon map')
	]);

foreach ($this->data['iconmaps'] as $iconMap) {
	$row = [];
	foreach ($iconMap['mappings'] as $mapping) {
		$row[] = $this->data['inventoryList'][$mapping['inventory_link']].NAME_DELIMITER.
				$mapping['expression'].SPACE.'&rArr;'.SPACE.$this->data['iconList'][$mapping['iconid']];
		$row[] = BR();
	}

	$iconMapTable->addRow([
		new CLink($iconMap['name'], 'adm.iconmapping.php?form=update&iconmapid='.$iconMap['iconmapid']),
		$row
	]);
}

$widget->addItem($iconMapTable);

return $widget;
