<?php



class CListBox extends CComboBox {

	public function __construct($name = 'listbox', $value = null, $size = 5, $action = null, array $items = []) {
		parent::__construct($name, $value, $action, $items);
		$this->setAttribute('multiple', 'multiple');
		$this->setAttribute('size', $size);
	}
}
