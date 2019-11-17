<?php



class CSvgGraphGrid extends CSvgTag {

	protected $points_value = [];
	protected $points_time = [];

	protected $position_x = 0;
	protected $position_y = 0;

	protected $color;

	public function __construct(array $points_value, array $points_time) {
		parent::__construct('g', true);

		$this
			->setAttribute('shape-rendering', 'crispEdges')
			->addClass(CSvgTag::ZBX_STYLE_GRAPH_GRID);

		$this->points_value = $points_value;
		$this->points_time = $points_time;
	}

	public function makeStyles() {
		return [
			'.'.CSvgTag::ZBX_STYLE_GRAPH_GRID.' path' => [
				'stroke-dasharray' => '2,2',
				'stroke' => $this->color
			]
		];
	}

	public function setPosition($x, $y) {
		$this->position_x = $x;
		$this->position_y = $y;

		return $this;
	}

	/**
	 * Set color.
	 *
	 * @param string $color  Color value.
	 *
	 * @return CSvgGraphGrid
	 */
	public function setColor($color) {
		$this->color = $color;

		return $this;
	}

	public function toString($destroy = true) {
		parent::addItem($this->draw());

		return parent::toString($destroy);
	}

	protected function draw() {
		$path = (new CSvgPath());

		foreach ($this->points_time as $pos => $time) {
			if (($this->position_x + $pos) <= ($this->position_x + $this->width)) {
				$path
					->moveTo($this->position_x + $pos, $this->position_y)
					->lineTo($this->position_x + $pos, $this->position_y + $this->height);
			}
		}

		foreach ($this->points_value as $pos => $value) {
			if (($this->position_y + $this->height - $pos) <= ($this->position_y + $this->height)) {
				$path
					->moveTo($this->position_x, $this->position_y + $this->height - $pos)
					->lineTo($this->position_x + $this->width, $this->position_y + $this->height - $pos);
			}
		}

		return $path;
	}
}
