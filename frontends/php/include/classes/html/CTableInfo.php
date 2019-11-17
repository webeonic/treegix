<?php



class CTableInfo extends CTable {

	protected $message;

	public function __construct() {
		parent::__construct();

		$this->addClass(ZBX_STYLE_LIST_TABLE);
		$this->setNoDataMessage(_('No data found.'));
		$this->addMakeVerticalRotationJs = false;
	}

	public function toString($destroy = true) {
		$tableid = $this->getId();

		if (!$tableid) {
			$tableid = uniqid('t', true);
			$tableid = str_replace('.', '', $tableid);
			$this->setId($tableid);
		}

		$string = parent::toString($destroy);

		if ($this->addMakeVerticalRotationJs) {
			$string .= get_js(
				'var makeVerticalRotationForTable = function() {'.
					'jQuery("#'.$tableid.'").makeVerticalRotation();'.
				'}'.
				"\n".
				'if (!jQuery.isReady) {'.
					'jQuery(document).ready(makeVerticalRotationForTable);'.
				'}'.
				'else {'.
					'makeVerticalRotationForTable();'.
				'}',
				true
			);
		}

		return $string;
	}

	public function setNoDataMessage($message) {
		$this->message = $message;

		return $this;
	}

	/**
	 * Rotate table header text vertical.
	 * Cells must be marked with "vertical_rotation" class.
	 */
	public function makeVerticalRotation() {
		$this->addMakeVerticalRotationJs = true;

		return $this;
	}

	protected function endToString() {
		$ret = '';
		if ($this->rownum == 0 && $this->message !== null) {
			$ret .= $this->prepareRow(new CCol($this->message), ZBX_STYLE_NOTHING_TO_SHOW)->toString();
		}
		$ret .= parent::endToString();

		return $ret;
	}
}
