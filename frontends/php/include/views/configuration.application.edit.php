<?php



$widget = (new CWidget())
	->setTitle(_('Applications'))
	->addItem(get_header_host_table('applications', $this->data['hostid']));

// create form
$applicationForm = (new CForm())
	->setAttribute('aria-labeledby', TRX_STYLE_PAGE_TITLE)
	->addVar('form', $this->data['form'])
	->addVar('hostid', $this->data['hostid']);
if (!empty($this->data['applicationid'])) {
	$applicationForm->addVar('applicationid', $this->data['applicationid']);
}

// append tabs to form
$applicationTab = (new CTabView())
	->addTab('applicationTab', _('Application'),
		(new CFormList())
			->addRow((new CLabel(_('Name'), 'appname'))->setAsteriskMark(),
				(new CTextBox('appname', $this->data['appname']))
					->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
					->setAriaRequired()
					->setAttribute('autofocus', 'autofocus')
			)
	);

// append buttons to form
if (!empty($this->data['applicationid'])) {
	$applicationTab->setFooter(makeFormFooter(
		new CSubmit('update', _('Update')),
		[
			new CSubmit('clone', _('Clone')),
			new CButtonDelete(_('Delete application?'), url_params(['hostid', 'form', 'applicationid'])),
			new CButtonCancel(url_param('hostid'))
		]
	));
}
else {
	$applicationTab->setFooter(makeFormFooter(
		new CSubmit('add', _('Add')),
		[new CButtonCancel(url_param('hostid'))]
	));
}

$applicationForm->addItem($applicationTab);

// append form to widget
$widget->addItem($applicationForm);

return $widget;
