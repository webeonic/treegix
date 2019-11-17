<?php



class CComboBox extends CTag {

	public $value;

	public function __construct($name = 'combobox', $value = null, $action = null, array $items = []) {
		parent::__construct('select', true);
		$this->setId(zbx_formatDomId($name));
		$this->setAttribute('name', $name);
		$this->value = $value;
		if ($action !== null) {
			$this->onChange($action);
		}
		$this->addItems($items);

		// Prevent Firefox remembering selected option on page refresh.
		$this->setAttribute('autocomplete', 'off');
	}

	public function setValue($value = null) {
		$this->value = $value;
		return $this;
	}

	public function addItems(array $items) {
		foreach ($items as $value => $caption) {
			$selected = (strcmp($value, $this->value) == 0);
			parent::addItem(new CComboItem($value, $caption, $selected));
		}
		return $this;
	}

	public function addItemsInGroup($label, $items) {
		$group = new COptGroup($label);
		foreach ($items as $value => $caption) {
			$selected = (strcmp($value, $this->value) == 0);
			$group->addItem(new CComboItem($value, $caption, $selected));
		}
		parent::addItem($group);
		return $this;
	}

	public function addItem($value, $caption = '', $selected = null, $enabled = true, $class = null) {
		if ($value instanceof CComboItem || $value instanceof COptGroup) {
			parent::addItem($value);
		}
		else {
			if (is_null($selected)) {
				$selected = false;
				if (is_array($this->value)) {
					if (str_in_array($value, $this->value)) {
						$selected = true;
					}
				}
				elseif (strcmp($value, $this->value) == 0) {
					$selected = true;
				}
			}
			else {
				$selected = true;
			}

			$citem = new CComboItem($value, $caption, $selected, $enabled);

			if ($class !== null) {
				$citem->addClass($class);
			}

			parent::addItem($citem);
		}
		return $this;
	}

	/**
	 * Enable or disable readonly mode for the element.
	 *
	 * @param bool $value
	 *
	 * @return CComboBox
	 */
	public function setReadonly($value) {
		if ($value) {
			$this->setAttribute('readonly', 'readonly');
		}
		else {
			$this->removeAttribute('readonly');
		}

		return $this;
	}

	/**
	 * Enable or disable the element.
	 *
	 * @param $value
	 */
	public function setEnabled($value) {
		if ($value) {
			$this->removeAttribute('disabled');
		}
		else {
			$this->setAttribute('disabled', 'disabled');
		}
		return $this;
	}

	/**
	 * Set with of the combo box.
	 *
	 * @param int $value  Width in pixels of the element.
	 *
	 * @return CComboBox
	 */
	public function setWidth($value) {
		$this->addStyle('width: '.$value.'px;');

		return $this;
	}
}

class COptGroup extends CTag {

	public function __construct($label) {
		parent::__construct('optgroup', true);
		$this->setAttribute('label', $label);
	}
}
