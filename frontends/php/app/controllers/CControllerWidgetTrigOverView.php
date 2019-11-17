<?php



class CControllerWidgetTrigOverView extends CControllerWidget {

	public function __construct() {
		parent::__construct();

		$this->setType(WIDGET_TRIG_OVER);
		$this->setValidationRules([
			'name' => 'string',
			'fields' => 'json'
		]);
	}

	protected function doAction() {
		$fields = $this->getForm()->getFieldsData();

		$data = [
			'name' => $this->getInput('name', $this->getDefaultHeader()),
			'style' => $fields['style'],
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		];

		$trigger_options = [
			'only_true' => ($fields['show'] == TRIGGERS_OPTION_RECENT_PROBLEM) ? true : null,
			'filter' => ['value' => ($fields['show'] == TRIGGERS_OPTION_IN_PROBLEM) ? TRIGGER_VALUE_TRUE : null]
		];

		list($data['hosts'], $data['triggers']) = getTriggersOverviewData(getSubGroups($fields['groupids']),
			$fields['application'], [], $trigger_options, [
				'show_suppressed' => $fields['show_suppressed'],
				'show_recent' => ($fields['show'] == TRIGGERS_OPTION_RECENT_PROBLEM) ? true : null
			]
		);

		$this->setResponse(new CControllerResponseData($data));
	}
}
