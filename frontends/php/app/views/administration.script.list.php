<?php



if ($data['uncheck']) {
	uncheckTableRows('script');
}

$widget = (new CWidget())
	->setTitle(_('Scripts'))
	->setControls((new CTag('nav', true,
		(new CList())
			->addItem(new CRedirectButton(_('Create script'), 'treegix.php?action=script.edit'))
		))
			->setAttribute('aria-label', _('Content controls'))
	)
	->addItem((new CFilter((new CUrl('treegix.php'))->setArgument('action', 'script.list')))
		->setProfile($data['profileIdx'])
		->setActiveTab($data['active_tab'])
		->addFilterTab(_('Filter'), [
			(new CFormList())->addRow(_('Name'),
				(new CTextBox('filter_name', $data['filter']['name']))
					->setWidth(TRX_TEXTAREA_FILTER_SMALL_WIDTH)
					->setAttribute('autofocus', 'autofocus')
			)
		])
		->addVar('action', 'script.list')
	);

$scriptsForm = (new CForm())
	->setName('scriptsForm')
	->setId('scripts');

$scriptsTable = (new CTableInfo())
	->setHeader([
		(new CColHeader(
			(new CCheckBox('all_scripts'))->onClick("checkAll('".$scriptsForm->getName()."', 'all_scripts', 'scriptids');")
		))->addClass(TRX_STYLE_CELL_WIDTH),
		make_sorting_header(_('Name'), 'name', $data['sort'], $data['sortorder']),
		_('Type'),
		_('Execute on'),
		make_sorting_header(_('Commands'), 'command', $data['sort'], $data['sortorder']),
		_('User group'),
		_('Host group'),
		_('Host access')
	]);

foreach ($data['scripts'] as $script) {
	switch ($script['type']) {
		case TRX_SCRIPT_TYPE_CUSTOM_SCRIPT:
			$scriptType = _('Script');
			break;
		case TRX_SCRIPT_TYPE_IPMI:
			$scriptType = _('IPMI');
			break;
	}

	if ($script['type'] == TRX_SCRIPT_TYPE_CUSTOM_SCRIPT) {
		switch ($script['execute_on']) {
			case TRX_SCRIPT_EXECUTE_ON_AGENT:
				$scriptExecuteOn = _('Agent');
				break;
			case TRX_SCRIPT_EXECUTE_ON_SERVER:
				$scriptExecuteOn = _('Server');
				break;
			case TRX_SCRIPT_EXECUTE_ON_PROXY:
				$scriptExecuteOn = _('Server (proxy)');
				break;
		}
	}
	else {
		$scriptExecuteOn = '';
	}

	$scriptsTable->addRow([
		new CCheckBox('scriptids['.$script['scriptid'].']', $script['scriptid']),
		(new CCol(
			new CLink($script['name'], 'treegix.php?action=script.edit&scriptid='.$script['scriptid'])
		))->addClass(TRX_STYLE_NOWRAP),
		$scriptType,
		$scriptExecuteOn,
		(new CCol(
			zbx_nl2br(htmlspecialchars($script['command'], ENT_COMPAT, 'UTF-8'))
		))->addClass(TRX_STYLE_MONOSPACE_FONT),
		($script['userGroupName'] === null) ? _('All') : $script['userGroupName'],
		($script['hostGroupName'] === null) ? _('All') : $script['hostGroupName'],
		($script['host_access'] == PERM_READ_WRITE) ? _('Write') : _('Read')
	]);
}

// append table to form
$scriptsForm->addItem([
	$scriptsTable,
	$data['paging'],
	new CActionButtonList('action', 'scriptids', [
		'script.delete' => ['name' => _('Delete'), 'confirm' => _('Delete selected scripts?')]
	], 'script')
]);

// append form to widget
$widget->addItem($scriptsForm)->show();
