<?php



class CControllerWidgetUrlView extends CControllerWidget {

	public function __construct() {
		parent::__construct();

		$this->setType(WIDGET_URL);
		$this->setValidationRules([
			'name' => 'string',
			'fields' => 'json',
			'dynamic_hostid' => 'db hosts.hostid'
		]);
	}

	protected function doAction() {
		$fields = $this->getForm()->getFieldsData();
		$error = null;
		$dynamic_hostid = $this->getInput('dynamic_hostid', '0');

		if ($fields['dynamic'] == WIDGET_DYNAMIC_ITEM && $dynamic_hostid == 0) {
			$error = _('No host selected.');
		}
		else {
			$resolveHostMacros = ($fields['dynamic'] == WIDGET_DYNAMIC_ITEM);

			$resolved_url = CMacrosResolverHelper::resolveWidgetURL([
				'config' => $resolveHostMacros ? 'widgetURL' : 'widgetURLUser',
				'url' => $fields['url'],
				'hostid' => $resolveHostMacros ? $dynamic_hostid : '0'
			]);

			$fields['url'] = $resolved_url ? $resolved_url : $fields['url'];
		}

		if (!$error && !CHtmlUrlValidator::validate($fields['url'])) {
			$error = _s('Provided URL "%1$s" is invalid.', $fields['url']);
		}

		$this->setResponse(new CControllerResponseData([
			'name' => $this->getInput('name', $this->getDefaultHeader()),
			'url' => [
				'url' => $fields['url'],
				'error' => $error
			],
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		]));
	}
}
