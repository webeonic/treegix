<?php



class CComboItem extends CTag {

	public function __construct($value, $caption = null, $selected = false, $enabled = null) {
		parent::__construct('option', true);
		$this->setAttribute('value', $value);
		$this->addItem($caption);
		$this->setSelected($selected);

		if ($enabled !== null) {
			$this->setEnabled($enabled);
		}
	}

	public function setValue($value) {
		$this->attributes['value'] = $value;
		return $this;
	}

	public function getValue() {
		return $this->getAttribute('value');
	}

	/**
	 * Set option as selected.
	 *
	 * @param bool $value
	 *
	 * @return CComboItem
	 */
	public function setSelected($value) {
		if ($value) {
			$this->attributes['selected'] = 'selected';
		}
		else {
			$this->removeAttribute('selected');
		}

		return $this;
	}

	/**
	 * Enable or disable the element.
	 *
	 * @param bool $value
	 *
	 * @return CComboItem
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
}
