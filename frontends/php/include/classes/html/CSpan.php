<?php



class CSpan extends CTag {

	public function __construct($items = null) {
		parent::__construct('span', true);
		$this->addItem($items);
	}
}
