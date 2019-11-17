<?php


$widget = (new CWidget())
	->setTitle(_('Images'))
	->setControls((new CForm())
		->cleanItems()
		->setAttribute('aria-label', _('Main filter'))
		->addItem((new CList())
			->addItem(makeAdministrationGeneralMenu('adm.images.php'))
		)
	);

$imageForm = (new CForm('post', null, 'multipart/form-data'))
	->setAttribute('aria-labeledby', TRX_STYLE_PAGE_TITLE)
	->addVar('form', $this->data['form']);
if (isset($this->data['imageid'])) {
	$imageForm->addVar('imageid', $this->data['imageid']);
}
$imageForm->addVar('imagetype', $this->data['imagetype']);

// append form list
$imageFormList = (new CFormList('imageFormList'))
	->addRow(
		(new CLabel(_('Name'), 'name'))->setAsteriskMark(),
		(new CTextBox('name', $this->data['imagename'], false, 64))
			->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
			->setAttribute('autofocus', 'autofocus')
			->setAriaRequired()
	)
	->addRow(
		(new CLabel(_('Upload'), 'image'))->setAsteriskMark(),
		(new CFile('image'))->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired()
	);

if (isset($this->data['imageid'])) {
	if ($this->data['imagetype'] == IMAGE_TYPE_BACKGROUND) {
		$imageFormList->addRow(_('Image'), new CLink(
			(new CImg('imgstore.php?iconid='.$this->data['imageid'], 'no image'))->addStyle('max-width:100%;'),
			'image.php?imageid='.$this->data['imageid']
		));
	}
	else {
		$imageFormList->addRow(_('Image'),
			(new CImg('imgstore.php?iconid='.$this->data['imageid'], 'no image', null))->addStyle('max-width:100%;')
		);
	}
}

// append tab
$imageTab = (new CTabView())
	->addTab('imageTab', ($this->data['imagetype'] == IMAGE_TYPE_ICON) ? _('Icon') : _('Background'), $imageFormList);

// append buttons
if (isset($this->data['imageid'])) {
	$imageTab->setFooter(makeFormFooter(
		new CSubmit('update', _('Update')),
		[
			new CButtonDelete(_('Delete selected image?'), url_param('form').url_param('imageid').
				url_param($data['imagetype'], false, 'imagetype')
			),
			new CButtonCancel(url_param($data['imagetype'], false, 'imagetype'))
		]
	));
}
else {
	$imageTab->setFooter(makeFormFooter(
		new CSubmit('add', _('Add')),
		[new CButtonCancel(url_param($data['imagetype'], false, 'imagetype'))]
	));
}

$imageForm->addItem($imageTab);

$widget->addItem($imageForm);

return $widget;
