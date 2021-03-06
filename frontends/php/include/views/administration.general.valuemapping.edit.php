<?php



include('include/views/js/administration.general.valuemapping.edit.js.php');

$widget = (new CWidget())
	->setTitle(_('Value mapping'))
	->setControls((new CTag('nav', true,
		(new CForm())
			->cleanItems()
			->addItem((new CList())
				->addItem(makeAdministrationGeneralMenu('adm.valuemapping.php'))
			)
		))
			->setAttribute('aria-label', _('Content controls'))
	);

$form = (new CForm())
	->setAttribute('aria-labeledby', TRX_STYLE_PAGE_TITLE)
	->addVar('form', $data['form']);

if ($data['valuemapid'] != 0) {
	$form->addVar('valuemapid', $data['valuemapid']);
}

$form_list = (new CFormList())
	->addRow(
		(new CLabel(_('Name'), 'name'))->setAsteriskMark(),
		(new CTextBox('name', $data['name'], false, 64))
			->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired()
			->setAttribute('autofocus', 'autofocus')
	);

$table = (new CTable())
	->setId('mappings_table')
	->setHeader([_('Value'), '', _('Mapped to'), _('Action')])
	->setAttribute('style', 'width: 100%;');

foreach ($data['mappings'] as $i => $mapping) {
	$table->addRow([
		(new CTextBox('mappings['.$i.'][value]', $mapping['value'], false, 64))->setWidth(TRX_TEXTAREA_SMALL_WIDTH),
		'&rArr;',
		(new CTextBox('mappings['.$i.'][newvalue]', $mapping['newvalue'], false, 64))
			->setWidth(TRX_TEXTAREA_SMALL_WIDTH)
			->setAriaRequired(),
		(new CButton('mappings['.$i.'][remove]', _('Remove')))
			->addClass(TRX_STYLE_BTN_LINK)
			->addClass('element-table-remove')
		],
		'form_row'
	);
}

$table->addRow([
	(new CCol(
		(new CButton('mapping_add', _('Add')))
			->addClass(TRX_STYLE_BTN_LINK)
			->addClass('element-table-add')
	))->setColSpan(4)
]);

$form_list->addRow(
	(new CLabel(_('Mappings'), $table->getId()))->setAsteriskMark(),
	(new CDiv($table))
		->addClass(TRX_STYLE_TABLE_FORMS_SEPARATOR)
		->setAttribute('style', 'min-width: '.TRX_TEXTAREA_STANDARD_WIDTH.'px;')
);

// append form list to tab
$tab_view = (new CTabView())->addTab('valuemap_tab', _('Value mapping'), $form_list);

// append buttons
if ($data['valuemapid'] != 0) {
	if ($data['valuemap_count'] == 0) {
		$confirm_message = _('Delete selected value mapping?');
	}
	else {
		$confirm_message = _n(
			'Delete selected value mapping? It is used for %d item!',
			'Delete selected value mapping? It is used for %d items!',
			$data['valuemap_count']
		);
	}

	$tab_view->setFooter(makeFormFooter(
		new CSubmit('update', _('Update')),
		[
			new CButton('clone', _('Clone')),
			new CButtonDelete($confirm_message, '&action=valuemap.delete&valuemapids[]='.$data['valuemapid']),
			new CButtonCancel()
		]
	));
}
else {
	$tab_view->setFooter(makeFormFooter(
		new CSubmit('add', _('Add')),
		[new CButtonCancel()]
	));
}

$form->addItem($tab_view);

$widget->addItem($form);

return $widget;
