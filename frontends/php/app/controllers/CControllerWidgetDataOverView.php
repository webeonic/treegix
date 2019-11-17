<?php



class CControllerWidgetDataOverView extends CControllerWidget {

	public function __construct() {
		parent::__construct();

		$this->setType(WIDGET_DATA_OVER);
		$this->setValidationRules([
			'name' => 'string',
			'fields' => 'json',
		]);
	}

	protected function doAction() {
		$fields = $this->getForm()->getFieldsData();

		$this->setResponse(new CControllerResponseData([
			'name' => $this->getInput('name', $this->getDefaultHeader()),
			'groupids' => getSubGroups($fields['groupids']),
			'application' => $fields['application'],
			'show_suppressed' => $fields['show_suppressed'],
			'style' => $fields['style'],
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		]));
	}
}
