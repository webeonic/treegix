<?php



class CControllerScriptEdit extends CController {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		$fields = [
			'scriptid' =>				'db scripts.scriptid',
			'name' =>					'db scripts.name',
			'type' =>					'db scripts.type        |in '.TRX_SCRIPT_TYPE_CUSTOM_SCRIPT.','.TRX_SCRIPT_TYPE_IPMI,
			'execute_on' =>				'db scripts.execute_on  |in '.TRX_SCRIPT_EXECUTE_ON_AGENT.','.TRX_SCRIPT_EXECUTE_ON_SERVER.','.TRX_SCRIPT_EXECUTE_ON_PROXY,
			'command' =>				'db scripts.command',
			'commandipmi' =>			'db scripts.command',
			'description' =>			'db scripts.description',
			'host_access' =>			'db scripts.host_access |in '.PERM_READ.','.PERM_READ_WRITE,
			'groupid' =>				'db scripts.groupid',
			'usrgrpid' =>				'db scripts.usrgrpid',
			'hgstype' =>				'                        in 0,1',
			'confirmation' =>			'db scripts.confirmation',
			'enable_confirmation' =>	'                        in 1'
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

		if ($this->hasInput('scriptid')) {
			return (bool) API::Script()->get([
				'output' => [],
				'scriptids' => $this->getInput('scriptid'),
				'editable' => true
			]);
		}

		return true;
	}

	protected function doAction() {
		// default values
		$data = [
			'sid' => $this->getUserSID(),
			'scriptid' => 0,
			'name' => '',
			'type' => TRX_SCRIPT_TYPE_CUSTOM_SCRIPT,
			'execute_on' => TRX_SCRIPT_EXECUTE_ON_AGENT,
			'command' => '',
			'commandipmi' => '',
			'description' => '',
			'usrgrpid' => 0,
			'groupid' => 0,
			'host_access' => PERM_READ,
			'confirmation' => '',
			'enable_confirmation' => 0,
			'hgstype' => 0
		];

		// get values from the dabatase
		if ($this->hasInput('scriptid')) {
			$scripts = API::Script()->get([
				'output' => ['scriptid', 'name', 'type', 'execute_on', 'command', 'description', 'usrgrpid',
					'groupid', 'host_access', 'confirmation'
				],
				'scriptids' => $this->getInput('scriptid')
			]);
			$script = $scripts[0];

			$data['scriptid'] = $script['scriptid'];
			$data['name'] = $script['name'];
			$data['type'] = $script['type'];
			$data['execute_on'] = $script['execute_on'];
			$data['command'] = ($script['type'] == TRX_SCRIPT_TYPE_CUSTOM_SCRIPT) ? $script['command'] : '';
			$data['commandipmi'] = ($script['type'] == TRX_SCRIPT_TYPE_IPMI) ? $script['command'] : '';
			$data['description'] = $script['description'];
			$data['usrgrpid'] = $script['usrgrpid'];
			$data['groupid'] = $script['groupid'];
			$data['host_access'] = $script['host_access'];
			$data['confirmation'] = $script['confirmation'];
			$data['enable_confirmation'] = ($script['confirmation'] !== '');
			$data['hgstype'] = ($script['groupid'] != 0) ? 1 : 0;
		}

		// overwrite with input variables
		$this->getInputs($data, [
			'name',
			'type',
			'execute_on',
			'command',
			'commandipmi',
			'description',
			'usrgrpid',
			'groupid',
			'host_access',
			'confirmation',
			'enable_confirmation',
			'hgstype'
		]);

		// get host group
		if ($data['groupid'] == 0) {
			$data['hostgroup'] = null;
		}
		else {
			$hostgroups = API::HostGroup()->get([
				'groupids' => [$data['groupid']],
				'output' => ['groupid', 'name']
			]);
			$hostgroup = $hostgroups[0];

			$data['hostgroup'][] = [
				'id' => $hostgroup['groupid'],
				'name' => $hostgroup['name']
			];
		}

		// get list of user groups
		$usergroups = API::UserGroup()->get([
			'output' => ['usrgrpid', 'name']
		]);
		order_result($usergroups, 'name');
		$data['usergroups'] = $usergroups;

		$response = new CControllerResponseData($data);
		$response->setTitle(_('Configuration of scripts'));
		$this->setResponse($response);
	}
}
