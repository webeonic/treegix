<?php



/**
 * Widget Field for integer values.
 */
class CWidgetFieldIntegerBox extends CWidgetField {

	/**
	 * Allowed min value
	 *
	 * @var int
	 */
	private $min;

	/**
	 * Allowed max value
	 *
	 * @var int
	 */
	private $max;

	/**
	 * A numeric box widget field.
	 *
	 * @param string $name   field name in form
	 * @param string $label  label for the field in form
	 * @param int    $min    minimal allowed value (this included)
	 * @param int    $max    maximal allowed value (this included)
	 */
	public function __construct($name, $label, $min = 0, $max = TRX_MAX_INT32) {
		parent::__construct($name, $label);

		$this->setSaveType(TRX_WIDGET_FIELD_TYPE_INT32);
		$this->min = $min;
		$this->max = $max;
		$this->setExValidationRules(['in' => $this->min.':'.$this->max]);
	}

	public function getMaxLength() {
		return strlen((string) $this->max);
	}

	public function setValue($value) {
		return parent::setValue((int) $value);
	}
}
