<?php



include('include/views/js/administration.general.trigger.options.js.php');

$widget = (new CWidget())
	->setTitle(_('Trigger displaying options'))
	->setControls((new CTag('nav', true,
		(new CForm())
			->cleanItems()
			->addItem((new CList())
				->addItem(makeAdministrationGeneralMenu('adm.triggerdisplayoptions.php'))
			)
		))
			->setAttribute('aria-label', _('Content controls'))
	);

$triggerDOFormList = (new CFormList())
	->addRow(_('Use custom event status colours'), (new CCheckBox('custom_color'))
		->setChecked($data['custom_color'] == EVENT_CUSTOM_COLOR_ENABLED)
		->setAttribute('autofocus', 'autofocus')
	)
	->addRow((new CLabel(_('Unacknowledged PROBLEM events'), 'problem_unack_color'))->setAsteriskMark(), [
		(new CColor('problem_unack_color', $data['problem_unack_color']))
			->setEnabled($data['custom_color'] == EVENT_CUSTOM_COLOR_ENABLED)
			->addClass(($data['custom_color'] == EVENT_CUSTOM_COLOR_DISABLED) ? TRX_STYLE_DISABLED : null)
			->setAriaRequired(),
		(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
		(new CCheckBox('problem_unack_style'))
			->setLabel(_('blinking'))
			->setChecked($data['problem_unack_style'] == 1)
	])
	->addRow((new CLabel(_('Acknowledged PROBLEM events'), 'problem_ack_color'))->setAsteriskMark(), [
		(new CColor('problem_ack_color', $data['problem_ack_color']))
			->setEnabled($data['custom_color'] == EVENT_CUSTOM_COLOR_ENABLED)
			->addClass(($data['custom_color'] == EVENT_CUSTOM_COLOR_DISABLED) ? TRX_STYLE_DISABLED : null)
			->setAriaRequired(),
		(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
		(new CCheckBox('problem_ack_style'))
			->setLabel(_('blinking'))
			->setChecked($data['problem_ack_style'] == 1)
	])
	->addRow((new CLabel(_('Unacknowledged RESOLVED events'), 'ok_unack_color'))->setAsteriskMark(), [
		(new CColor('ok_unack_color', $data['ok_unack_color']))
			->setEnabled($data['custom_color'] == EVENT_CUSTOM_COLOR_ENABLED)
			->addClass(($data['custom_color'] == EVENT_CUSTOM_COLOR_DISABLED) ? TRX_STYLE_DISABLED : null)
			->setAriaRequired(),
		(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
		(new CCheckBox('ok_unack_style'))
			->setLabel(_('blinking'))
			->setChecked($data['ok_unack_style'] == 1)
	])
	->addRow((new CLabel(_('Acknowledged RESOLVED events'), 'ok_ack_color'))->setAsteriskMark(), [
		(new CColor('ok_ack_color', $data['ok_ack_color']))
			->setEnabled($data['custom_color'] == EVENT_CUSTOM_COLOR_ENABLED)
			->addClass(($data['custom_color'] == EVENT_CUSTOM_COLOR_DISABLED) ? TRX_STYLE_DISABLED : null)
			->setAriaRequired(),
		(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
		(new CCheckBox('ok_ack_style'))
			->setLabel(_('blinking'))
			->setChecked($data['ok_ack_style'] == 1)
	])
	->addRow(null)
	->addRow((new CLabel(_('Display OK triggers for'), 'ok_period'))->setAsteriskMark(), [
		(new CTextBox('ok_period', $data['ok_period']))
			->setWidth(TRX_TEXTAREA_TINY_WIDTH)
			->setAriaRequired()
			->setAttribute('maxlength', '6')
	])
	->addRow((new CLabel(_('On status change triggers blink for'), 'blink_period'))->setAsteriskMark(), [
		(new CTextBox('blink_period', $data['blink_period']))
			->setWidth(TRX_TEXTAREA_TINY_WIDTH)
			->setAriaRequired()
			->setAttribute('maxlength', '6')
	]);

$severityForm = (new CForm())
	->setAttribute('aria-labeledby', TRX_STYLE_PAGE_TITLE)
	->addItem(
		(new CTabView())
			->addTab('triggerdo', _('Trigger displaying options'), $triggerDOFormList)
			->setFooter(makeFormFooter(
				new CSubmit('update', _('Update')),
				[new CButton('resetDefaults', _('Reset defaults'))]
			))
	);

$widget->addItem($severityForm);

return $widget;
