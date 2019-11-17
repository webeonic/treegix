<?php



class CControllerDashboardPropertiesCheck extends CController {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		$fields = [
			'dashboardid' =>		'required|db dashboard.dashboardid',
			'userid'	  =>		'required|db users.userid',
			'name'		  =>		'string|not_empty'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(
				(new CControllerResponseData([
					'main_block' => CJs::encodeJson(['errors' => getMessages()->toString()])
				]))->disableView()
			);
		}

		return $ret;
	}

	protected function checkPermissions() {
		return true;
	}

	protected function doAction() {
		// Dashboard with ID 0 is considered as newly created dashboard.
		if ($this->getInput('dashboardid') != 0) {
			$dashboards = API::Dashboard()->get([
				'output' => [],
				'dashboardids' => $this->getInput('dashboardid'),
				'editable' => true
			]);

			if (!$dashboards) {
				error(_('No permissions to referred object or it does not exist!'));
			}
		}

		if (!hasErrorMesssages() && $this->getUserType() == USER_TYPE_SUPER_ADMIN) {
			$users = API::User()->get([
				'output' => [],
				'userids' => $this->getInput('userid')
			]);

			if (!$users) {
				error(_s('User with ID "%1$s" is not available.', $this->getInput('userid')));
			}
		}

		$output = [];
		if (($messages = getMessages()) !== null) {
			$output = [
				'errors' => $messages->toString()
			];
		}

		$this->setResponse(
			(new CControllerResponseData(['main_block' => CJs::encodeJson($output)]))->disableView()
		);
	}
}
