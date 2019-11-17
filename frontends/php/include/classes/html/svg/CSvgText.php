<?php



class CSvgText extends CSvgTag {

	public function __construct($x, $y, $text) {
		parent::__construct('text', true);

		$this->setAttribute('x', $x);
		$this->setAttribute('y', $y);
		$this->addItem($text);
	}
}
