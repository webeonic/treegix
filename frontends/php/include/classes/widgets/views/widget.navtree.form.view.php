<?php



/**
 * Map navigation widget form view.
 */
$fields = $data['dialogue']['fields'];

$form = CWidgetHelper::createForm();

$form_list = CWidgetHelper::createFormList($data['dialogue']['name'], $data['dialogue']['type'],
	$data['dialogue']['view_mode'], $data['known_widget_types'], $fields['rf_rate']
);

$scripts = [];

// Map widget reference.
$field = $fields[CWidgetFieldReference::FIELD_NAME];
$form->addVar($field->getName(), $field->getValue());

if ($field->getValue() === '') {
	$scripts[] = $field->getJavascript('#'.$form->getAttribute('id'));
}

// Register dynamically created item fields. Only for map.name.#, map.parent.#, map.order.#, mapid.#
foreach ($fields as $field) {
	if ($field instanceof CWidgetFieldHidden) {
		$form->addVar($field->getName(), $field->getValue());
	}
}

// Show unavailable maps
$form_list->addRow(
	CWidgetHelper::getLabel($fields['show_unavailable']),
	CWidgetHelper::getCheckBox($fields['show_unavailable'])
);

$form->addItem($form_list);

return [
	'form' => $form,
	'scripts' => $scripts
];
