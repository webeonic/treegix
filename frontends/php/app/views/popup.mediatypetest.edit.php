<?php



$form_list = (new CFormList());

if ($data['type'] == MEDIA_TYPE_WEBHOOK) {
	$i = 0;

	foreach ($data['parameters'] as $parameter) {
		$fieldid = 'parameters['.$i.']';
		$form_list
			->addRow(new CLabel($parameter['name'], $fieldid.'[value]'), [
				new CVar($fieldid.'[name]', $parameter['name']),
				(new CTextBox($fieldid.'[value]', $parameter['value']))->setWidth(TRX_TEXTAREA_BIG_WIDTH)
			]);
		$i++;
	}

	if (!$i) {
		$form_list->addRow(_('Webhook does not have parameters.'));
	}
}
else {
	$form_list
		->addRow(
			(new CLabel(_('Send to'), 'sendto'))->setAsteriskMark(),
			(new CTextBox('sendto', $data['sendto'], false, 1024))
				->setWidth(TRX_TEXTAREA_BIG_WIDTH)
				->setAttribute('autofocus', 'autofocus')
				->setAriaRequired()
				->setEnabled($data['enabled'])
		)
		->addRow(
			new CLabel(_('Subject'), 'subject'),
			(new CTextBox('subject', $data['subject'], false, 1024))
				->setWidth(TRX_TEXTAREA_BIG_WIDTH)
				->setEnabled($data['enabled'])
		)
		->addRow(
			(new CLabel(_('Message'), 'message'))->setAsteriskMark($data['type'] != MEDIA_TYPE_EXEC),
			(new CTextArea('message', $data['message'], ['rows' => 10]))
				->setWidth(TRX_TEXTAREA_BIG_WIDTH)
				->setAriaRequired($data['type'] != MEDIA_TYPE_EXEC)
				->setEnabled($data['enabled'])
		);
}

$form = (new CForm())
	->cleanItems()
	->setName('mediatypetest_form')
	->addVar('action', 'popup.mediatypetest.send')
	->addVar('mediatypeid', $data['mediatypeid'])
	->addItem([
		$form_list,
		(new CInput('submit', 'submit'))->addStyle('display: none;')
	]);

$output = [
	'header' => $data['title'],
	'script_inline' => require 'app/views/popup.mediatypetest.edit.js.php',
	'body' => (new CDiv([$data['errors'], $form]))->toString(),
	'buttons' => [
		[
			'title' => _('Test'),
			'class' => 'submit-test-btn',
			'keepOpen' => true,
			'isSubmit' => true,
			'enabled' => $data['enabled'],
			'action' => 'mediatypeTestSend("'.$form->getName().'");'
		]
	]
];

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
