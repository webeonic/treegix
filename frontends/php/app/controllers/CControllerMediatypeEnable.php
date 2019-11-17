<?php



class CControllerMediatypeEnable extends CController {

	protected function checkInput() {
		$fields = [
			'mediatypeids' =>	'required|array_db media_type.mediatypeid'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		if ($this->getUserType() != USER_TYPE_SUPER_ADMIN) {
			return false;
		}

		$mediatypes = API::Mediatype()->get([
			'mediatypeids' => $this->getInput('mediatypeids'),
			'countOutput' => true,
			'editable' => true
		]);

		return ($mediatypes == count($this->getInput('mediatypeids')));
	}

	protected function doAction() {
		$mediatypes = [];

		foreach ($this->getInput('mediatypeids') as $mediatypeid) {
			$mediatypes[] = [
				'mediatypeid' => $mediatypeid,
				'status' => MEDIA_TYPE_STATUS_ACTIVE
			];
		}
		$result = API::Mediatype()->update($mediatypes);

		$updated = count($mediatypes);

		$response = new CControllerResponseRedirect('treegix.php?action=mediatype.list&uncheck=1');

		if ($result) {
			$response->setMessageOk(_n('Media type enabled', 'Media types enabled', $updated));
		}
		else {
			$response->setMessageError(_n('Cannot enable media type', 'Cannot enable media types', $updated));
		}
		$this->setResponse($response);
	}
}
