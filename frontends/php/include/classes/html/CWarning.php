<?php



class CWarning extends Ctag {

	public function __construct($header, $messages = [], $buttons = []) {
		parent::__construct('output', true);
		$this->addItem($header);
		$this->addClass(ZBX_STYLE_MSG_BAD);
		$this->addClass('msg-global');
		if ($messages) {
			parent::addItem(
				(new CDiv(
					(new CList($messages))->addClass(ZBX_STYLE_MSG_DETAILS_BORDER)
				))->addClass(ZBX_STYLE_MSG_DETAILS)
			);
		}
		parent::addItem((new CDiv($buttons))->addClass('msg-buttons'));
	}
}
