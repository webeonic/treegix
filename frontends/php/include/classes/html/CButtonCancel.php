<?php



class CButtonCancel extends CButton {

	public function __construct($vars = null, $action = null) {
		parent::__construct('cancel', _('Cancel'));
		if (is_null($action)) {
			$this->setVars($vars);
		}
		if ($action !== null) {
			$this->onClick($action);
		}
	}

	public function setVars($value = null) {
		$url = '?cancel=1';
		if (!empty($value)) {
			$url .= $value;
		}
		$uri = new CUrl($url);
		$url = $uri->getUrl();
		$this->onClick("javascript: return redirect('".$url."');");
		return $this;
	}
}
