<?php



class CPassBox extends CInput {

	public function __construct($name = 'password', $value = '', $maxlength = 255) {
		parent::__construct('password', $name, $value);
		$this->setAttribute('maxlength', $maxlength);
	}

	public function setWidth($value) {
		$this->addStyle('width: '.$value.'px;');
		return $this;
	}
}
