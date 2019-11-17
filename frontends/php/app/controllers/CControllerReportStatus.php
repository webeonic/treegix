<?php



class CControllerReportStatus extends CController {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		return true;
	}

	protected function checkPermissions() {
		return ($this->getUserType() == USER_TYPE_SUPER_ADMIN);
	}

	protected function doAction() {
		// no data is passed to the view
		$response = new CControllerResponseData([]);
		$response->setTitle(_('System information'));
		$this->setResponse($response);
	}
}
