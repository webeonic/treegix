<?php



include('include/views/js/administration.general.triggerSeverity.js.php');

$widget = (new CWidget())
	->setTitle(_('Trigger severities'))
	->setControls((new CTag('nav', true,
		(new CForm())
			->cleanItems()
			->addItem((new CList())
				->addItem(makeAdministrationGeneralMenu('adm.triggerseverities.php'))
			)
		))
			->setAttribute('aria-label', _('Content controls'))
	);

$severityTab = (new CFormList())
	->addRow((new CLabel(_('Not classified'), 'severity_name_0'))->setAsteriskMark(), [
		(new CTextBox('severity_name_0', $data['severity_name_0']))
			->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired()
			->setAttribute('maxlength', 32)
			->setAttribute('autofocus', 'autofocus'),
		(new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN),
		new CColor('severity_color_0', $data['severity_color_0'])
	])
	->addRow((new CLabel(_('Information'), 'severity_name_1'))->setAsteriskMark(), [
		(new CTextBox('severity_name_1', $data['severity_name_1']))
			->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired()
			->setAttribute('maxlength', 32),
		(new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN),
		new CColor('severity_color_1', $data['severity_color_1'])
	])
	->addRow((new CLabel(_('Warning'), 'severity_name_2'))->setAsteriskMark(), [
		(new CTextBox('severity_name_2', $data['severity_name_2']))
			->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired()
			->setAttribute('maxlength', 32),
		(new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN),
		new CColor('severity_color_2', $data['severity_color_2'])
	])
	->addRow((new CLabel(_('Average'), 'severity_name_3'))->setAsteriskMark(), [
		(new CTextBox('severity_name_3', $data['severity_name_3']))
			->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired()
			->setAttribute('maxlength', 32),
		(new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN),
		new CColor('severity_color_3', $data['severity_color_3'])
	])
	->addRow((new CLabel(_('High'), 'severity_name_4'))->setAsteriskMark(), [
		(new CTextBox('severity_name_4', $data['severity_name_4']))
			->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired()
			->setAttribute('maxlength', 32),
		(new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN),
		new CColor('severity_color_4', $data['severity_color_4'])
	])
	->addRow((new CLabel(_('Disaster'), 'severity_name_5'))->setAsteriskMark(), [
		(new CTextBox('severity_name_5', $data['severity_name_5']))
			->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired()
			->setAttribute('maxlength', 32),
		(new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN),
		new CColor('severity_color_5', $data['severity_color_5'])
	])
	->addRow(null)
	->addInfo(_('Custom severity names affect all locales and require manual translation!'));

$severityForm = (new CForm())
	->setAttribute('aria-labeledby', ZBX_STYLE_PAGE_TITLE)
	->addItem(
		(new CTabView())
			->addTab('severities', _('Trigger severities'), $severityTab)
			->setFooter(makeFormFooter(
				new CSubmit('update', _('Update')),
				[new CButton('resetDefaults', _('Reset defaults'))]
			))
	);

$widget->addItem($severityForm);

return $widget;
