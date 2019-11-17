<?php



class CControllerProxyHostEnable extends CController {

	protected function checkInput() {
		$fields = [
			'proxyids' =>	'required|array_db hosts.hostid'
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
		$hosts = API::Host()->get([
			'output' => ['hostid'],
			'filter' => [
				'proxy_hostid' => $this->getInput('proxyids'),
				'status' => HOST_STATUS_NOT_MONITORED
			]
		]);

		foreach ($hosts as &$host) {
			$host['status'] = HOST_STATUS_MONITORED;
		}
		unset($host);

		$result = API::Host()->update($hosts);

		$updated = count($hosts);

		$response = new CControllerResponseRedirect('treegix.php?action=proxy.list&uncheck=1');

		if ($result) {
			$response->setMessageOk(_n('Host enabled', 'Hosts enabled', $updated));
		}
		else {
			$response->setMessageError(_n('Cannot enable host', 'Cannot enable hosts', $updated));
		}
		$this->setResponse($response);
	}
}
