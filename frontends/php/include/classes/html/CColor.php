<?php



class CColor extends CDiv {

	const MAX_LENGTH = 6;

	private $name;
	private $value;
	private $is_enabled = true;
	private $is_required = false;
	private $append_color_picker_js = true;

	/**
	 * Creates a color picker form element.
	 *
	 * @param string $name	Color picker field name.
	 * @param string $value Color value in HEX RGB format.
	 */
	public function __construct($name, $value) {
		parent::__construct();

		$this->name = $name;
		$this->value = $value;
	}

	/**
	 * Enable or disable the element.
	 *
	 * @param bool $is_enabled
	 *
	 * @return CColor
	 */
	public function setEnabled($is_enabled = true) {
		$this->is_enabled = $is_enabled;

		return $this;
	}

	/**
	 * Set or reset element 'aria-required' attribute.
	 *
	 * @param bool $is_required
	 *
	 * @return CColor
	 */
	public function setAriaRequired($is_required = true) {
		$this->is_required = $is_required;

		return $this;
	}

	/**
	 * Append color picker javascript.
	 *
	 * @param bool $append
	 *
	 * @return CColor
	 */
	public function appendColorPickerJs($append = true)
	{
		$this->append_color_picker_js = $append;

		return $this;
	}

	/**
	 * Gets string representation of widget HTML content.
	 *
	 * @param bool $destroy
	 *
	 * @return string
	 */
	public function toString($destroy = true) {
		$this->cleanItems();

		$this->addItem(
			(new CTextBox($this->name, $this->value))
				->setWidth(ZBX_TEXTAREA_COLOR_WIDTH)
				->setAttribute('maxlength', self::MAX_LENGTH)
				->setEnabled($this->is_enabled)
				->setAriaRequired($this->is_required)
		);

		$this->addClass(ZBX_STYLE_INPUT_COLOR_PICKER);

		$init_script = $this->append_color_picker_js ? get_js('jQuery("#'.$this->name.'").colorpicker()') : '';

		return parent::toString($destroy).$init_script;
	}
}
