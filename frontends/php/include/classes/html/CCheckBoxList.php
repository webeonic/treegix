<?php



class CCheckBoxList extends CList {

	/**
	 * @var array $values
	 */
	protected $values;

	/**
	 * @var string $name
	 */
	protected $name;

	/**
	 * @param string $name
	 */
	public function __construct($name) {
		parent::__construct();

		$this->addClass(ZBX_STYLE_CHECKBOX_LIST);
		$this->name = $name;
		$this->values = [];
	}

	/**
	 * @param array $values
	 *
	 * @return CCheckBoxList
	 */
	public function setChecked(array $values) {
		$values = array_flip($values);

		foreach ($this->values as &$value) {
			$value['checked'] = array_key_exists($value['value'], $values);
		}
		unset($value);

		return $this;
	}

	/**
	 * @param array $values
	 *
	 * @return CCheckBoxList
	 */
	public function setOptions(array $values) {
		$this->values = [];

		foreach ($values as $value) {
			$this->values[] = $value + [
				'name' => '',
				'value' => null,
				'checked' => false
			];
		}

		return $this;
	}

	/*
	 * @param bool $destroy
	 *
	 * @return string
	 */
	public function toString($destroy = true) {
		foreach ($this->values as $value) {
			parent::addItem(
				(new CCheckBox($this->name.'['.$value['value'].']', $value['value']))
					->setLabel($value['name'])
					->setChecked($value['checked'])
			);
		}

		return parent::toString($destroy);
	}
}
