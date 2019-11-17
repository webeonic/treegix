<?php



/**
 * Class containing operations for updating user profile.
 */
class CControllerUserProfileUpdate extends CControllerUserUpdateGeneral {

	protected function checkInput() {
		$locales = array_keys(getLocales());
		$themes = array_keys(Z::getThemes());
		$themes[] = THEME_DEFAULT;

		$fields = [
			'userid' =>			'fatal|required|db users.userid',
			'password1' =>		'db users.passwd',
			'password2' =>		'db users.passwd',
			'user_medias' =>	'array',
			'lang' =>			'db users.lang|in '.implode(',', $locales),
			'theme' =>			'db users.theme|in '.implode(',', $themes),
			'autologin' =>		'db users.autologin|in 0,1',
			'autologout' =>		'db users.autologout|not_empty',
			'refresh' =>		'db users.refresh|not_empty',
			'rows_per_page' =>	'db users.rows_per_page',
			'url' =>			'db users.url',
			'messages' =>		'array',
			'form_refresh' =>	'int32'
		];

		$ret = $this->validateInput($fields);
		$error = $this->GetValidationError();

		if ($ret && !$this->validatePassword()) {
			$error = self::VALIDATION_ERROR;
			$ret = false;
		}

		if (!$ret) {
			switch ($error) {
				case self::VALIDATION_ERROR:
					$response = new CControllerResponseRedirect('treegix.php?action=userprofile.edit');
					$response->setFormData($this->getInputAll());
					$response->setMessageError(_('Cannot update user'));
					$this->setResponse($response);
					break;

				case self::VALIDATION_FATAL_ERROR:
					$this->setResponse(new CControllerResponseFatal());
					break;
			}
		}

		return $ret;
	}

	protected function checkPermissions() {
		return (bool) API::User()->get([
			'output' => [],
			'userids' => $this->getInput('userid'),
			'editable' => true
		]);
	}

	protected function doAction() {
		$user = [];

		$this->getInputs($user, ['lang', 'theme', 'autologin', 'autologout', 'refresh', 'rows_per_page', 'url']);
		$user['userid'] = CWebUser::$data['userid'];

		if ($this->getInput('password1', '') !== ''
				|| ($this->hasInput('password1') && $this->auth_type == TRX_AUTH_INTERNAL)) {
			$user['passwd'] = $this->getInput('password1');
		}

		if (CWebUser::$data['type'] > USER_TYPE_TREEGIX_USER) {
			$user['user_medias'] = [];

			foreach ($this->getInput('user_medias', []) as $media) {
				$user['user_medias'][] = [
					'mediatypeid' => $media['mediatypeid'],
					'sendto' => $media['sendto'],
					'active' => $media['active'],
					'severity' => $media['severity'],
					'period' => $media['period']
				];
			}
		}

		DBstart();
		$result = updateMessageSettings($this->getInput('messages', []));
		$result = $result && (bool) API::User()->update($user);
		$result = DBend($result);

		if ($result) {
			$response = new CControllerResponseRedirect(TRX_DEFAULT_URL);
			$response->setMessageOk(_('User updated'));
		}
		else {
			$response = new CControllerResponseRedirect('treegix.php?action=userprofile.edit');
			$response->setFormData($this->getInputAll());
			$response->setMessageError(_('Cannot update user'));
		}

		$this->setResponse($response);
	}
}
