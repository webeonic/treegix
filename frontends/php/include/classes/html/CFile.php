<?php



class CFile extends CInput {

	public function __construct($name = 'file') {
		parent::__construct('file', $name);
	}

	public function setWidth($value) {
		$this->addStyle('width: '.$value.'px;');
		return $this;
	}
}
