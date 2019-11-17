<?php



class CVisibilityBox extends CCheckBox {

	public function __construct($name = 'visibilitybox', $object_id = null, $replace_to = null) {
		$this->object_id = $object_id;
		$this->replace_to = unpack_object($replace_to);

		parent::__construct($name);
		$this->onClick('visibility_status_changeds(this.checked, '.zbx_jsvalue($this->object_id).', '.
			zbx_jsvalue($this->replace_to).');');
		insert_javascript_for_visibilitybox();
	}

	/**
	 * Set the label for the checkbox and put it on the left.
	 *
	 * @param string $label
	 *
	 * @return CVisibilityBox
	 */
	public function setLabel($label) {
		parent::setLabel($label);
		$this->setLabelPosition(self::LABEL_POSITION_LEFT);

		return $this;
	}

	public function toString($destroy = true) {
		if (!isset($this->attributes['checked'])) {
			zbx_add_post_js('visibility_status_changeds(false, '.zbx_jsvalue($this->object_id).', '.
				zbx_jsvalue($this->replace_to).');');
		}

		return parent::toString($destroy);
	}
}
