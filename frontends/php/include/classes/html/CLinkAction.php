<?php



class CLinkAction extends CLink {

	public function __construct($items = null) {
		parent::__construct($items);

		$this->addClass(TRX_STYLE_LINK_ACTION);
	}
}
