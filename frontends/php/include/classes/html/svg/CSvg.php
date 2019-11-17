<?php



class CSvg extends CSvgTag {

	public function __construct() {
		parent::__construct('svg', true);

		$this
			->setAttribute('id', str_replace('.', '', uniqid('svg_', true)))
			->setAttribute('version', '1.1')
			->setAttribute('xmlns', 'http://www.w3.org/2000/svg');
	}

	protected function startToString() {
		$styles = "\n";
		$scope = '#'.$this->getAttribute('id').' ';

		foreach ($this->styles as $selector => $properties) {
			if ($properties) {
				$styles .= $scope.$selector.'{';
				foreach ($properties as $property => $value) {
					$styles .= $property.':'.$value.';';
				}
				$styles .= '}'."\n";
			}
		}

		$styles = (new CTag('style', true, $styles))->toString();

		return parent::startToString().$styles;
	}

	/**
	 * Set SVG element width and height.
	 *
	 * @param int $width
	 * @param int $height
	 */
	public function setSize($width, $height) {
		$this->setAttribute('width', $width.'px');
		$this->setAttribute('height', $height.'px');

		return parent::setSize($width, $height);
	}
}
