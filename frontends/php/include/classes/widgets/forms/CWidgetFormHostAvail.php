<?php
 


/**
 * Host availability widget form.
 */
class CWidgetFormHostAvail extends CWidgetForm {

	public function __construct($data) {
		parent::__construct($data, WIDGET_HOST_AVAIL);

		// Host groups.
		$field_groups = new CWidgetFieldMsGroup('groupids', _('Host groups'));

		if (array_key_exists('groupids', $this->data)) {
			$field_groups->setValue($this->data['groupids']);
		}
		$this->fields[$field_groups->getName()] = $field_groups;

		// Interface type.
		$field_interface_type = new CWidgetFieldCheckBoxList('interface_type', _('Interface type'));

		if (array_key_exists('interface_type', $this->data)) {
			$field_interface_type->setValue($this->data['interface_type']);
		}

		$this->fields[$field_interface_type->getName()] = $field_interface_type;

		// Layout.
		$field_layout = (new CWidgetFieldRadioButtonList('layout', _('Layout'), [
			STYLE_HORIZONTAL => _('Horizontal'),
			STYLE_VERTICAL => _('Vertical')
		]))
			->setDefault(STYLE_HORIZONTAL)
			->setModern(true);

		if (array_key_exists('layout', $this->data)) {
			$field_layout->setValue($this->data['layout']);
		}

		$this->fields[$field_layout->getName()] = $field_layout;

		// Show hosts in maintenance.
		$field_maintenance = (new CWidgetFieldCheckBox('maintenance', _('Show hosts in maintenance')))
			->setDefault(HOST_MAINTENANCE_STATUS_OFF);

		if (array_key_exists('maintenance', $this->data)) {
			$field_maintenance->setValue($this->data['maintenance']);
		}

		$this->fields[$field_maintenance->getName()] = $field_maintenance;
	}
}
