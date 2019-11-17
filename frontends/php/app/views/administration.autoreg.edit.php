<?php


$this->includeJSfile('app/views/administration.autoreg.edit.js.php');

$widget = (new CWidget())
	->setTitle(_('Auto registration'))
	->setControls((new CTag('nav', true,
		(new CForm())
			->cleanItems()
			->addItem((new CList())
				->addItem(makeAdministrationGeneralMenu((new CUrl('treegix.php'))
					->setArgument('action', 'autoreg.edit')
					->getUrl()
				))
			)
		))
			->setAttribute('aria-label', _('Content controls'))
	);

$autoreg_form = (new CForm())
	->setId('autoreg-form')
	->setName('autoreg-form')
	->setAction((new CUrl('treegix.php'))
		->setArgument('action', 'autoreg.edit')
		->getUrl()
	)
	->setAttribute('aria-labeledby', ZBX_STYLE_PAGE_TITLE)
	->addVar('tls_accept', $data['tls_accept']);

$autoreg_tab = (new CFormList())
	->addRow(_('Encryption level'),
		(new CList())
			->addClass(ZBX_STYLE_LIST_CHECK_RADIO)
			->addItem((new CCheckBox('tls_in_none'))
				->setAttribute('autofocus', 'autofocus')
				->setLabel(_('No encryption'))
			)
			->addItem((new CCheckBox('tls_in_psk'))->setLabel(_('PSK')))
	);

if ($data['change_psk']) {
	$autoreg_tab
		->addRow(
			(new CLabel(_('PSK identity'), 'tls_psk_identity'))->setAsteriskMark(),
			(new CTextBox('tls_psk_identity', $data['tls_psk_identity'], false,
				DB::getFieldLength('config_autoreg_tls', 'tls_psk_identity')
			))
				->setWidth(ZBX_TEXTAREA_BIG_WIDTH)
				->setAttribute('autocomplete', 'off')
				->setAriaRequired(),
			null,
			'tls_psk'
		)
		->addRow(
			(new CLabel(_('PSK'), 'tls_psk'))->setAsteriskMark(),
			(new CTextBox('tls_psk', $data['tls_psk'], false, DB::getFieldLength('config_autoreg_tls', 'tls_psk')))
				->setWidth(ZBX_TEXTAREA_BIG_WIDTH)
				->setAttribute('autocomplete', 'off')
				->setAriaRequired(),
			null,
			'tls_psk'
		);
}
else {
	$autoreg_tab
		->addRow(
			(new CLabel(_('PSK')))->setAsteriskMark(),
			(new CSimpleButton(_('Change PSK')))
				->onClick('javascript: submitFormWithParam("'.$autoreg_form->getName().'", "change_psk", "1");')
				->addClass(ZBX_STYLE_BTN_GREY),
			null,
			'tls_psk'
		);
}

$autoreg_view = (new CTabView())
	->addTab('autoreg', _('Auto registration'), $autoreg_tab)
	->setFooter(makeFormFooter((new CSubmitButton(_('Update'), 'action', 'autoreg.update'))->setId('update')));

$autoreg_form->addItem($autoreg_view);

$widget
	->addItem($autoreg_form)
	->show();
