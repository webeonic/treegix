<?php



/**
 * Graph prototype widget form view.
 */
$fields = $data['dialogue']['fields'];

$form = CWidgetHelper::createForm();

$form_list = CWidgetHelper::createFormList($data['dialogue']['name'], $data['dialogue']['type'],
	$data['dialogue']['view_mode'], $data['known_widget_types'], $fields['rf_rate']
);

$scripts = [];

// Source.
$form_list->addRow(
	CWidgetHelper::getLabel($fields['source_type']),
	CWidgetHelper::getRadioButtonList($fields['source_type'])
);

// Graph prototype.
if (array_key_exists('graphid', $fields)) {
	$field_graphid = CWidgetHelper::getGraphPrototype($fields['graphid'],
		$data['captions']['ms']['graph_prototypes']['graphid'], $form->getName()
	);
	$form_list->addRow(CWidgetHelper::getMultiselectLabel($fields['graphid']), $field_graphid);
	$scripts[] = $field_graphid->getPostJS();
}

// Item prototype.
if (array_key_exists('itemid', $fields)) {
	$field_itemid = CWidgetHelper::getItemPrototype($fields['itemid'],
		$data['captions']['ms']['item_prototypes']['itemid'], $form->getName()
	);
	$form_list->addRow(CWidgetHelper::getMultiselectLabel($fields['itemid']), $field_itemid);
	$scripts[] = $field_itemid->getPostJS();
}

// Show legend.
$form_list->addRow(CWidgetHelper::getLabel($fields['show_legend']), CWidgetHelper::getCheckBox($fields['show_legend']));

// Dynamic item.
$form_list->addRow(CWidgetHelper::getLabel($fields['dynamic']), CWidgetHelper::getCheckBox($fields['dynamic']));

// Columns and Rows.
CWidgetHelper::addIteratorFields($form_list, $fields['columns'], $fields['rows']);

$form->addItem($form_list);

return [
	'form' => $form,
	'scripts' => $scripts
];
