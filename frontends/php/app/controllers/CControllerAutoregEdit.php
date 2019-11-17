<?php



class CControllerAutoregEdit extends CController {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		$fields = [
			'tls_accept' =>				'in 0,'.HOST_ENCRYPTION_NONE.','.HOST_ENCRYPTION_PSK.','.(HOST_ENCRYPTION_NONE | HOST_ENCRYPTION_PSK),
			'tls_psk_identity' =>		'db config_autoreg_tls.tls_psk_identity',
			'tls_psk' =>				'db config_autoreg_tls.tls_psk',
			'change_psk' =>				'in 1'
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
		// get values from the database
		$autoreg = API::Autoregistration()->get([
			'output' => ['tls_accept']
		]);

		$data = [
			'tls_accept' => $autoreg['tls_accept'],
			'tls_psk_identity' => '',
			'tls_psk' => '',
			'change_psk' => !($autoreg['tls_accept'] & HOST_ENCRYPTION_PSK) || $this->hasInput('change_psk')
				|| $this->hasInput('tls_psk_identity')
		];

		// overwrite with input variables
		$this->getInputs($data, ['tls_accept', 'tls_psk_identity', 'tls_psk']);

		$response = new CControllerResponseData($data);
		$response->setTitle(_('Auto registration'));
		$this->setResponse($response);
	}
}
