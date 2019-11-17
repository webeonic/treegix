<?php



class CPre extends CTag {

	public function __construct($items = null) {
		parent::__construct('pre', true);
		$this->addItem($items);
	}
}
