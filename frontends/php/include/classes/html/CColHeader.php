<?php



class CColHeader extends CCol {

	public function __construct($item = null) {
		$this->tag = 'th';
		parent::__construct($item);
	}
}
