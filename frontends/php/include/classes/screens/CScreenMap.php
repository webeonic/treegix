<?php



class CScreenMap extends CScreenBase {

	/**
	 * Process screen.
	 *
	 * @return CDiv (screen inside container)
	 */
	public function get() {
		$map_options = [];

		if (array_key_exists('severity_min', $this->screenitem)) {
			$map_options['severity_min'] = $this->screenitem['severity_min'];
		}

		$map_data = CMapHelper::get($this->screenitem['resourceid'], $map_options);
		$map_data['container'] = '#map_'.$this->screenitem['screenitemid'];
		$this->insertFlickerfreeJs($map_data);

		$output = [
			(new CDiv())->setId('map_'.$this->screenitem['screenitemid'])
		];

		if ($this->mode == SCREEN_MODE_EDIT) {
			$output += [BR(), new CLink(_x('Change', 'verb'), $this->action)];
		}

		$div = (new CDiv($output))
			->addClass('flickerfreescreen')
			->setId($this->getScreenId())
			->setAttribute('data-timestamp', $this->timestamp);

		// Add map to additional wrapper to enable horizontal scrolling.
		$div = (new CDiv($div))->addClass('sysmap-scroll-container');

		return $div;
	}
}
