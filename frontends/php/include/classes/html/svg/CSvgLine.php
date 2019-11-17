<?php



class CSvgLine extends CSvgTag {

	public function __construct($x1, $y1, $x2, $y2) {
		parent::__construct('line', true);

		$this->setAttribute('x1', $x1);
		$this->setAttribute('y1', $y1);
		$this->setAttribute('x2', $x2);
		$this->setAttribute('y2', $y2);
	}
}
