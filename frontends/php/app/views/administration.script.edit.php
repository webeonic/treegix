<?php


$this->addJsFile('multiselect.js');
$this->includeJSfile('app/views/administration.script.edit.js.php');

$widget = (new CWidget())->setTitle(_('Scripts'));

$scriptForm = (new CForm())
	->setId('scriptForm')
	->setName('scripts')
	->setAttribute('aria-labeledby', TRX_STYLE_PAGE_TITLE)
	->addVar('form', 1)
	->addVar('scriptid', $data['scriptid']);

$scriptFormList = (new CFormList())
	->addRow((new CLabel(_('Name'), 'name'))->setAsteriskMark(),
		(new CTextBox('name', $data['name']))
			->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
			->setAttribute('autofocus', 'autofocus')
			->setAttribute('placeholder', _('<sub-menu/sub-menu/...>script'))
			->setAriaRequired()
	)
	->addRow((new CLabel(_('Type'), 'type')),
		(new CRadioButtonList('type', (int) $data['type']))
			->addValue(_('IPMI'), TRX_SCRIPT_TYPE_IPMI)
			->addValue(_('Script'), TRX_SCRIPT_TYPE_CUSTOM_SCRIPT)
			->setModern(true)
	)
	->addRow((new CLabel(_('Execute on'), 'execute_on')),
		(new CRadioButtonList('execute_on', (int) $data['execute_on']))
			->addValue(_('Treegix agent'), TRX_SCRIPT_EXECUTE_ON_AGENT)
			->addValue(_('Treegix server (proxy)'), TRX_SCRIPT_EXECUTE_ON_PROXY)
			->addValue(_('Treegix server'), TRX_SCRIPT_EXECUTE_ON_SERVER)
			->setModern(true)
	)
	->addRow((new CLabel(_('Commands'), 'command'))->setAsteriskMark(),
		(new CTextArea('command', $data['command']))
			->addClass(TRX_STYLE_MONOSPACE_FONT)
			->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
			->setMaxLength(255)
			->setAriaRequired()
	)
	->addRow((new CLabel(_('Command'), 'commandipmi'))->setAsteriskMark(),
		(new CTextBox('commandipmi', $data['commandipmi']))
			->addClass(TRX_STYLE_MONOSPACE_FONT)
			->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired()
	)
	->addRow(_('Description'),
		(new CTextArea('description', $data['description']))->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
	);

$user_groups = [0 => _('All')];
foreach ($data['usergroups'] as $user_group) {
	$user_groups[$user_group['usrgrpid']] = $user_group['name'];
}
$scriptFormList
	->addRow(_('User group'),
		new CComboBox('usrgrpid', $data['usrgrpid'], null, $user_groups))
	->addRow(_('Host group'),
		new CComboBox('hgstype', $data['hgstype'], null, [
			0 => _('All'),
			1 => _('Selected')
		])
	)
	->addRow(null, (new CMultiSelect([
		'name' => 'groupid',
		'object_name' => 'hostGroup',
		'multiple' => false,
		'data' => $data['hostgroup'],
		'popup' => [
			'parameters' => [
				'srctbl' => 'host_groups',
				'srcfld1' => 'groupid',
				'dstfrm' => $scriptForm->getName(),
				'dstfld1' => 'groupid'
			]
		]
	]))->setWidth(TRX_TEXTAREA_STANDARD_WIDTH), 'hostGroupSelection')
	->addRow((new CLabel(_('Required host permissions'), 'host_access')),
		(new CRadioButtonList('host_access', (int) $data['host_access']))
			->addValue(_('Read'), PERM_READ)
			->addValue(_('Write'), PERM_READ_WRITE)
			->setModern(true)
	)
	->addRow(_('Enable confirmation'),
		(new CCheckBox('enable_confirmation'))->setChecked($data['enable_confirmation'] == 1)
	);

$scriptFormList->addRow(new CLabel(_('Confirmation text'), 'confirmation'), [
	(new CTextBox('confirmation', $data['confirmation']))->setWidth(TRX_TEXTAREA_STANDARD_WIDTH),
	SPACE,
	(new CButton('testConfirmation', _('Test confirmation')))->addClass(TRX_STYLE_BTN_GREY)
]);

$scriptView = (new CTabView())->addTab('scripts', _('Script'), $scriptFormList);

// footer
$cancelButton = (new CRedirectButton(_('Cancel'), 'treegix.php?action=script.list'))->setId('cancel');

if ($data['scriptid'] == 0) {
	$addButton = (new CSubmitButton(_('Add'), 'action', 'script.create'))->setId('add');

	$scriptView->setFooter(makeFormFooter(
		$addButton,
		[$cancelButton]
	));
}
else {
	$updateButton = (new CSubmitButton(_('Update'), 'action', 'script.update'))->setId('update');
	$cloneButton = (new CSimpleButton(_('Clone')))->setId('clone');
	$deleteButton = (new CRedirectButton(_('Delete'),
		'treegix.php?action=script.delete&sid='.$data['sid'].'&scriptids[]='.$data['scriptid'],
		_('Delete script?')
	))
		->setId('delete');

	$scriptView->setFooter(makeFormFooter(
		$updateButton,
		[
			$cloneButton,
			$deleteButton,
			$cancelButton
		]
	));
}

$scriptForm->addItem($scriptView);

$widget->addItem($scriptForm)->show();
