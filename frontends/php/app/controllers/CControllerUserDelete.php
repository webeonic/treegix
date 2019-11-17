<?php



class CControllerUserDelete extends CController {

	protected function checkInput() {
		$fields = [
			'userids' =>	'required|array_db users.userid'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		return ($this->getUserType() == USER_TYPE_SUPER_ADMIN);
	}

	protected function doAction() {
		$userids = $this->getInput('userids');

		$result = (bool) API::User()->delete($userids);

		$deleted = count($userids);

		$url = (new CUrl('treegix.php'))
			->setArgument('action', 'user.list')
			->setArgument('uncheck', '1');

		$response = new CControllerResponseRedirect($url->getUrl());

		if ($result) {
			$response->setMessageOk(_n('User deleted', 'Users deleted', $deleted));
		}
		else {
			$response->setMessageError(_n('Cannot delete user', 'Cannot delete users', $deleted));
		}

		$this->setResponse($response);
	}
}
