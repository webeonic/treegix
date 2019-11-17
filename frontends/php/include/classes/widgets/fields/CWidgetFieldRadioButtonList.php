<?php



class CWidgetFieldRadioButtonList extends CWidgetField {

	private $values;
	private $modern = false;

	/**
	 * Radio button widget field. Can use both, string and integer type keys.
	 *
	 * @param string $name       field name in form
	 * @param string $label      label for the field in form
	 * @param array  $values     key/value pairs of radio button values. Key - saved in DB. Value - visible to user.
	 */
	public function __construct($name, $label, $values) {
		parent::__construct($name, $label);

		$this->setSaveType(ZBX_WIDGET_FIELD_TYPE_INT32);
		$this->values = $values;
		$this->setExValidationRules(['in' => implode(',', array_keys($this->values))]);
	}

	public function setValue($value) {
		return parent::setValue((int) $value);
	}

	public function setModern($modern) {
		$this->modern = $modern;

		return $this;
	}

	public function getModern() {
		return $this->modern;
	}

	public function getValues() {
		return $this->values;
	}
}
