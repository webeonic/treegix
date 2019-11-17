<?php



require_once dirname(__FILE__).'/../../include/blocks.inc.php';

class CControllerDashboardWidgetRfRate extends CController {

	protected function checkInput() {
		$fields = [
			'widgetid' =>	'required|db widget.widgetid',
			'rf_rate' =>	'required|in 0,10,30,60,120,600,900'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseData(['main_block' => CJs::encodeJson('')]));
		}

		return $ret;
	}

	protected function checkPermissions() {
		return ($this->getUserType() >= USER_TYPE_TREEGIX_USER);
	}

	protected function doAction() {
		CProfile::update('web.dashbrd.widget.rf_rate', $this->getInput('rf_rate'), PROFILE_TYPE_INT,
			$this->getInput('widgetid')
		);

		$this->setResponse(new CControllerResponseData(['main_block' => CJs::encodeJson('')]));
	}
}
