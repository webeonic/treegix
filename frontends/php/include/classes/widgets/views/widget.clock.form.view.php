<?php



/**
 * Clock widget form view.
 */
$fields = $data['dialogue']['fields'];

$form = CWidgetHelper::createForm();

$form_list = CWidgetHelper::createFormList($data['dialogue']['name'], $data['dialogue']['type'],
	$data['dialogue']['view_mode'], $data['known_widget_types'], $fields['rf_rate']
);

$scripts = [];

// Time type.
$form_list->addRow(CWidgetHelper::getLabel($fields['time_type']), CWidgetHelper::getComboBox($fields['time_type']));

// Item.
if (array_key_exists('itemid', $fields)) {
	$field_itemid = CWidgetHelper::getItem($fields['itemid'], $data['captions']['ms']['items']['itemid'],
		$form->getName()
	);
	$form_list->addRow(CWidgetHelper::getMultiselectLabel($fields['itemid']), $field_itemid);
	$scripts[] = $field_itemid->getPostJS();
}

$form->addItem($form_list);

return [
	'form' => $form,
	'scripts' => $scripts
];
