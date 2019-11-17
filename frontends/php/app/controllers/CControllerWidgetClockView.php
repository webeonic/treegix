<?php



class CControllerWidgetClockView extends CControllerWidget {

	public function __construct() {
		parent::__construct();

		$this->setType(WIDGET_CLOCK);
		$this->setValidationRules([
			'name' => 'string',
			'fields' => 'json'
		]);
	}

	protected function doAction() {
		$fields = $this->getForm()->getFieldsData();

		$time = null;
		$name = $this->getDefaultHeader();
		$time_zone_string = null;
		$time_zone_offset = null;
		$error = null;
		$critical_error = null;

		switch ($fields['time_type']) {
			case TIME_TYPE_HOST:
				$items = API::Item()->get([
					'output' => ['itemid', 'value_type'],
					'selectHosts' => ['name'],
					'itemids' => $fields['itemid'],
					'webitems' => true
				]);

				if ($items) {
					$item = $items[0];
					$name = $item['hosts'][0]['name'];
					unset($items, $item['hosts']);

					$last_value = Manager::History()->getLastValues([$item]);

					if ($last_value) {
						$last_value = $last_value[$item['itemid']][0];

						try {
							$now = new DateTime($last_value['value']);

							$time_zone_string = _s('GMT%1$s', $now->format('P'));
							$time_zone_offset = $now->format('Z');

							$time = time() - ($last_value['clock'] - $now->getTimestamp());
						}
						catch (Exception $e) {
							$error = _('Incorrect data.');
						}
					}
					else {
						$error = _('No data.');
					}
				}
				else {
					$critical_error = _('No permissions to referred object or it does not exist!');
				}
				break;

			case TIME_TYPE_SERVER:
				$name = _('Server');

				$now = new DateTime();
				$time = $now->getTimestamp();
				$time_zone_string = _s('GMT%1$s', $now->format('P'));
				$time_zone_offset = $now->format('Z');
				break;

			default:
				$name = _('Local');
				break;
		}

		$this->setResponse(new CControllerResponseData([
			'name' => $this->getInput('name', $name),
			'clock' => [
				'time' => $time,
				'time_zone_string' => $time_zone_string,
				'time_zone_offset' => $time_zone_offset,
				'error' => $error,
				'critical_error' => $critical_error
			],
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		]));
	}
}
