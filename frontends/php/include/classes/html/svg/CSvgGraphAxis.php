<?php



/**
 * SVG graphs axis class.
 */
class CSvgGraphAxis extends CSvgTag {

	/**
	 * CSS class name for axis container.
	 *
	 * @var array
	 */
	private $css_class;

	/**
	 * Axis type. One of CSvgGraphAxis::AXIS_* constants.
	 *
	 * @var int
	 */
	private $type;

	/**
	 * Array of labels. Key is coordinate, value is text label.
	 *
	 * @var array
	 */
	private $labels;

	/**
	 * Axis triangle icon size.
	 *
	 * @var int
	 */
	const TRX_ARROW_SIZE = 5;
	const TRX_ARROW_OFFSET = 5;

	/**
	 * Color for labels.
	 *
	 * @var string
	 */
	private $text_color;

	/**
	 * Color for axis.
	 *
	 * @var string
	 */
	private $line_color;

	public function __construct(array $labels, $type) {
		$this->css_class = [
			GRAPH_YAXIS_SIDE_RIGHT => CSvgTag::TRX_STYLE_GRAPH_AXIS.' '.CSvgTag::TRX_STYLE_GRAPH_AXIS_RIGHT,
			GRAPH_YAXIS_SIDE_LEFT => CSvgTag::TRX_STYLE_GRAPH_AXIS.' '.CSvgTag::TRX_STYLE_GRAPH_AXIS_LEFT,
			GRAPH_YAXIS_SIDE_BOTTOM => CSvgTag::TRX_STYLE_GRAPH_AXIS.' '.CSvgTag::TRX_STYLE_GRAPH_AXIS_BOTTOM
		];

		$this->labels = $labels;
		$this->type = $type;
	}

	/**
	 * Return CSS style definitions for axis as array.
	 *
	 * @return array
	 */
	public function makeStyles() {
		return [
			'.'.CSvgTag::TRX_STYLE_GRAPH_AXIS.' path' => [
				'stroke' => $this->line_color,
				'fill' => 'transparent'
			],
			'.'.CSvgTag::TRX_STYLE_GRAPH_AXIS.' text' => [
				'fill' => $this->text_color,
				'font-size' => '11px',
				'alignment-baseline' => 'middle',
				'dominant-baseline' => 'middle'
			],
			'.'.CSvgTag::TRX_STYLE_GRAPH_AXIS_RIGHT.' text' => [
				'text-anchor' => 'start'
			],
			'.'.CSvgTag::TRX_STYLE_GRAPH_AXIS_LEFT.' text' => [
				'text-anchor' => 'end'
			],
			'.'.CSvgTag::TRX_STYLE_GRAPH_AXIS_BOTTOM.' text' => [
				'text-anchor' => 'middle'
			]
		];
	}

	/**
	 * Set text color.
	 *
	 * @param string $color  Color value.
	 *
	 * @return CSvgGraphAxis
	 */
	public function setTextColor($color) {
		$this->text_color = $color;

		return $this;
	}

	/**
	 * Set line color.
	 *
	 * @param string $color  Color value.
	 *
	 * @return CSvgGraphAxis
	 */
	public function setLineColor($color) {
		$this->line_color = $color;

		return $this;
	}

	/**
	 * Get axis line with arrow.
	 *
	 * @return CSvgPath
	 */
	private function getAxis() {
		$offset = ceil(self::TRX_ARROW_SIZE / 2);

		if ($this->type == GRAPH_YAXIS_SIDE_BOTTOM) {
			$x = $this->x + $this->width + self::TRX_ARROW_OFFSET;
			$y = $this->y;

			return [
				// Draw axis line.
				(new CSvgPath())
					->setAttribute('shape-rendering', 'crispEdges')
					->moveTo($this->x, $y)
					->lineTo($x, $y),
				// Draw arrow.
				(new CSvgPath())
					->moveTo($x + self::TRX_ARROW_SIZE, $y)
					->lineTo($x, $y - $offset)
					->lineTo($x, $y + $offset)
					->closePath()
			];
		}
		else {
			$x = ($this->type == GRAPH_YAXIS_SIDE_RIGHT) ? $this->x : $this->x + $this->width;
			$y = $this->y - self::TRX_ARROW_OFFSET;

			return [
				// Draw axis line.
				(new CSvgPath())
					->setAttribute('shape-rendering', 'crispEdges')
					->moveTo($x, $y)
					->lineTo($x, $this->height + $y + self::TRX_ARROW_OFFSET),
				// Draw arrow.
				(new CSvgPath())
					->moveTo($x, $y - self::TRX_ARROW_SIZE)
					->lineTo($x - $offset, $y)
					->lineTo($x + $offset, $y)
					->closePath()
			];
		}
	}

	/**
	 * Return array of initialized CSvgText objects for axis labels.
	 *
	 * @return array
	 */
	private function getLabels() {
		$x = 0;
		$y = 0;
		$labels = [];

		if ($this->type == GRAPH_YAXIS_SIDE_BOTTOM) {
			$axis = 'x';
			$y = $this->height - CSvgGraph::SVG_GRAPH_X_AXIS_LABEL_MARGIN;
		}
		else {
			$axis = 'y';
			$x = ($this->type == GRAPH_YAXIS_SIDE_RIGHT)
				? CSvgGraph::SVG_GRAPH_Y_AXIS_RIGHT_LABEL_MARGIN
				: $this->width - CSvgGraph::SVG_GRAPH_Y_AXIS_LEFT_LABEL_MARGIN;
		}

		foreach ($this->labels as $pos => $label) {
			$$axis = $pos;

			if ($this->type == GRAPH_YAXIS_SIDE_LEFT || $this->type == GRAPH_YAXIS_SIDE_RIGHT) {
				// Flip upside down.
				$y = $this->height - $y;
			}

			if ($this->type == GRAPH_YAXIS_SIDE_BOTTOM) {
				if (count($labels) == 0) {
					$text_tag_x = max($this->x + $x, strlen($label) * 4);
				}
				elseif (end($this->labels) === $label) {
					$text_tag_x = (strlen($label) * 4 > $this->width - $x) ? $this->x + $x - 10 : $this->x + $x;
				}
				else {
					$text_tag_x = $this->x + $x;
				}
			}
			else {
				$text_tag_x = $this->x + $x;
			}

			$labels[] = new CSvgText($text_tag_x, $this->y + $y, $label);
		}

		return $labels;
	}

	public function toString($destroy = true) {
		return (new CSvgGroup())
			->additem([
				$this->getAxis(),
				$this->getLabels()
			])
			->addClass($this->css_class[$this->type])
			->toString($destroy);
	}
}
