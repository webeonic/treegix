<?php



class CControllerScriptDelete extends CController {

	protected function checkInput() {
		$fields = [
			'scriptids' =>	'required|array_db scripts.scriptid'
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

		$scripts = API::Script()->get([
			'countOutput' => true,
			'scriptids' => $this->getInput('scriptids'),
			'editable' => true
		]);

		return ($scripts == count($this->getInput('scriptids')));
	}

	protected function doAction() {
		$scriptids = $this->getInput('scriptids');

		$result = (bool) API::Script()->delete($scriptids);

		$deleted = count($scriptids);

		$response = new CControllerResponseRedirect('treegix.php?action=script.list&uncheck=1');

		if ($result) {
			$response->setMessageOk(_n('Script deleted', 'Scripts deleted', $deleted));
		}
		else {
			$response->setMessageError(_n('Cannot delete script', 'Cannot delete scripts', $deleted));
		}

		$this->setResponse($response);
	}
}
