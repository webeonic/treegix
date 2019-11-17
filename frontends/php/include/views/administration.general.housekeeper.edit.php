<?php



require_once dirname(__FILE__).'/js/administration.general.housekeeper.edit.js.php';

$widget = (new CWidget())
	->setTitle(_('Housekeeping'))
	->setControls((new CTag('nav', true,
		(new CForm())
			->cleanItems()
			->addItem((new CList())
				->addItem(makeAdministrationGeneralMenu('adm.housekeeper.php'))
			)
		))
			->setAttribute('aria-label', _('Content controls'))
	);

$houseKeeperTab = (new CFormList())
	->addRow(new CTag('h4', true, _('Events and alerts')))
	->addRow(
		new CLabel(_('Enable internal housekeeping'), 'hk_events_mode'),
		(new CCheckBox('hk_events_mode'))
			->setChecked($data['hk_events_mode'] == 1)
			->setAttribute('autofocus', 'autofocus')
	)
	->addRow(
		(new CLabel(_('Trigger data storage period'), 'hk_events_trigger'))->setAsteriskMark(),
		(new CTextBox('hk_events_trigger', $data['hk_events_trigger']))
			->setWidth(TRX_TEXTAREA_TINY_WIDTH)
			->setEnabled($data['hk_events_mode'] == 1)
			->setAriaRequired()
	)
	->addRow(
		(new CLabel(_('Internal data storage period'), 'hk_events_internal'))->setAsteriskMark(),
		(new CTextBox('hk_events_internal', $data['hk_events_internal']))
			->setWidth(TRX_TEXTAREA_TINY_WIDTH)
			->setEnabled($data['hk_events_mode'] == 1)
			->setAriaRequired()
	)
	->addRow(
		(new CLabel(_('Network discovery data storage period'), 'hk_events_discovery'))
			->setAsteriskMark(),
		(new CTextBox('hk_events_discovery', $data['hk_events_discovery']))
			->setWidth(TRX_TEXTAREA_TINY_WIDTH)
			->setEnabled($data['hk_events_mode'] == 1)
			->setAriaRequired()
	)
	->addRow(
		(new CLabel(_('Auto-registration data storage period'), 'hk_events_autoreg'))
			->setAsteriskMark(),
		(new CTextBox('hk_events_autoreg', $data['hk_events_autoreg']))
			->setWidth(TRX_TEXTAREA_TINY_WIDTH)
			->setEnabled($data['hk_events_mode'] == 1)
			->setAriaRequired()
	)
	->addRow(null)
	->addRow(new CTag('h4', true, _('Services')))
	->addRow(
		new CLabel(_('Enable internal housekeeping'), 'hk_services_mode'),
		(new CCheckBox('hk_services_mode'))->setChecked($data['hk_services_mode'] == 1)
	)
	->addRow(
		(new CLabel(_('Data storage period'), 'hk_services'))
			->setAsteriskMark(),
		(new CTextBox('hk_services', $data['hk_services']))
			->setWidth(TRX_TEXTAREA_TINY_WIDTH)
			->setEnabled($data['hk_services_mode'] == 1)
			->setAriaRequired()
	)
	->addRow(null)
	->addRow(new CTag('h4', true, _('Audit')))
	->addRow(
		new CLabel(_('Enable internal housekeeping'), 'hk_audit_mode'),
		(new CCheckBox('hk_audit_mode'))->setChecked($data['hk_audit_mode'] == 1)
	)
	->addRow(
		(new CLabel(_('Data storage period'), 'hk_audit'))
			->setAsteriskMark(),
		(new CTextBox('hk_audit', $data['hk_audit']))
			->setWidth(TRX_TEXTAREA_TINY_WIDTH)
			->setEnabled($data['hk_audit_mode'] == 1)
			->setAriaRequired()
	)
	->addRow(null)
	->addRow(new CTag('h4', true, _('User sessions')))
	->addRow(
		new CLabel(_('Enable internal housekeeping'), 'hk_sessions_mode'),
		(new CCheckBox('hk_sessions_mode'))->setChecked($data['hk_sessions_mode'] == 1)
	)
	->addRow(
		(new CLabel(_('Data storage period'), 'hk_sessions'))
			->setAsteriskMark(),
		(new CTextBox('hk_sessions', $data['hk_sessions']))
			->setWidth(TRX_TEXTAREA_TINY_WIDTH)
			->setEnabled($data['hk_sessions_mode'] == 1)
			->setAriaRequired()
	)
	->addRow(null)
	->addRow(new CTag('h4', true, _('History')))
	->addRow(
		new CLabel(_('Enable internal housekeeping'), 'hk_history_mode'),
		(new CCheckBox('hk_history_mode'))->setChecked($data['hk_history_mode'] == 1)
	)
	->addRow(
		new CLabel(_('Override item history period'), 'hk_history_global'),
		(new CCheckBox('hk_history_global'))->setChecked($data['hk_history_global'] == 1)
	)
	->addRow(
		(new CLabel(_('Data storage period'), 'hk_history'))
			->setAsteriskMark(),
		(new CTextBox('hk_history', $data['hk_history']))
			->setWidth(TRX_TEXTAREA_TINY_WIDTH)
			->setEnabled($data['hk_history_global'] == 1)
			->setAriaRequired()
	)
	->addRow(null)
	->addRow(new CTag('h4', true, _('Trends')))
	->addRow(
		new CLabel(_('Enable internal housekeeping'), 'hk_trends_mode'),
		(new CCheckBox('hk_trends_mode'))->setChecked($data['hk_trends_mode'] == 1)
	)
	->addRow(
		new CLabel(_('Override item trend period'), 'hk_trends_global'),
		(new CCheckBox('hk_trends_global'))->setChecked($data['hk_trends_global'] == 1)
	)
	->addRow(
		(new CLabel(_('Data storage period'), 'hk_trends'))
			->setAsteriskMark(),
		(new CTextBox('hk_trends', $data['hk_trends']))
			->setWidth(TRX_TEXTAREA_TINY_WIDTH)
			->setEnabled($data['hk_trends_global'] == 1)
			->setAriaRequired()
	);

$houseKeeperView = (new CTabView())
	->addTab('houseKeeper', _('Housekeeping'), $houseKeeperTab)
	->setFooter(makeFormFooter(
		new CSubmit('update', _('Update')),
		[new CButton('resetDefaults', _('Reset defaults'))]
	));

$widget->addItem((new CForm())
	->setAttribute('aria-labeledby', TRX_STYLE_PAGE_TITLE)
	->addItem($houseKeeperView)
);

return $widget;
