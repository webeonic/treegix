<?php



$allowed_testing = $data['allowed_testing'];
$test = $data['test'];

$data_table = (new CTable())
	->addStyle('width: 100%;')
	->setHeader([
		_('Expression Variable Elements'),
		_('Result type'),
		_('Value')
	]);

foreach ($data['data_table_rows'] as $row) {
	$data_table->addRow($row);
}

$form_list = (new CFormList())
	->addRow(_('Test data'),
		(new CDiv($data_table))
			->addClass(TRX_STYLE_TABLE_FORMS_SEPARATOR)
			->addStyle('min-width: '.TRX_TEXTAREA_BIG_WIDTH.'px;')
	);

$result_table = (new CTable())
	->addStyle('width: 100%;')
	->setHeader([
		_('Expression'),
		_('Result')
	]);

foreach ($data['eHTMLTree'] as $e) {
	$result = '';
	$style = null;

	if ($allowed_testing && $test && array_key_exists('expression', $e)) {
		if (evalExpressionData($e['expression']['value'], $data['macros_data'])) {
			$result = 'TRUE';
			$style = TRX_STYLE_GREEN;
		}
		else {
			$result = 'FALSE';
			$style = TRX_STYLE_RED;
		}
	}

	$result_table->addRow([
		(new CCol($e['list']))
			->addClass(TRX_STYLE_OVERFLOW_ELLIPSIS)
			->addStyle('max-width: '.TRX_TEXTAREA_BIG_WIDTH.'px;'),
		(new CCol($result))->addClass($style)
	]);
}

$result = '';
if ($allowed_testing && $test) {
	if (evalExpressionData($data['expression'], $data['macros_data'])) {
		$result = 'TRUE';
		$style = TRX_STYLE_GREEN;
	}
	else {
		$result = 'FALSE';
		$style = TRX_STYLE_RED;
	}
}

$result_table->setFooter([
	(new CCol($data['outline']))
		->setAttribute('title', $data['outline'])
		->addClass(TRX_STYLE_OVERFLOW_ELLIPSIS)
		->addStyle('max-width: '.TRX_TEXTAREA_BIG_WIDTH.'px;'),
	(new CCol($result))->addClass($style)
]);

$form_list->addRow(_('Result'),
	(new CDiv($result_table))
		->addClass(TRX_STYLE_TABLE_FORMS_SEPARATOR)
		->addStyle('min-width: '.TRX_TEXTAREA_BIG_WIDTH.'px;')
);

$output = [
	'header' => $data['title'],
	'body' => (new CDiv([
		$data['message'],
		(new CForm())
			->cleanItems()
			->setId('expression_testing_from')
			->addItem((new CVar('expression', $data['expression']))->removeId())
			->addItem((new CVar('test_expression', 1))->removeId())
			->addItem([
				$form_list,
				(new CInput('submit', 'submit'))->addStyle('display: none;')
			])
		]))->toString(),
	'buttons' => [
		[
			'title' => _('Test'),
			'enabled' => $allowed_testing,
			'class' => '',
			'keepOpen' => true,
			'isSubmit' => true,
			'action' => 'return reloadPopup(document.forms["expression_testing_from"], "popup.testtriggerexpr");'
		]
	]
];

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
