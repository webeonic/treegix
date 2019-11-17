<?php



class CControllerMediatypeDelete extends CController {

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
		$mediatypeids = $this->getInput('mediatypeids');

		$result = API::Mediatype()->delete($mediatypeids);

		$deleted = count($mediatypeids);

		$response = new CControllerResponseRedirect('treegix.php?action=mediatype.list&uncheck=1');

		if ($result) {
			$response->setMessageOk(_n('Media type deleted', 'Media types deleted', $deleted));
		}
		else {
			$response->setMessageError(_n('Cannot delete media type', 'Cannot delete media types', $deleted));
		}
		$this->setResponse($response);
	}
}
