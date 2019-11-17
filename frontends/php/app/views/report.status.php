<?php



require_once dirname(__FILE__).'/../../include/blocks.inc.php';

$widget = (new CWidget())
	->setTitle(_('System information'))
	->addItem(make_status_of_zbx())->show();
