<?php



class CWidgetFieldMsHost extends CWidgetFieldMs {

	/**
	 * Create widget field for Host selection
	 *
	 * @param string      $name     field name in form
	 * @param string      $label    label for the field in form
	 */
	public function __construct($name, $label) {
		parent::__construct($name, $label);

		$this->setSaveType(ZBX_WIDGET_FIELD_TYPE_HOST);
	}
}
