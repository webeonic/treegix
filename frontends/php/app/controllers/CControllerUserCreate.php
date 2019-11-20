<?php



class CControllerUserCreate extends CControllerUserUpdateGeneral {

	protected function checkInput() {
		$locales = array_keys(getLocales());
		$themes = array_keys(Z::getThemes());
		$themes[] = THEME_DEFAULT;

		$fields = [
			'alias' =>			'required|db users.alias|not_empty',
			'name' =>			'db users.name',
			'surname' =>		'db users.surname',
			'password1' =>		'required|db users.passwd',
			'password2' =>		'required|db users.passwd',
			'type' =>			'db users.type|in '.USER_TYPE_TREEGIX_USER.','.USER_TYPE_TREEGIX_ADMIN.','.USER_TYPE_SUPER_ADMIN,
			'user_groups' =>	'required|array_id|not_empty',
			'user_medias' =>	'array',
			'lang' =>			'db users.lang|in '.implode(',', $locales),
			'theme' =>			'db users.theme|in '.implode(',', $themes),
			'autologin' =>		'db users.autologin|in 0,1',
			'autologout' =>		'db users.autologout|not_empty',
			'url' =>			'db users.url',
			'refresh' =>		'required|db users.refresh|not_empty',
			'rows_per_page' =>	'required|db users.rows_per_page',
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
					$response = new CControllerResponseRedirect('treegix.php?action=user.edit');
					$response->setFormData($this->getInputAll());
					$response->setMessageError(_('Cannot add user'));
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
		return ($this->getUserType() == USER_TYPE_SUPER_ADMIN);
	}

	protected function doAction() {
		$user = [];

		$this->getInputs($user, ['alias', 'name', 'surname', 'url', 'autologin', 'autologout', 'theme', 'refresh',
			'rows_per_page', 'lang', 'type'
		]);
		$user['usrgrps'] = trx_toObject($this->getInput('user_groups'), 'usrgrpid');

		if ($this->getInput('password1', '') !== '' || !$this->allow_empty_password) {
			$user['passwd'] = $this->getInput('password1');
		}

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

		$result = (bool) API::User()->create($user);

		if ($result) {
			$response = new CControllerResponseRedirect('treegix.php?action=user.list&uncheck=1');
			$response->setMessageOk(_('User added'));
		}
		else {
			$response = new CControllerResponseRedirect('treegix.php?action=user.edit');
			$response->setFormData($this->getInputAll());
			$response->setMessageError(_('Cannot add user'));
		}
		$this->setResponse($response);
	}
}
