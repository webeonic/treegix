<?php



/**
 * Host availability widget form view.
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

// Interface type.
$form_list->addRow(
	CWidgetHelper::getLabel($fields['interface_type']),
	CWidgetHelper::getCheckBoxList($fields['interface_type'], [
		INTERFACE_TYPE_AGENT => _('Treegix agent'),
		INTERFACE_TYPE_SNMP => _('SNMP'),
		INTERFACE_TYPE_JMX => _('JMX'),
		INTERFACE_TYPE_IPMI => _('IPMI')
	])
);

// Layout.
$form_list->addRow(CWidgetHelper::getLabel($fields['layout']), CWidgetHelper::getRadioButtonList($fields['layout']));

// Show hosts in maintenance.
$form_list->addRow(CWidgetHelper::getLabel($fields['maintenance']), CWidgetHelper::getCheckBox($fields['maintenance']));

$form->addItem($form_list);

return [
	'form' => $form,
	'scripts' => $scripts
];
