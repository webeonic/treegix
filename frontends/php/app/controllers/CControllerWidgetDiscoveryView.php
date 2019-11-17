<?php



require_once dirname(__FILE__).'/../../include/blocks.inc.php';

class CControllerWidgetDiscoveryView extends CControllerWidget {

	public function __construct() {
		parent::__construct();

		$this->setType(WIDGET_DISCOVERY);
		$this->setValidationRules([
			'name' => 'string',
			'fields' => 'json'
		]);
	}

	protected function doAction() {
		if ($this->getUserType() >= USER_TYPE_TREEGIX_ADMIN) {
			$drules = API::DRule()->get([
				'output' => ['druleid', 'name'],
				'selectDHosts' => ['status'],
				'filter' => ['status' => DHOST_STATUS_ACTIVE]
			]);
			CArrayHelper::sort($drules, ['name']);

			foreach ($drules as &$drule) {
				$drule['up'] = 0;
				$drule['down'] = 0;

				foreach ($drule['dhosts'] as $dhost){
					if ($dhost['status'] == DRULE_STATUS_DISABLED) {
						$drule['down']++;
					}
					else {
						$drule['up']++;
					}
				}
			}
			unset($drule);

			$error = null;
		}
		else {
			$drules = [];
			$error = _('No permissions to referred object or it does not exist!');
		}

		$this->setResponse(new CControllerResponseData([
			'name' => $this->getInput('name', $this->getDefaultHeader()),
			'drules' => $drules,
			'error' => $error,
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		]));
	}
}
