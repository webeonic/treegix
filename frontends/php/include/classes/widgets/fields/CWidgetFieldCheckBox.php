<?php


class CWidgetFieldCheckBox extends CWidgetField {

	private $caption;

	/**
	 * Check box widget field.
	 *
	 * @param string $name     Field name in form.
	 * @param string $label    Label for the field in form.
	 * @param string $caption  Text after checkbox.
	 */
	public function __construct($name, $label, $caption = null) {
		parent::__construct($name, $label);

		$this->setSaveType(ZBX_WIDGET_FIELD_TYPE_INT32);
		$this->setDefault(0);
		$this->caption = $caption;
	}

	public function setValue($value) {
		return parent::setValue((int) $value);
	}

	public function getCaption() {
		return $this->caption;
	}
}
