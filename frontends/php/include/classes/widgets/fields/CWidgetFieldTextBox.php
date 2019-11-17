<?php



class CWidgetFieldTextBox extends CWidgetField {

	private $placeholder;
	private $width;

	/**
	 * Text box widget field.
	 *
	 * @param string $name  field name in form
	 * @param string $label  label for the field in form
	 */
	public function __construct($name, $label) {
		parent::__construct($name, $label);

		$this->setSaveType(ZBX_WIDGET_FIELD_TYPE_STR);
		$this->setDefault('');
		$this->placeholder = null;
		$this->width = ZBX_TEXTAREA_STANDARD_WIDTH;
	}

	/**
	 * Set additional flags, which can be used in configuration form.
	 *
	 * @param int $flags
	 *
	 * @return $this
	 */
	public function setFlags($flags) {
		parent::setFlags($flags);

		if ($flags & self::FLAG_NOT_EMPTY) {
			$strict_validation_rules = $this->getValidationRules();
			self::setValidationRuleFlag($strict_validation_rules, API_NOT_EMPTY);
			$this->setStrictValidationRules($strict_validation_rules);
		}
		else {
			$this->setStrictValidationRules(null);
		}

		return $this;
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
