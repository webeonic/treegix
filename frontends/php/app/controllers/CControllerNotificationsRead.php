<?php



class CControllerNotificationsRead extends CController {

	protected function checkInput() {
		$fields = [
			'ids' => 'array_db events.eventid|required'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$data = CJs::encodeJson(['error' => true]);
			$this->setResponse(new CControllerResponseData(['main_block' => $data]));
		}

		return $ret;
	}

	protected function checkPermissions() {
		return (!CWebUser::isGuest() && $this->getUserType() >= USER_TYPE_TREEGIX_USER);
	}

	protected function doAction() {
		$msg_settings = getMessageSettings();

		$events = API::Event()->get([
			'output' => ['clock', 'r_eventid'],
			'eventids' => $this->input['ids'],
			'preservekeys' => true
		]);

		$recovery_eventids = array_filter(zbx_objectValues($events, 'r_eventid'));
		if ($recovery_eventids) {
			$events += API::Event()->get([
				'output' => ['clock'],
				'eventids' => $recovery_eventids,
				'preservekeys' => true
			]);
		}

		CArrayHelper::sort($events, [
			['field' => 'clock', 'order' => ZBX_SORT_DOWN]
		]);

		$last_event = reset($events);

		$msg_settings['last.clock'] = $last_event['clock'] + 1;
		updateMessageSettings($msg_settings);

		$data = CJs::encodeJson(['ids' => array_keys($events)]);
		$this->setResponse(new CControllerResponseData(['main_block' => $data]));
	}
}
