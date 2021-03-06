<?php
 


class CObject {

	public $items;

	public function __construct($items = null) {
		$this->items = [];
		if (isset($items)) {
			$this->addItem($items);
		}
	}

	public function toString($destroy = true) {
		$res = implode('', $this->items);
		if ($destroy) {
			$this->destroy();
		}
		return $res;
	}

	public function __toString() {
		return $this->toString();
	}

	public function show($destroy = true) {
		echo $this->toString($destroy);
		return $this;
	}

	public function destroy() {
		$this->cleanItems();
		return $this;
	}

	public function cleanItems() {
		$this->items = [];
		return $this;
	}

	public function itemsCount() {
		return count($this->items);
	}

	public function addItem($value) {
		if (is_object($value)) {
			array_push($this->items, unpack_object($value));
		}
		elseif (is_string($value)) {
			array_push($this->items, $value);
		}
		elseif (is_array($value)) {
			foreach ($value as $item) {
				$this->addItem($item); // attention, recursion !!!
			}
		}
		elseif (!is_null($value)) {
			array_push($this->items, unpack_object($value));
		}
		return $this;
	}
}

function unpack_object(&$item) {
	$res = '';
	if (is_object($item)) {
		$res = $item->toString(false);
	}
	elseif (is_array($item)) {
		foreach ($item as $id => $dat) {
			$res .= unpack_object($item[$id]); // attention, recursion !!!
		}
	}
	elseif (!is_null($item)) {
		$res = strval($item);
		unset($item);
	}
	return $res;
}
