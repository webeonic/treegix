<?php
 


class CSvgCircle extends CSvgTag {

	public function __construct($x, $y, $diameter) {
		parent::__construct('circle', true);

		$this->setAttribute('cx', round($x));
		$this->setAttribute('cy', round($y));
		$this->setAttribute('r', round($diameter / 2, 1));
	}
}
