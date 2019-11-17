<?php



class CWidgetFieldSeverities extends CWidgetFieldCheckBoxList {

	public function __construct($name, $label) {
		parent::__construct($name, $label);

		$this->setExValidationRules(
			['in' => implode(',', range(TRIGGER_SEVERITY_NOT_CLASSIFIED, TRIGGER_SEVERITY_COUNT - 1))
		]);
	}
}
