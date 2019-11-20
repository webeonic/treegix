<?php



class CNavigationTree extends CDiv {
	private $error;
	private $data;

	public function __construct(array $data = []) {
		parent::__construct();

		$this->data = $data;
		$this->error = null;

		$this->setId(uniqid());
		$this->addClass(TRX_STYLE_NAVIGATIONTREE);
	}

	public function setError($value) {
		$this->error = $value;
		return $this;
	}

	public function getScriptRun() {
		return ($this->error === null)
			? 'jQuery(function($) {'.
				'$("#'.$this->getId().'").trx_navtree({'.
					'problems: '.CJs::encodeJson($this->data['problems']).','.
					'severity_levels: '.CJs::encodeJson($this->data['severity_config']).','.
					'navtree_items_opened: "'.implode(',', $this->data['navtree_items_opened']).'",'.
					'navtree_item_selected: '.intval($this->data['navtree_item_selected']).','.
					'maps_accessible: '.CJs::encodeJson(array_map('strval', $this->data['maps_accessible'])).','.
					'show_unavailable: '.$this->data['show_unavailable'].','.
					'initial_load: '.$this->data['initial_load'].','.
					'uniqueid: "'.$this->data['uniqueid'].'",'.
					'max_depth: '.WIDGET_NAVIGATION_TREE_MAX_DEPTH.
				'});'.
			'});'
			: '';
	}

	private function build() {
		if ($this->error !== null) {
			$span->addClass(TRX_STYLE_DISABLED);
		}

		$this->addItem((new CDiv())->addClass('tree'));
	}

	public function toString($destroy = true) {
		$this->build();

		return parent::toString($destroy);
	}
}
