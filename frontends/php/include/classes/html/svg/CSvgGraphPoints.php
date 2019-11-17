<?php



class CSvgGraphPoints extends CSvgGroup {

	protected $path;
	protected $itemid;
	protected $item_name;
	protected $options;

	public function __construct($path, $metric) {
		parent::__construct();

		$this->path = $path ? : [];
		$this->itemid = $metric['itemid'];
		$this->item_name = $metric['name'];
		$this->options = $metric['options'] + [
			'color' => CSvgGraph::SVG_GRAPH_DEFAULT_COLOR,
			'pointsize' => CSvgGraph::SVG_GRAPH_DEFAULT_POINTSIZE,
			'transparency' => CSvgGraph::SVG_GRAPH_DEFAULT_TRANSPARENCY,
			'order' => 1
		];
	}

	public function makeStyles() {
		$this
			->addClass(CSvgTag::ZBX_STYLE_GRAPH_POINTS)
			->addClass(CSvgTag::ZBX_STYLE_GRAPH_POINTS.'-'.$this->itemid.'-'.$this->options['order']);

		return [
			'.'.CSvgTag::ZBX_STYLE_GRAPH_POINTS.'-'.$this->itemid.'-'.$this->options['order'] => [
				'fill-opacity' => $this->options['transparency'] * 0.1,
				'fill' => $this->options['color']
			]
		];
	}

	protected function draw() {
		foreach ($this->path as $point) {
			$this->addItem(
				(new CSvgCircle($point[0], $point[1], $this->options['pointsize']))->setAttribute('label', $point[2])
			);
		}
	}

	public function toString($destroy = true) {
		$this->setAttribute('data-set', 'points')
			->setAttribute('data-metric', CHtml::encode($this->item_name))
			->setAttribute('data-color', $this->options['color'])
			->addItem(
				(new CSvgCircle(-10, -10, $this->options['pointsize'] + 4))
					->addClass(CSvgTag::ZBX_STYLE_GRAPH_HIGHLIGHTED_VALUE)
			)
			->draw();

		return parent::toString($destroy);
	}
}
