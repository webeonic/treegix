<?php



class CControllerAutoregUpdate extends CController {

	protected function checkInput() {
		$fields = [
			'tls_accept' =>				'in '.HOST_ENCRYPTION_NONE.','.HOST_ENCRYPTION_PSK.','.(HOST_ENCRYPTION_NONE | HOST_ENCRYPTION_PSK),
			'tls_psk_identity' =>		'db config_autoreg_tls.tls_psk_identity',
			'tls_psk' =>				'db config_autoreg_tls.tls_psk'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			switch ($this->GetValidationError()) {
				case self::VALIDATION_ERROR:
					$response = new CControllerResponseRedirect((new CUrl('treegix.php'))
						->setArgument('action', 'autoreg.edit')
						->getUrl()
					);
					$response->setFormData($this->getInputAll());
					$response->setMessageError(_('Cannot update configuration'));
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
		$autoreg = [];

		$this->getInputs($autoreg, ['tls_accept', 'tls_psk_identity', 'tls_psk']);

		$result = (bool) API::Autoregistration()->update($autoreg);

		$response = new CControllerResponseRedirect((new CUrl('treegix.php'))
			->setArgument('action', 'autoreg.edit')
			->getUrl()
		);

		if ($result) {
			$response->setMessageOk(_('Configuration updated'));
		}
		else {
			$response->setFormData($this->getInputAll());
			$response->setMessageError(_('Cannot update configuration'));
		}

		$this->setResponse($response);
	}
}
