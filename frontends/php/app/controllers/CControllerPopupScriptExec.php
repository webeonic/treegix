<?php



class CControllerPopupScriptExec extends CController {

	protected function checkInput() {
		$fields = [
			'hostid' =>			'db hosts.hostid',
			'scriptid' =>		'db scripts.scriptid'
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
		return (bool) API::Host()->get([
			'output' => [],
			'hostids' => $this->getInput('hostid')
		]);
	}

	protected function doAction() {
		$scriptid = $this->getInput('scriptid');
		$hostid = $this->getInput('hostid');

		$data = [
			'title' => _('Scripts'),
			'command' => '',
			'message' => '',
			'errors' => null,
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		];

		$scripts = API::Script()->get([
			'scriptids' => $scriptid,
			'output' => ['name', 'command']
		]);

		if ($scripts) {
			$script = $scripts[0];

			$macros_data = CMacrosResolverHelper::resolve([
				'config' => 'scriptConfirmation',
				'data' => [$hostid => [$scriptid => $script['command']]]
			]);

			$data['title'] = $script['name'];
			$data['command'] = $macros_data[$hostid][$scriptid];

			$result = API::Script()->execute([
				'hostid' => $hostid,
				'scriptid' => $scriptid
			]);

			if (!$result) {
				error(_('Cannot execute script'));
			}
			elseif ($result['response'] === 'failed') {
				error($result['value']);
			}
			else {
				$data['message'] = $result['value'];
			}
		}
		else {
			error(_('No permissions to referred object or it does not exist!'));
		}

		$data['errors'] = getMessages();

		$this->setResponse(new CControllerResponseData($data));
	}
}
