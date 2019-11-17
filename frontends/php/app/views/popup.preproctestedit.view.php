<?php


$form = (new CForm())
	->cleanItems()
	->addVar('action', 'popup.preproctest.send')
	->addVar('hostid', $data['hostid'])
	->addVar('value_type', $data['value_type'])
	->addVar('test_type', $data['test_type'])
	->addVar('show_final_result', $data['show_final_result'])
	->setId('preprocessing-test-form');

// Create macros table.
$macros_table = $data['macros'] ? (new CTable())->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_CONTAINER) : null;

$i = 0;
foreach ($data['macros'] as $macro_name => $macro_value) {
	$macros_table->addRow([
		(new CCol(
			(new CTextAreaFlexible('macro_rows['.$i++.']', $macro_name, ['readonly' => true]))
				->setWidth(TRX_TEXTAREA_MACRO_WIDTH)
				->removeAttribute('name')
				->removeId()
		))->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_PARENT),
		(new CCol('&rArr;'))->addStyle('vertical-align: top;'),
		(new CCol(
			(new CTextAreaFlexible('macros['.$macro_name.']', $macro_value))
				->setWidth(TRX_TEXTAREA_MACRO_VALUE_WIDTH)
				->setAttribute('placeholder', _('value'))
				->removeId()
		))->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_PARENT)
	]);
}

// Create results table.
$result_table = (new CTable())
	->setId('preprocessing-steps')
	->addClass('preprocessing-test-results')
	->addStyle('width: 100%;')
	->setHeader([
		'',
		(new CColHeader(_('Name')))->addStyle('width: 100%;'),
		(new CColHeader(_('Result')))->addClass(TRX_STYLE_RIGHT)
	]);

foreach ($data['steps'] as $i => $step) {
	$form
		->addVar('steps['.$i.'][type]', $step['type'])
		->addVar('steps['.$i.'][error_handler]', $step['error_handler'])
		->addVar('steps['.$i.'][error_handler_params]', $step['error_handler_params']);

	// Temporary solution to fix "\n\n1" conversion to "\n1" in the hidden textarea field after jQuery.append().
	if ($step['type'] == TRX_PREPROC_CSV_TO_JSON) {
		$form->addItem(new CInput('hidden', 'steps['.$i.'][params]', $step['params']));
	}
	else {
		$form->addVar('steps['.$i.'][params]', $step['params']);
	}

	$result_table->addRow([
		$step['num'].':',
		(new CCol($step['name']))->setId('preproc-test-step-'.$i.'-name'),
		(new CCol())
			->addClass(TRX_STYLE_RIGHT)
			->setId('preproc-test-step-'.$i.'-result')
	]);
}

$form_list = (new CFormList())
	->addRow(
		new CLabel(_('Value'), 'value'),
		(new CDiv([
			(new CMultilineInput('value', '', [
				'disabled' => false,
				'readonly' => false
			]))->setWidth(TRX_TEXTAREA_STANDARD_WIDTH),
			new CLabel(_('Time'), 'time'),
			(new CTextBox(null, 'now', true))
				->setWidth(TRX_TEXTAREA_SMALL_WIDTH)
				->setId('time')
		]))->addClass('preproc-test-popup-value-row')
	)
	->addRow(
		new CLabel(_('Previous value'), 'prev_item_value'),
		(new CDiv([
			(new CMultilineInput('prev_value', '', [
				'disabled' => !$data['show_prev']
			]))->setWidth(TRX_TEXTAREA_STANDARD_WIDTH),
			new CLabel(_('Prev. time'), 'prev_time'),
			(new CTextBox('prev_time', $data['prev_time']))
				->setEnabled($data['show_prev'])
				->setWidth(TRX_TEXTAREA_SMALL_WIDTH)
		]))->addClass('preproc-test-popup-value-row')
	)
	->addRow(
		new CLabel(_('End of line sequence'), 'eol'),
		(new CRadioButtonList('eol', $data['eol']))
			->addValue(_('LF'), TRX_EOL_LF)
			->addValue(_('CRLF'), TRX_EOL_CRLF)
			->setModern(true)
	);

if ($macros_table) {
	$form_list->addRow(
		_('Macros'),
		(new CDiv($macros_table))->addClass(TRX_STYLE_TABLE_FORMS_SEPARATOR)
	);
}

$form_list->addRow(
	_('Preprocessing steps'),
	(new CDiv($result_table))
		->addClass(TRX_STYLE_TABLE_FORMS_SEPARATOR)
		->addStyle('width: 100%;')
);

if ($data['show_final_result']) {
	$form_list->addRow(
		_('Result'),
		new CDiv(),
		'final-result'
	);
}

$form
	->addItem($form_list)
	->addItem((new CInput('submit', 'submit'))->addStyle('display: none;'));

$templates = [
	(new CTag('script', true))
		->setAttribute('type', 'text/x-jquery-tmpl')
		->setId('preprocessing-step-error-icon')
		->addItem(makeErrorIcon('#{error}')),
	(new CTag('script', true))
		->setAttribute('type', 'text/x-jquery-tmpl')
		->setId('preprocessing-gray-label')
		->addItem(
			(new CDiv('#{label}'))
				->addStyle('margin-top: 5px;')
				->addClass(TRX_STYLE_GREY)
		),
	(new CTag('script', true))
		->setAttribute('type', 'text/x-jquery-tmpl')
		->setId('preprocessing-step-result')
		->addItem(
			(new CDiv(
				(new CSpan('#{result}'))
					->addClass(TRX_STYLE_LINK_ACTION)
					->setHint('#{result}', 'hintbox-scrollable', true, 'max-width:'.TRX_ACTIONS_POPUP_MAX_WIDTH.'px;')
			))
				->addStyle('max-width: '.TRX_TEXTAREA_STANDARD_WIDTH.'px;')
				->addClass(TRX_STYLE_OVERFLOW_ELLIPSIS)
		),
	(new CTag('script', true))
		->setAttribute('type', 'text/x-jquery-tmpl')
		->setId('preprocessing-step-action-done')
		->addItem(
			(new CDiv([
				'#{action_name} ',
				(new CDiv(
					(new CSpan('#{failed}'))
						->addClass(TRX_STYLE_LINK_ACTION)
						->setHint('#{failed}', '', true, 'max-width:'.TRX_ACTIONS_POPUP_MAX_WIDTH.'px; ')
				))
					->addStyle('max-width: '.TRX_TEXTAREA_STANDARD_WIDTH.'px;')
					->addClass(TRX_STYLE_OVERFLOW_ELLIPSIS)
					->addClass(TRX_STYLE_REL_CONTAINER)
			]))
				->addStyle('margin-top: 1px;')
				->addClass(TRX_STYLE_GREY)
		)
];

$output = [
	'header' => $data['title'],
	'script_inline' => require 'app/views/popup.preproctestedit.view.js.php',
	'body' => (new CDiv([$form, $templates]))->toString(),
	'cancel_action' => 'return savePreprocessingTestInputs();',
	'buttons' => [
		[
			'title' => _('Test'),
			'class' => 'submit-test-btn',
			'keepOpen' => true,
			'isSubmit' => true,
			'action' => 'return itemPreprocessingTest("#'.$form->getId().'");'
		]
	]
];

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
