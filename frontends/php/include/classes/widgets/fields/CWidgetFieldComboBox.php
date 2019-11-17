<?php



class CWidgetFieldComboBox extends CWidgetField {

	private $values;

	/**
	 * Combo box widget field. Can use both, string and integer type keys.
	 *
	 * @param string $name    Field name in form
	 * @param string $label   Label for the field in form
	 * @param array  $values  Key/value pairs of combo box values. Key - saved in DB. Value - visible to user.
	 */
	public function __construct($name, $label, $values) {
		parent::__construct($name, $label);

		$this->setSaveType(TRX_WIDGET_FIELD_TYPE_INT32);
		$this->values = $values;
		$this->setExValidationRules(['in' => implode(',', array_keys($this->values))]);
	}

	public function setValue($value) {
		return parent::setValue((int) $value);
	}

	public function getValues() {
		return $this->values;
	}
}
