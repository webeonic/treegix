<?php


$this->includeJSfile('app/views/monitoring.acknowledge.edit.js.php');

$form_list = (new CFormList())
	->addRow(
		new CLabel(_('Message'), 'message'),
		(new CTextArea('message', $data['message']))
			->setWidth(TRX_TEXTAREA_BIG_WIDTH)
			->setMaxLength(255)
			->setAttribute('autofocus', 'autofocus')
	);

if (array_key_exists('history', $data)) {
	$form_list->addRow(_('History'),
		(new CDiv(makeEventHistoryTable($data['history'], $data['users'], $data['config'])))
			->addClass(TRX_STYLE_TABLE_FORMS_SEPARATOR)
			->setAttribute('style', 'min-width: '.TRX_TEXTAREA_BIG_WIDTH.'px;')
	);
}

$selected_events = count($data['eventids']);

$form_list
	->addRow(_x('Scope', 'selected problems'),
		(new CDiv(
			(new CRadioButtonList('scope', $data['scope']))
				->makeVertical()
				->addValue([
					_n('Only selected problem', 'Only selected problems', $selected_events),
					$selected_events > 1 ? (new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN) : null,
					$selected_events > 1 ? new CSup(_n('%1$s event', '%1$s events', $selected_events)) : null
				], TRX_ACKNOWLEDGE_SELECTED)
				->addValue([
					_('Selected and all other problems of related triggers'),
					(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
					new CSup(_n('%1$s event', '%1$s events', $data['related_problems_count']))
				], TRX_ACKNOWLEDGE_PROBLEM)
		))
			->setAttribute('style', 'min-width: '.TRX_TEXTAREA_BIG_WIDTH.'px;')
			->addClass(TRX_STYLE_TABLE_FORMS_SEPARATOR)
	)
	->addRow(_('Change severity'),
		(new CList([
			(new CCheckBox('change_severity', TRX_PROBLEM_UPDATE_SEVERITY))
				->onClick('javascript: jQuery("#severity input").attr("disabled", this.checked ? false : true)')
				->setChecked($data['change_severity'])
				->setEnabled($data['problem_severity_can_be_changed']),
			(new CSeverity(['name' => 'severity', 'value' => $data['severity']], $data['change_severity']))
		]))
			->addClass('hor-list')
	)
	->addRow(_('Acknowledge'),
		(new CCheckBox('acknowledge_problem', TRX_PROBLEM_UPDATE_ACKNOWLEDGE))
			->setChecked($data['acknowledge_problem'])
			->setEnabled($data['problem_can_be_acknowledged'])
	)
	->addRow(_('Close problem'),
		(new CCheckBox('close_problem', TRX_PROBLEM_UPDATE_CLOSE))
			->setChecked($data['close_problem'])
			->setEnabled($data['problem_can_be_closed'])
	)
	->addRow('',
		(new CDiv((new CLabel(_('At least one update operation or message must exist.')))->setAsteriskMark()))
	);

$footer_buttons = makeFormFooter(
	new CSubmitButton(_('Update'), 'action', 'acknowledge.create'),
	[new CRedirectButton(_('Cancel'), $data['backurl'])]
);

(new CWidget())
	->setTitle(_('Update problem'))
	->addItem(
		(new CForm())
			->setId('acknowledge_form')
			->setAttribute('aria-labeledby', TRX_STYLE_PAGE_TITLE)
			->addVar('eventids', $data['eventids'])
			->addVar('backurl', $data['backurl'])
			->addItem(
				(new CTabView())
					->addTab('ackTab', null, $form_list)
					->setFooter($footer_buttons)
			)
	)
	->show();
