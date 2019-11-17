<?php



/**
 * URL widget form view.
 */
$fields = $data['dialogue']['fields'];

$form = CWidgetHelper::createForm();

$form_list = CWidgetHelper::createFormList($data['dialogue']['name'], $data['dialogue']['type'],
	$data['dialogue']['view_mode'], $data['known_widget_types'], $fields['rf_rate']
);

// URL.
$form_list->addRow(CWidgetHelper::getLabel($fields['url']), CWidgetHelper::getUrlBox($fields['url']));

// Dynamic item.
$form_list->addRow(CWidgetHelper::getLabel($fields['dynamic']), CWidgetHelper::getCheckBox($fields['dynamic']));

$form->addItem($form_list);

return [
	'form' => $form
];
