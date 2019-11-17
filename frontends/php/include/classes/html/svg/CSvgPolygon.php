<?php



class CSvgPolygon extends CSvgTag {

	public function __construct($points) {
		parent::__construct('polygon', true);

		$p = '';

		foreach ($points as $point) {
			$p .= ' '.$point[0].','.$point[1];
		}

		$this->setAttribute('points', ltrim($p));
	}
}
