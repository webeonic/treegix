<?php



class CWidgetFieldCheckBoxList extends CWidgetField {

	const ORIENTATION_HORIZONTAL = 0;
	const ORIENTATION_VERTICAL = 1;

	private $orientation;

	public function __construct($name, $label) {
		parent::__construct($name, $label);

		$this->setSaveType(ZBX_WIDGET_FIELD_TYPE_INT32);
		$this->setDefault([]);
		$this->setValidationRules(['type' => API_INTS32]);
		$this->orientation = self::ORIENTATION_VERTICAL;
	}

	public function setValue($value) {
		$this->value = (array) $value;

		return $this;
	}

	public function setOrientation($orientation) {
		$this->orientation = $orientation;

		return $this;
	}

	public function getOrientation() {
		return $this->orientation;
	}
}
