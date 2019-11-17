<?php
 


class CParam extends CTag {

	function __construct($name, $value) {
		parent::__construct('param');
		$this->attributes['name'] = $name;
		$this->attributes['value'] = $value;
	}
}
