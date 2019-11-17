<?php



class CSvgGraphArea extends CSvgGraphLine {

	protected $y_zero;

	public function __construct($path, $metric, $y_zero = 0) {
		parent::__construct($path, $metric);

		$this->y_zero = $y_zero;
		$this->add_label = false;
		$this->options = $metric['options'] + [
			'fill' => 5
		];
	}

	public function makeStyles() {
		$this
			->addClass(CSvgTag::TRX_STYLE_GRAPH_AREA)
			->addClass(CSvgTag::TRX_STYLE_GRAPH_AREA.'-'.$this->itemid.'-'.$this->options['order']);

		return [
			'.'.CSvgTag::TRX_STYLE_GRAPH_AREA.'-'.$this->itemid.'-'.$this->options['order'] => [
				'fill-opacity' => $this->options['fill'] * 0.1,
				'fill' => $this->options['color'],
				'stroke-opacity' => 0.1,
				'stroke' => $this->options['color'],
				'stroke-width' => 2
			]
		];
	}

	protected function draw() {
		$path = parent::draw();

		if ($this->path) {
			$first_point = reset($this->path);
			$last_point = end($this->path);
			$this
				->lineTo($last_point[0], $this->y_zero)
				->lineTo($first_point[0], $this->y_zero)
				->closePath();
		}

		return $path;
	}
}
