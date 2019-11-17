<?php



class CSvgPath extends CSvgTag {

	protected $directions;
	protected $last_value = 0;

	public function __construct($directions = '') {
		parent::__construct('path');

		$this->setDirections($directions);
	}

	public function setDirections($directions) {
		$this->directions = $directions;
	}

	public function moveTo($x, $y) {
		$this->directions .= ' M'.floor($x).','.ceil($y);

		return $this;
	}

	public function lineTo($x, $y) {
		$this->directions .= ' L'.floor($x).','.ceil($y);

		return $this;
	}

	public function closePath() {
		$this->directions .= ' Z';

		return $this;
	}

	public function toString($destroy = true) {
		$this->setAttribute('d', trim($this->directions));

		return parent::toString($destroy);
	}
}
