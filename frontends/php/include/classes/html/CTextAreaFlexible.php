<?php
 


class CTextAreaFlexible extends CTextArea {
	/**
	 * Default CSS class name for textarea element.
	 */
	const ZBX_STYLE_CLASS = 'textarea-flexible';

	/**
	 * An options array.
	 *
	 * @var array
	 */
	protected $options = [
		'add_post_js' => true,
		'maxlength' => 255,
		'readonly' => false,
		'rows' => 1
	];

	/**
	 * CTextAreaFlexible constructor.
	 *
	 * @param string $name
	 * @param string $value                   (optional)
	 * @param array  $options                 (optional)
	 * @param bool   $options['add_post_js']  (optional)
	 * @param int    $options['maxlength']    (optional)
	 * @param bool   $options['readonly']     (optional)
	 * @param int    $options['rows']         (optional)
	 */
	public function __construct($name, $value = '', array $options = []) {
		$this->options = array_merge($this->options, $options);

		parent::__construct($name, $value, $this->options);

		$this->addClass(self::ZBX_STYLE_CLASS);

		if ($this->options['add_post_js']) {
			zbx_add_post_js($this->getPostJS());
		}
	}

	/**
	 * Sets textarea maxlength.
	 *
	 * @param int $maxlength
	 *
	 * @return $this
	 */
	public function setMaxlength($maxlength) {
		$this->setAttribute('maxlength', $maxlength);

		return $this;
	}

	/**
	 * Get content of all Javascript code.
	 *
	 * @return string  Javascript code.
	 */
	public function getPostJS() {
		return 'jQuery("#'.$this->getId().'").textareaFlexible();';
	}
}
