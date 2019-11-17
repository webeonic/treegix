<?php



class CNumericBox extends CInput {

	public function __construct($name = 'number', $value = '0', $maxlength = 20, $readonly = false, $allowempty = false, $allownegative = true) {
		parent::__construct('text', $name, $value);
		$this->setReadonly($readonly);
		$this->setAttribute('maxlength', $maxlength);
		$this->setAttribute('style', 'text-align: right;');
		$this->onChange('validateNumericBox(this, '.($allowempty ? 'true' : 'false').', '.($allownegative ? 'true' : 'false').');');
	}

	public function setWidth($value) {
		$this->addStyle('width: '.$value.'px;');
		return $this;
	}
}
