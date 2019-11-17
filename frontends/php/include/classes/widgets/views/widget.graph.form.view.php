<?php



/**
 * Graph widget form view.
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

// Graph.
if (array_key_exists('graphid', $fields)) {
	$field_graphid = CWidgetHelper::getGraph($fields['graphid'], $data['captions']['ms']['graphs']['graphid'],
		$form->getName()
	);
	$form_list->addRow(CWidgetHelper::getMultiselectLabel($fields['graphid']), $field_graphid);
	$scripts[] = $field_graphid->getPostJS();
}

// Item.
if (array_key_exists('itemid', $fields)) {
	$field_itemid = CWidgetHelper::getItem($fields['itemid'], $data['captions']['ms']['items']['itemid'],
		$form->getName()
	);
	$form_list->addRow(CWidgetHelper::getMultiselectLabel($fields['itemid']), $field_itemid);
	$scripts[] = $field_itemid->getPostJS();
}

// Show legend.
$form_list->addRow(CWidgetHelper::getLabel($fields['show_legend']), CWidgetHelper::getCheckBox($fields['show_legend']));

// Dynamic item.
$form_list->addRow(CWidgetHelper::getLabel($fields['dynamic']), CWidgetHelper::getCheckBox($fields['dynamic']));

$form->addItem($form_list);

return [
	'form' => $form,
	'scripts' => $scripts
];
