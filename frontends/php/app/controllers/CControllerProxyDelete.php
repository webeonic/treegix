<?php



class CControllerProxyDelete extends CController {

	protected function checkInput() {
		$fields = [
			'proxyids' =>	'array_db hosts.hostid|required'
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

		$proxies = API::Proxy()->get([
			'proxyids' => $this->getInput('proxyids'),
			'countOutput' => true,
			'editable' => true
		]);

		return ($proxies == count($this->getInput('proxyids')));
	}

	protected function doAction() {
		$proxyids = $this->getInput('proxyids');

		$result = API::Proxy()->delete($proxyids);

		$deleted = count($proxyids);

		$response = new CControllerResponseRedirect('treegix.php?action=proxy.list&uncheck=1');

		if ($result) {
			$response->setMessageOk(_n('Proxy deleted', 'Proxies deleted', $deleted));
		}
		else {
			$response->setMessageError(_n('Cannot delete proxy', 'Cannot delete proxies', $deleted));
		}
		$this->setResponse($response);
	}
}
