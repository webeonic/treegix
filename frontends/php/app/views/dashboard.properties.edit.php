<?php



$form = (new CForm())
	->cleanItems()
	->setName('dashboard_properties_form');

$multiselect = (new CMultiSelect([
	'name' => 'userid',
	'object_name' => 'users',
	'data' => [$data['dashboard']['owner']],
	'disabled' => in_array(CWebUser::getType(), [USER_TYPE_TREEGIX_USER, USER_TYPE_TREEGIX_ADMIN]),
	'multiple' => false,
	'popup' => [
		'parameters' => [
			'srctbl' => 'users',
			'srcfld1' => 'userid',
			'srcfld2' => 'fullname',
			'dstfrm' => $form->getName(),
			'dstfld1' => 'userid'
		]
	]
]))
	->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
	->setAriaRequired();

$form
	->addItem(getMessages())
	->addItem(new CInput('hidden', 'dashboardid', $data['dashboard']['dashboardid']))
	->addItem((new CFormList())
		->addRow((new CLabel(_('Owner'), 'userid_ms'))->setAsteriskMark(), $multiselect)
		->addRow((new CLabel(_('Name'), 'name'))->setAsteriskMark(),
			(new CTextBox('name', $data['dashboard']['name'], false, DB::getFieldLength('dashboard', 'name')))
				->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
				->setAriaRequired()
				->setAttribute('autofocus', 'autofocus')
		)
		->addItem((new CInput('submit', 'submit'))->addStyle('display: none;'))
	);

$output = [
	'header' => _('Dashboard properties'),
	'script_inline' => $multiselect->getPostJS(),
	'body' => $form->toString(),
	'buttons' => [
		[
			'title' => _('Apply'),
			'keepOpen' => true,
			'isSubmit' => true,
			'action' => 'return dashbrdApplyProperties();'
		]
	]
];

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
