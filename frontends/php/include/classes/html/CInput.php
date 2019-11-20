<?php



class CInput extends CTag {

	/**
	 * Enabled or disabled state of input field.
	 *
	 * @var bool
	 */
	protected $enabled = true;

	public function __construct($type = 'text', $name = 'textbox', $value = '') {
		parent::__construct('input');
		$this->setType($type);

		if ($name !== null) {
			$this->setId(trx_formatDomId($name));
			$this->setAttribute('name', $name);
		}

		$this->setAttribute('value', $value);
		return $this;
	}

	public function setType($type) {
		$this->setAttribute('type', $type);
		return $this;
	}

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
	 * @param bool $value
	 */
	public function setEnabled($value) {
		$this->enabled = $value;

		if ($value) {
			$this->removeAttribute('disabled');
		}
		else {
			$this->setAttribute('disabled', 'disabled');
		}

		return $this;
	}
}
