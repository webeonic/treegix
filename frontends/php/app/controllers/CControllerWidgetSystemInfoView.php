<?php



require_once dirname(__FILE__).'/../../include/blocks.inc.php';

class CControllerWidgetSystemInfoView extends CControllerWidget {

	public function __construct() {
		parent::__construct();

		$this->setType(WIDGET_SYSTEM_INFO);
		$this->setValidationRules([
			'name' => 'string',
			'fields' => 'json'
		]);
	}

	protected function doAction() {
		$this->setResponse(new CControllerResponseData([
			'name' => $this->getInput('name', $this->getDefaultHeader()),
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		]));
	}
}
