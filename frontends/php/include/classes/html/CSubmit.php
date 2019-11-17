<?php



class CSubmit extends CButton {

	public function __construct($name = 'submit', $caption = '') {
		parent::__construct($name, $caption);
		$this->setAttribute('type', 'submit');
		$this->setAttribute('value', $caption);
	}
}
