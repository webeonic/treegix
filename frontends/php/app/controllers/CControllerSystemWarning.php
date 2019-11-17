<?php



class CControllerSystemWarning extends CController {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		return true;
	}

	protected function checkPermissions() {
		return true;
	}

	protected function doAction() {
		$data = [
			'theme' => getUserTheme(CWebUser::$data),
			'messages' => []
		];

		if (CSession::keyExists('messages')) {
			$data['messages'] = CSession::getValue('messages');
			CSession::unsetValue(['messages']);
		}

		$this->setResponse(new CControllerResponseData($data));
	}
}
