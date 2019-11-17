<?php
 


class CAreaMap extends CTag {

	public function __construct($name = '') {
		parent::__construct('map', true);
		$this->setName($name);
	}

	public function addRectArea($x1, $y1, $x2, $y2, $href, $alt) {
		$this->addArea([$x1, $y1, $x2, $y2], $href, $alt, 'rect');
		return $this;
	}

	public function addArea($coords, $href, $alt, $shape) {
		$this->addItem(new CArea($coords, $href, $alt, $shape));
		return $this;
	}

	public function addItem($value) {
		if (is_object($value) && strtolower(get_class($value)) !== 'carea') {
			return $this->error('Incorrect value for addItem "'.$value.'".');
		}

		parent::addItem($value);
		return $this;
	}
}
