<?php



class CTextBox extends CInput {

	private $caption;

	public function __construct($name = 'textbox', $value = '', $readonly = false, $maxlength = 255) {
		parent::__construct('text', $name, $value);
		$this->setReadonly($readonly);
		$this->caption = null;
		$this->setAttribute('maxlength', $maxlength);
	}

	public function setWidth($value) {
		$this->addStyle('width: '.$value.'px;');
		return $this;
	}
}
