<?php



class CControllerMediatypeDisable extends CController {

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
				'status' => MEDIA_TYPE_STATUS_DISABLED
			];
		}
		$result = API::Mediatype()->update($mediatypes);

		$updated = count($mediatypes);

		$response = new CControllerResponseRedirect('treegix.php?action=mediatype.list&uncheck=1');

		if ($result) {
			$response->setMessageOk(_n('Media type disabled', 'Media types disabled', $updated));
		}
		else {
			$response->setMessageError(_n('Cannot disable media type', 'Cannot disable media types', $updated));
		}
		$this->setResponse($response);
	}
}
