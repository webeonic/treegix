<?php



class CWidgetFieldMsGraphPrototype extends CWidgetFieldMs {

	public function __construct($name, $label) {
		parent::__construct($name, $label);

		$this->setSaveType(ZBX_WIDGET_FIELD_TYPE_GRAPH_PROTOTYPE);
	}
}
