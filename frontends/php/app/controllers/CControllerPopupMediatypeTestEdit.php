<?php
 


class CControllerPopupMediatypeTestEdit extends CController {

	protected function checkInput() {
		$fields = [
			'mediatypeid' => 'fatal|required|db media_type.mediatypeid'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$output = [];
			if (($messages = getMessages()) !== null) {
				$output['errors'] = $messages->toString();
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

	protected function doAction() {
		$mediatype = API::MediaType()->get([
			'output' => ['type', 'status', 'parameters'],
			'mediatypeids' => $this->getInput('mediatypeid'),
		]);

		if (!$mediatype) {
			error(_('No permissions to referred object or it does not exist!'));

			$output = [];
			if (($messages = getMessages(false, null, false)) !== null) {
				$output['errors'] = $messages->toString();
			}

			$this->setResponse(
				(new CControllerResponseData(['main_block' => CJs::encodeJson($output)]))->disableView()
			);

			return;
		}

		if ($mediatype[0]['status'] != MEDIA_STATUS_ACTIVE) {
			error(_('Cannot test disabled media type.'));
		}

		$this->setResponse(new CControllerResponseData([
			'title' => _('Test media type'),
			'errors' => hasErrorMesssages() ? getMessages() : null,
			'mediatypeid' => $this->getInput('mediatypeid'),
			'sendto' => '',
			'subject' => _('Test subject'),
			'message' => _('This is the test message from Treegix'),
			'parameters' => $mediatype[0]['parameters'],
			'type' => $mediatype[0]['type'],
			'enabled' => ($mediatype[0]['status'] == MEDIA_STATUS_ACTIVE),
			'user' => ['debug_mode' => $this->getDebugMode()]
		]));
	}
}
