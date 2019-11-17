<?php



$widget = (new CWidget())->setTitle(_('Host groups'));

$form = (new CForm())
	->setName('hostgroupForm')
	->setAttribute('aria-labeledby', TRX_STYLE_PAGE_TITLE)
	->addVar('groupid', $data['groupid'])
	->addVar('form', $data['form']);

$form_list = (new CFormList('hostgroupFormList'))
	->addRow(
		(new CLabel(_('Group name'), 'name'))->setAsteriskMark(),
		(new CTextBox('name', $data['name'], $data['groupid'] && $data['group']['flags'] == TRX_FLAG_DISCOVERY_CREATED))
			->setAttribute('autofocus', 'autofocus')
			->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired()
	);

if ($data['groupid'] != 0 && CWebUser::getType() == USER_TYPE_SUPER_ADMIN) {
	$form_list->addRow(null,
		(new CCheckBox('subgroups'))
			->setLabel(_('Apply permissions and tag filters to all subgroups'))
			->setChecked($data['subgroups'])
	);
}

$tab = (new CTabView())->addTab('hostgroupTab', _('Host group'), $form_list);

if ($data['groupid'] == 0) {
	$tab->setFooter(makeFormFooter(
		new CSubmit('add', _('Add')),
		[new CButtonCancel()]
	));
}
else {
	$tab->setFooter(makeFormFooter(
		new CSubmit('update', _('Update')), [
			(new CSubmit('clone', _('Clone')))->setEnabled(CWebUser::getType() == USER_TYPE_SUPER_ADMIN),
			(new CButtonDelete(_('Delete selected group?'), url_param('form').url_param('groupid')))
				->setEnabled(array_key_exists($data['groupid'], $data['deletable_host_groups'])),
			new CButtonCancel()
		]
	));
}

$form->addItem($tab);

$widget->addItem($form);

return $widget;
