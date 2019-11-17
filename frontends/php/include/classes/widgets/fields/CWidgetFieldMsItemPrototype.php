<?php
 


class CWidgetFieldMsItemPrototype extends CWidgetFieldMs {

	public function __construct($name, $label) {
		parent::__construct($name, $label);

		$this->setSaveType(TRX_WIDGET_FIELD_TYPE_ITEM_PROTOTYPE);
	}
}
