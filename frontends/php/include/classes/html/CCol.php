<?php



class CCol extends CTag {

	protected $tag = 'td';

	public function __construct($item = null) {
		parent::__construct($this->tag, true);
		$this->addItem($item);
	}

	public function setRowSpan($value) {
		$this->setAttribute('rowspan', $value);

		return $this;
	}

	public function setColSpan($value) {
		$this->setAttribute('colspan', $value);

		return $this;
	}

	public function setWidth($value) {
		$this->setAttribute('width', $value);

		return $this;
	}
}
