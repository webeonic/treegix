<?php



class CControllerDashboardWidgetConfigure extends CController {

	protected function checkInput() {
		$fields = [
			'type' => 'in '.implode(',', array_keys(CWidgetConfig::getKnownWidgetTypes())),
			'fields' => 'json',
			'view_mode' => 'in '.implode(',', [TRX_WIDGET_VIEW_MODE_NORMAL, TRX_WIDGET_VIEW_MODE_HIDDEN_HEADER])
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		return ($this->getUserType() >= USER_TYPE_TREEGIX_USER);
	}

	protected function doAction() {
		$type = $this->getInput('type');
		$form = CWidgetConfig::getForm($type, $this->getInput('fields', '{}'));
		// Transforms corrupted data to default values.
		$form->validate();

		$this->setResponse(new CControllerResponseData(['main_block' => CJs::encodeJson([
			'configuration' => CWidgetConfig::getConfiguration(
				$type, $form->getFieldsData(), $this->getInput('view_mode')
			)
		])]));
	}
}
