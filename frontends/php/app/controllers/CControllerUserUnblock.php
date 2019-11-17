<?php



class CControllerUserUnblock extends CController {

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

		DBstart();

		$users = API::User()->get([
			'output' => ['alias', 'name', 'surname'],
			'userids' => $userids,
			'editable' => true
		]);

		$result = (count($users) == count($userids) && unblock_user_login($userids));

		if ($result) {
			foreach ($users as $user) {
				info('User '.$user['alias'].' unblocked');
				add_audit(AUDIT_ACTION_UPDATE, AUDIT_RESOURCE_USER,
					'Unblocked user alias ['.$user['alias'].'] name ['.$user['name'].'] surname ['.$user['surname'].']'
				);
			}
		}

		$result = DBend($result);

		$unblocked = count($userids);

		$url = (new CUrl('treegix.php'))
			->setArgument('action', 'user.list')
			->setArgument('uncheck', '1');

		$response = new CControllerResponseRedirect($url->getUrl());

		if ($result) {
			$response->setMessageOk(_n('User unblocked', 'Users unblocked', $unblocked));
		}
		else {
			$response->setMessageError(_n('Cannot unblock user', 'Cannot unblock users', $unblocked));
		}

		$this->setResponse($response);
	}
}
