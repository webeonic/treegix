<?php



class CJsScript extends CObject {

	public function __construct($item = null) {
		$this->items = [];
		$this->addItem($item);
	}

	public function addItem($value) {
		if (is_array($value)) {
			foreach ($value as $item) {
				array_push($this->items, unpack_object($item));
			}
		}
		elseif (!is_null($value)) {
			array_push($this->items, unpack_object($value));
		}
		return $this;
	}
}
