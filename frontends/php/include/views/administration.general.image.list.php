<?php



$widget = (new CWidget())
	->setTitle(_('Images'))
	->setControls((new CTag('nav', true,
		(new CForm())
			->cleanItems()
			->addItem((new CList())
				->addItem(makeAdministrationGeneralMenu('adm.images.php'))
				->addItem([
					new CLabel(_('Type'), 'imagetype'),
					(new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN),
					new CComboBox('imagetype', $data['imagetype'], 'submit();', [
						IMAGE_TYPE_ICON => _('Icon'),
						IMAGE_TYPE_BACKGROUND => _('Background')
					])
				])
				->addItem(
					new CSubmit('form', ($data['imagetype'] == IMAGE_TYPE_ICON)
						? _('Create icon')
						: _('Create background'))
				)
			)
		))
			->setAttribute('aria-label', _('Content controls'))
	);

if (!$data['images']) {
	$widget->addItem(new CTableInfo());
}
else {
	// header
	$imageTable = (new CDiv())
		->addClass(ZBX_STYLE_TABLE)
		->addClass(ZBX_STYLE_ADM_IMG);

	$count = 0;
	$imageRow = (new CDiv())->addClass(ZBX_STYLE_ROW);
	foreach ($data['images'] as $image) {
		$img = ($image['imagetype'] == IMAGE_TYPE_BACKGROUND)
			? new CLink(
				new CImg('imgstore.php?width=200&height=200&iconid='.$image['imageid'], 'no image'),
				'image.php?imageid='.$image['imageid']
			)
			: new CImg('imgstore.php?iconid='.$image['imageid'], 'no image');

		$imageRow->addItem(
			(new CDiv())
				->addClass(ZBX_STYLE_CELL)
				->addItem([
					$img,
					BR(),
					new CLink($image['name'], 'adm.images.php?form=update&imageid='.$image['imageid'])
				])
		);

		if ((++$count % 5) == 0) {
			$imageTable->addItem($imageRow);
			$imageRow = (new CDiv())->addClass(ZBX_STYLE_ROW);
		}
	}

	if (($count % 5) != 0) {
		$imageTable->addItem($imageRow);
	}

	$widget->addItem(
		(new CForm())->addItem(
			(new CTabView())->addTab('image', null, $imageTable)
		)
	);
}

return $widget;
