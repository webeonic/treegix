<?php



/**
 * Action log widget form view.
 */
$fields = $data['dialogue']['fields'];

$form = CWidgetHelper::createForm();

$form_list = CWidgetHelper::createFormList($data['dialogue']['name'], $data['dialogue']['type'],
	$data['dialogue']['view_mode'], $data['known_widget_types'], $fields['rf_rate']
);

// Sort entries by.
$form_list->addRow(CWidgetHelper::getLabel($fields['sort_triggers']),
	CWidgetHelper::getComboBox($fields['sort_triggers'])
);

// Show lines.
$form_list->addRow(CWidgetHelper::getLabel($fields['show_lines']), CWidgetHelper::getIntegerBox($fields['show_lines']));

$form->addItem($form_list);

return [
	'form' => $form
];
