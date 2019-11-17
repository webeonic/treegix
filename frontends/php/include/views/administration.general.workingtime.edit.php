<?php



$widget = (new CWidget())
	->setTitle(_('Working time'))
	->setControls((new CTag('nav', true,
		(new CForm())
			->cleanItems()
			->addItem((new CList())
				->addItem(makeAdministrationGeneralMenu('adm.workingtime.php'))
			)
		))
			->setAttribute('aria-label', _('Content controls'))
	);

$workingTimeView = (new CTabView())
	->addTab('workingTime', _('Working time'),
		(new CFormList())
			->addRow((new CLabel(_('Working time'), 'work_period'))->setAsteriskMark(),
				(new CTextBox('work_period', $data['work_period']))
					->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
					->setAriaRequired()
					->setAttribute('autofocus', 'autofocus')
			)
	)
	->setFooter(makeFormFooter(new CSubmit('update', _('Update'))));

$workingTimeForm = (new CForm())
	->setAttribute('aria-labeledby', TRX_STYLE_PAGE_TITLE)
	->addItem($workingTimeView);

$widget->addItem($workingTimeForm);

return $widget;
