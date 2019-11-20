<?php



class CButtonQMessage extends CSubmit {

	public $vars;
	public $msg;
	public $name;

	public function __construct($name, $caption, $msg = null, $vars = null) {
		$this->vars = null;
		$this->msg = null;
		$this->name = $name;
		parent::__construct($name, $caption);
		$this->setMessage($msg);
		$this->setVars($vars);
		$this->setAction(null);
	}

	public function setVars($value = null) {
		$this->vars = $value;
		$this->setAction(null);
		return $this;
	}

	public function setMessage($value = null) {
		if (is_null($value)) {
			$value = _('Are you sure you want perform this action?');
		}
		// if message will contain single quotes, it will break everything, so it must be escaped
		$this->msg = trx_jsvalue(
			$value,
			false, // not as object
			false // do not add quotes to the string
		);
		$this->setAction(null);
		return $this;
	}

	public function setAction($value = null) {
		if (!is_null($value)) {
			parent::onClick($value);
			return $this;
		}

		global $page;
		$confirmation = "Confirm('".$this->msg."')";

		if (isset($this->vars)) {
			$link = $page['file'].'?'.$this->name.'=1'.$this->vars;
			$action = "redirect('".(new CUrl($link))->getUrl()."', 'post')";
		}
		else {
			$action = 'true';
		}
		parent::onClick('if ('.$confirmation.') { return '.$action.'; } else { return false; }');
		return $this;
	}
}
