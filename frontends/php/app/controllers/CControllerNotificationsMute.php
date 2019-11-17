<?php



class CControllerNotificationsMute extends CController {

	protected function checkInput() {
		$fields = [
			'muted' => 'required|in 0,1'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$data = CJs::encodeJson(['error' => true]);
			$this->setResponse(new CControllerResponseData(['main_block' => $data]));
		}

		return $ret;
	}

	protected function checkPermissions() {
		return (!CWebUser::isGuest() && $this->getUserType() >= USER_TYPE_TREEGIX_USER);
	}

	protected function doAction() {
		$msg_settings = getMessageSettings();
		$msg_settings['sounds.mute'] = $this->input['muted'];

		updateMessageSettings($msg_settings);

		$data = CJs::encodeJson(['muted' => (int) $msg_settings['sounds.mute']]);
		$this->setResponse(new CControllerResponseData(['main_block' => $data]));
	}
}
