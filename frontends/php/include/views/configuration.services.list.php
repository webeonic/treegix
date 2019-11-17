<?php



return (new CWidget())
	->setTitle(_('Services'))
	->addItem($this->data['tree']->getHTML());
