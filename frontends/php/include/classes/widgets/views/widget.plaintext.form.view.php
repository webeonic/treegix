<?php



/**
 * Plain text widget form view.
 */
$fields = $data['dialogue']['fields'];

$form = CWidgetHelper::createForm();

$form_list = CWidgetHelper::createFormList($data['dialogue']['name'], $data['dialogue']['type'],
	$data['dialogue']['view_mode'], $data['known_widget_types'], $fields['rf_rate']
);

// Items.
$field_itemids = CWidgetHelper::getItem($fields['itemids'], $data['captions']['ms']['items']['itemids'],
	$form->getName()
);
$form_list->addRow(CWidgetHelper::getMultiselectLabel($fields['itemids']), $field_itemids);
$scripts = [$field_itemids->getPostJS()];

// Items location.
$form_list->addRow(CWidgetHelper::getLabel($fields['style']), CWidgetHelper::getRadioButtonList($fields['style']));

// Show lines.
$form_list->addRow(CWidgetHelper::getLabel($fields['show_lines']), CWidgetHelper::getIntegerBox($fields['show_lines']));

// Show text as HTML.
$form_list->addRow(CWidgetHelper::getLabel($fields['show_as_html']),
	CWidgetHelper::getCheckBox($fields['show_as_html'])
);

// Dynamic items.
$form_list->addRow(CWidgetHelper::getLabel($fields['dynamic']), CWidgetHelper::getCheckBox($fields['dynamic']));

$form->addItem($form_list);

return [
	'form' => $form,
	'scripts' => $scripts
];
