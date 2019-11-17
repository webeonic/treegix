<?php



class CDiv extends CTag {

	public function __construct($items = null) {
		parent::__construct('div', true);

		$this->addItem($items);
	}

	public function setWidth($value) {
		$this->addStyle('width: '.$value.'px;');

		return $this;
	}
}
