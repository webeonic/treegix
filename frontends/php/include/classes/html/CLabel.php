<?php



class CLabel extends CTag {

	public function __construct($label, $for = null) {
		parent::__construct('label', true, $label);

		if ($for !== null) {
			$this->setAttribute('for', trx_formatDomId($for));
		}
	}

	/**
	 * Allow to add visual 'asterisk' mark to label.
	 *
	 * @param bool $add_asterisk  Define is label marked with asterisk or not.
	 *
	 * @return CLabel
	 */
	public function setAsteriskMark($add_asterisk = true) {
		return $this->addClass($add_asterisk ? TRX_STYLE_FIELD_LABEL_ASTERISK : null);
	}
}
