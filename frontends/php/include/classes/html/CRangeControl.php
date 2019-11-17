<?php



/**
 * Class for range control creation.
 */
class CRangeControl extends CTextBox {
	/**
	 * Default CSS class name for HTML root element.
	 */
	const TRX_STYLE_CLASS = 'range-control';

	/**
	 * Options array for javascript initialization class.crangecontrol.js plugin.
	 */
	private $options = [
		'min' => 0,
		'max' => TRX_MAX_INT32,
		'step' => 1,
		'width' => TRX_TEXTAREA_SMALL_WIDTH
	];

	public function __construct($name, $value = '') {
		parent::__construct($name);

		$this->setValue($value);
		$this->addClass(static::TRX_STYLE_CLASS);
		return $this;
	}

	public function setValue($value) {
		$this->setAttribute('value', $value);
		return $this;
	}

	public function setMin($value) {
		$this->options['min'] = $value;
		return $this;
	}

	public function setMax($value) {
		$this->options['max'] = $value;
		return $this;
	}

	public function setStep($value) {
		$this->options['step'] = $value;
		return $this;
	}

	public function setWidth($value) {
		$this->options['width'] = $value;
		return $this;
	}

	public function getPostJS() {
		return 'jQuery("[name=\''.$this->getName().'\']").rangeControl();';
	}

	public function toString($destroy = true) {
		// Set options for jQuery rangeControl class.
		$this->setAttribute('data-options', CJs::encodeJson($this->options));
		$this->setAttribute('maxlength', max(strlen($this->options['min']), strlen($this->options['max'])));

		return parent::toString($destroy);
	}
}
