<?php



class CMultilineInput extends CDiv {
	/**
	 * Default CSS class name for HTML root element.
	 */
	const TRX_STYLE_CLASS = 'multilineinput-control';

	/**
	 * @var string
	 */
	private $name;

	/**
	 * @var string
	 */
	private $value;

	/**
	 * @var array
	 */
	private $options;

	/**
	 * @param string      $name
	 * @param string      $value
	 * @param array       $options
	 * @param string      $options['title']                 Modal dialog title.
	 * @param string      $options['hint']                  Hint message for input element.
	 * @param string      $options['placeholder']           Placeholder for empty value.
	 * @param string      $options['placeholder_textarea']  Placeholder for empty value for textarea.
	 * @param string      $options['label_before']          Label, placed before textarea (HTML allowed, default: '').
	 * @param string      $options['label_after']           Label, placed after textarea (HTML allowed, default: '').
	 * @param int         $options['maxlength']             Max characters length (default: 255).
	 * @param bool        $options['line_numbers']          Show line numbers (default: true).
	 * @param int         $options['rows']                  Textarea rows number for grow=fixed
	 *                                                      or rows limit for grow=auto if more then 0 (default: 20).
	 * @param bool        $options['grow']                  Textarea grow mode fixed|auto|stretch (default: 'fixed').
	 * @param bool        $options['monospace_font']        Monospace font type (default: true).
	 * @param bool        $options['readonly']              Readonly component (default: false).
	 * @param bool        $options['disabled']              Is disabled (default: false).
	 */
	public function __construct($name = 'multilineinput', $value = '', array $options = []) {
		parent::__construct();

		$this
			->addClass(self::TRX_STYLE_CLASS)
			->setId(trx_formatDomId($name))
			->setAttribute('data-name', $name);

		$this->name = $name;
		$this->value = $value;
		$this->options = $options;
	}

	/**
	 * @param string $value
	 *
	 * @return CMultilineInput
	 */
	public function setValue($value) {
		$this->value = $value;

		return $this;
	}

	/**
	 * @param string           $key
	 * @param string|int|bool  $value
	 *
	 * @return CMultilineInput
	 */
	public function setOption($key, $value) {
		$this->options[$key] = $value;

		return $this;
	}

	/**
	 * @return CMultilineInput
	 */
	public function setEnabled() {
		$this->options['disabled'] = false;

		return $this;
	}

	/**
	 * @return CMultilineInput
	 */
	public function setDisabled() {
		$this->options['disabled'] = true;

		return $this;
	}

	/**
	 * Get content of all Javascript code.
	 *
	 * @return string  Javascript code.
	 */
	public function getPostJS() {
		return 'jQuery("#'.$this->getId().'").multilineInput('.CJs::encodeJson([
			'value' => $this->value
		] + $this->options).');';
	}

	public function toString($destroy = true) {
		if (!array_key_exists('add_post_js', $this->options) || $this->options['add_post_js']) {
			trx_add_post_js($this->getPostJS());
		}

		return parent::toString($destroy);
	}
}
