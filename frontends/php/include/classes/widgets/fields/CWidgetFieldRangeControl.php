<?php



/**
 * Widget Field for numeric box
 */
class CWidgetFieldRangeControl extends CWidgetField {

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
	 * Step value
	 *
	 * @var int
	 */
	private $step;

	/**
	 * A numeric box widget field.
	 *
	 * @param string $name   field name in form
	 * @param string $label  label for the field in form
	 * @param int    $min    minimal allowed value (this included)
	 * @param int    $max    maximal allowed value (this included)
	 * @param int    $step   step value
	 */
	public function __construct($name, $label, $min = 0, $max = TRX_MAX_INT32, $step = 1) {
		parent::__construct($name, $label);

		$this->setSaveType(TRX_WIDGET_FIELD_TYPE_INT32);
		$this->min = $min;
		$this->max = $max;
		$this->step = $step;
		$this->setExValidationRules(['in' => $this->min.':'.$this->max]);
	}

	public function setValue($value) {
		$this->value = (int) $value;
		return $this;
	}

	public function getMin() {
		return $this->min;
	}

	public function getMax() {
		return $this->max;
	}

	public function getStep() {
		return $this->step;
	}
}
