<?php



class CControllerScriptUpdate extends CController {

	protected function checkInput() {
		$fields = [
			'scriptid' =>				'fatal|required|db scripts.scriptid',
			'name' =>					'               db scripts.name',
			'type' =>					'               db scripts.type        |in '.TRX_SCRIPT_TYPE_CUSTOM_SCRIPT.','.TRX_SCRIPT_TYPE_IPMI,
			'execute_on' =>				'               db scripts.execute_on  |in '.TRX_SCRIPT_EXECUTE_ON_AGENT.','.TRX_SCRIPT_EXECUTE_ON_SERVER.','.TRX_SCRIPT_EXECUTE_ON_PROXY,
			'command' =>				'               db scripts.command     |flags '.P_CRLF,
			'commandipmi' =>			'               db scripts.command     |flags '.P_CRLF,
			'description' =>			'               db scripts.description',
			'host_access' =>			'               db scripts.host_access |in '.PERM_READ.','.PERM_READ_WRITE,
			'groupid' =>				'               db scripts.groupid',
			'usrgrpid' =>				'               db scripts.usrgrpid',
			'hgstype' =>				'                                       in 0,1',
			'confirmation' =>			'               db scripts.confirmation|not_empty',
			'enable_confirmation' =>	'                                       in 1'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			switch ($this->GetValidationError()) {
				case self::VALIDATION_ERROR:
					$response = new CControllerResponseRedirect('treegix.php?action=script.edit');
					$response->setFormData($this->getInputAll());
					$response->setMessageError(_('Cannot update script'));
					$this->setResponse($response);
					break;
				case self::VALIDATION_FATAL_ERROR:
					$this->setResponse(new CControllerResponseFatal());
					break;
			}
		}

		return $ret;
	}

	protected function checkPermissions() {
		if ($this->getUserType() != USER_TYPE_SUPER_ADMIN) {
			return false;
		}

		return (bool) API::Script()->get([
			'output' => [],
			'scriptids' => $this->getInput('scriptid'),
			'editable' => true
		]);
	}

	protected function doAction() {
		$script = [];

		$this->getInputs($script, ['scriptid', 'command', 'description', 'usrgrpid', 'groupid', 'host_access']);
		$script['name'] = trimPath($this->getInput('name', ''));
		$script['type'] = $this->getInput('type', TRX_SCRIPT_TYPE_CUSTOM_SCRIPT);
		$script['execute_on'] = $this->getInput('execute_on', TRX_SCRIPT_EXECUTE_ON_SERVER);
		$script['confirmation'] = $this->getInput('confirmation', '');

		if ($script['type'] == TRX_SCRIPT_TYPE_IPMI) {
			if ($this->hasInput('commandipmi')) {
				$script['command'] = $this->getInput('commandipmi');
			}
			$script['execute_on'] = TRX_SCRIPT_EXECUTE_ON_SERVER;
		}

		if ($this->getInput('hgstype', 1) == 0) {
			$script['groupid'] = 0;
		}

		$result = (bool) API::Script()->update($script);

		if ($result) {
			$response = new CControllerResponseRedirect('treegix.php?action=script.list&uncheck=1');
			$response->setMessageOk(_('Script updated'));
		}
		else {
			$response = new CControllerResponseRedirect('treegix.php?action=script.edit&scriptid='.$this->getInput('scriptid'));
			$response->setFormData($this->getInputAll());
			$response->setMessageError(_('Cannot update script'));
		}
		$this->setResponse($response);
	}
}
