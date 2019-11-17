<?php



class CSup extends CTag {

	public function __construct($items = null) {
		parent::__construct('sup', true);
		$this->addItem($items);

		return $this;
	}
}
