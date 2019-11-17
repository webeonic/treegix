<?php



class CSvgGraphLegend extends CDiv {

	/**
	 * @param array  $labels
	 * @param string $labels[]['name']
	 * @param string $labels[]['color']
	 */
	public function __construct(array $labels = []) {
		parent::__construct();

		foreach ($labels as $label) {
			// border-color is for legend element ::before pseudo element.
			parent::addItem((new CDiv($label['name']))->setAttribute('style', 'border-color: '.$label['color']));
		}

		switch (count($labels)) {
			case 1:
				$this->addClass(CSvgTag::ZBX_STYLE_GRAPH_LEGEND_SINGLE_ITEM);
				break;

			case 2:
				$this->addClass(CSvgTag::ZBX_STYLE_GRAPH_LEGEND_TWO_ITEMS);
				break;

			default:
				$this->addClass(CSvgTag::ZBX_STYLE_GRAPH_LEGEND);
				break;
		}
	}
}
