<?php



class CControllerPopupMediatypeTestSend extends CController {

	private $metiatype;

	protected function checkInput() {
		$fields = [
			'mediatypeid' =>	'fatal|required|db media_type.mediatypeid',
			'sendto' =>			'string|not_empty',
			'subject' =>		'string',
			'message' =>		'string',
			'parameters' =>		'array'
		];

		$ret = $this->validateInput($fields) && $this->validateMediaType();

		if (!$ret) {
			$output = [];

			if (($messages = getMessages(false, _('Media type test failed.'))) !== null) {
				$output['messages'] = $messages->toString();
			}

			$this->setResponse(
				(new CControllerResponseData(['main_block' => CJs::encodeJson($output)]))->disableView()
			);
		}

		return $ret;
	}

	protected function checkPermissions() {
		return ($this->getUserType() == USER_TYPE_SUPER_ADMIN);
	}

	/**
	 * Additional method to validate fields specific for mediatype.
	 *
	 * @return bool
	 */
	protected function validateMediaType() {
		$mediatypes = API::MediaType()->get([
			'output' => ['type', 'status'],
			'mediatypeids' => $this->getInput('mediatypeid'),
		]);

		if (!$mediatypes) {
			error(_('No permissions to referred object or it does not exist!'));

			return false;
		}

		$this->mediatype = $mediatypes[0];

		if ($this->mediatype['status'] != MEDIA_STATUS_ACTIVE) {
			error(_('Cannot test disabled media type.'));

			return false;
		}

		$ret = true;

		if ($this->mediatype['type'] != MEDIA_TYPE_EXEC && $this->mediatype['type'] != MEDIA_TYPE_WEBHOOK) {
			$validator = new CNewValidator(array_map('trim', $this->getInputAll()), [
				'message' =>	'string|not_empty'
			]);

			foreach ($validator->getAllErrors() as $error) {
				error($error);
			}

			$ret = !$validator->isError();

			if ($ret && $this->mediatype['type'] == MEDIA_TYPE_EMAIL) {
				$email_validator = new CEmailValidator();
				$ret = $email_validator->validate($this->getInput('sendto'));

				if (!$ret) {
					error($email_validator->getError());
				}
			}
		}

		return $ret;
	}

	protected function doAction() {
		global $TRX_SERVER, $TRX_SERVER_PORT;

		if ($this->mediatype['type'] == MEDIA_TYPE_WEBHOOK ) {
			$params = [];

			foreach ($this->getInput('parameters', []) as $parameter) {
				$params[$parameter['name']] = $parameter['value'];
			}

			$params = ['parameters' => $params];
		}
		else {
			$params = [
				'sendto' =>	$this->getInput('sendto'),
				'subject' => $this->getInput('subject'),
				'message' => $this->getInput('message')
			];
		}

		$params['mediatypeid'] = $this->getInput('mediatypeid');
		$server = new CTreegixServer($TRX_SERVER, $TRX_SERVER_PORT, TRX_SOCKET_TIMEOUT, TRX_SOCKET_BYTES_LIMIT);
		$result = $server->testMediaType($params, CWebUser::getSessionCookie());

		if ($result) {
			$msg_title = null;
			info(_('Media type test successful.'));
		}
		else {
			$msg_title = _('Media type test failed.');
			error($server->getError());
		}

		$output = [
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		];

		if (($messages = getMessages($result, $msg_title)) !== null) {
			$output['messages'] = $messages->toString();
		}

		$this->setResponse((new CControllerResponseData(['main_block' => CJs::encodeJson($output)]))->disableView());
	}
}
