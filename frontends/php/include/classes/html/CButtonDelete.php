<?php



class CButtonDelete extends CButtonQMessage {

	public function __construct($msg = null, $vars = null) {
		parent::__construct('delete', _('Delete'), $msg, $vars);
	}
}
