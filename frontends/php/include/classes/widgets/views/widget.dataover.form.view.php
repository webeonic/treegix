<?php



/**
 * Data overview widget form view.
 */
$fields = $data['dialogue']['fields'];

$form = CWidgetHelper::createForm();

$form_list = CWidgetHelper::createFormList($data['dialogue']['name'], $data['dialogue']['type'],
	$data['dialogue']['view_mode'], $data['known_widget_types'], $fields['rf_rate']
);

// Host groups.
$field_groupids = CWidgetHelper::getGroup($fields['groupids'], $data['captions']['ms']['groups']['groupids'],
	$form->getName()
);
$form_list->addRow(CWidgetHelper::getMultiselectLabel($fields['groupids']), $field_groupids);
$scripts = [$field_groupids->getPostJS()];

// Application.
$form_list->addRow(CWidgetHelper::getLabel($fields['application']),
	CWidgetHelper::getApplicationSelector($fields['application'])
);

// Show suppressed problems.
$form_list->addRow(CWidgetHelper::getLabel($fields['show_suppressed']),
	CWidgetHelper::getCheckBox($fields['show_suppressed'])
);

// Hosts location.
$form_list->addRow(CWidgetHelper::getLabel($fields['style']), CWidgetHelper::getRadioButtonList($fields['style']));

$form->addItem($form_list);

return [
	'form' => $form,
	'scripts' => $scripts
];
