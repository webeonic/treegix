<?php



class CSvgRect extends CSvgTag {

	public function __construct($x, $y, $width, $height) {
		parent::__construct('rect', true);

		$this->setAttribute('x', $x);
		$this->setAttribute('y', $y);
		$this->setAttribute('width', $width);
		$this->setAttribute('height', $height);
	}
}
