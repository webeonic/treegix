<?php



/**
 * Problems by severity widget form view.
 */
$fields = $data['dialogue']['fields'];

$form = CWidgetHelper::createForm();

$form_list = CWidgetHelper::createFormList($data['dialogue']['name'], $data['dialogue']['type'],
	$data['dialogue']['view_mode'], $data['known_widget_types'], $fields['rf_rate']
);

$scripts = [];

// Host groups.
$field_groupids = CWidgetHelper::getGroup($fields['groupids'],
	$data['captions']['ms']['groups']['groupids'],
	$form->getName()
);
$form_list->addRow(CWidgetHelper::getMultiselectLabel($fields['groupids']), $field_groupids);
$scripts[] = $field_groupids->getPostJS();

// Exclude host groups.
$field_exclude_groupids = CWidgetHelper::getGroup($fields['exclude_groupids'],
	$data['captions']['ms']['groups']['exclude_groupids'],
	$form->getName()
);
$form_list->addRow(CWidgetHelper::getMultiselectLabel($fields['exclude_groupids']), $field_exclude_groupids);
$scripts[] = $field_exclude_groupids->getPostJS();

// Hosts.
$field_hostids = CWidgetHelper::getHost($fields['hostids'],
	$data['captions']['ms']['hosts']['hostids'],
	$form->getName()
);
$form_list->addRow(CWidgetHelper::getMultiselectLabel($fields['hostids']), $field_hostids);
$scripts[] = $field_hostids->getPostJS();

// Problem.
$form_list->addRow(CWidgetHelper::getLabel($fields['problem']), CWidgetHelper::getTextBox($fields['problem']));

// Severity.
$form_list->addRow(
	CWidgetHelper::getLabel($fields['severities']),
	CWidgetHelper::getSeverities($fields['severities'], $data['config'])
);

// Show type.
$form_list->addRow(CWidgetHelper::getLabel($fields['show_type']), CWidgetHelper::getRadioButtonList($fields['show_type']));

// Layout.
$form_list->addRow(CWidgetHelper::getLabel($fields['layout']), CWidgetHelper::getRadioButtonList($fields['layout']));

// Show operational data.
$form_list->addRow(CWidgetHelper::getLabel($fields['show_opdata']), CWidgetHelper::getRadioButtonList($fields['show_opdata']));

// Show suppressed problems.
$form_list->addRow(CWidgetHelper::getLabel($fields['show_suppressed']),
	CWidgetHelper::getCheckBox($fields['show_suppressed'])
);

// Hide groups without problems.
$form_list->addRow(
	CWidgetHelper::getLabel($fields['hide_empty_groups']),
	CWidgetHelper::getCheckBox($fields['hide_empty_groups'])
);

// Problem display.
$form_list->addRow(CWidgetHelper::getLabel($fields['ext_ack']), CWidgetHelper::getRadioButtonList($fields['ext_ack']));

// Show timeline.
$form_list->addRow(CWidgetHelper::getLabel($fields['show_timeline']),
	CWidgetHelper::getCheckBox($fields['show_timeline'])
);

$form->addItem($form_list);

return [
	'form' => $form,
	'scripts' => $scripts
];
