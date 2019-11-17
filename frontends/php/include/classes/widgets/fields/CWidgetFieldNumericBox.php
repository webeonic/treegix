<?php



/**
 * Widget Field for numeric data.
 */
class CWidgetFieldNumericBox extends CWidgetField {

	private $placeholder;
	private $width;

	/**
	 * A numeric box widget field.
	 * Supported signed decimal values with suffix (KMGTsmhdw).
	 *
	 * @param string $name   field name in form
	 * @param string $label  label for the field in form
	 */
	public function __construct($name, $label) {
		parent::__construct($name, $label);

		$this->setSaveType(ZBX_WIDGET_FIELD_TYPE_STR);
		$this->setValidationRules(['type' => API_NUMERIC, 'length' => 255]);
		$this->setDefault('');
	}

	public function getMaxLength() {
		return strlen((string) $this->max);
	}

	public function setPlaceholder($placeholder) {
		$this->placeholder = $placeholder;

		return $this;
	}

	public function getPlaceholder() {
		return $this->placeholder;
	}

	public function setWidth($width) {
		$this->width = $width;

		return $this;
	}

	public function getWidth() {
		return $this->width;
	}
}
