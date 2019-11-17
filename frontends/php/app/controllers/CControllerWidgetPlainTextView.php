<?php



/**
 * Class for Dashboard Plain-text widget view.
 */
class CControllerWidgetPlainTextView extends CControllerWidget {

	public function __construct() {
		parent::__construct();

		$this->setType(WIDGET_PLAIN_TEXT);
		$this->setValidationRules([
			'name' => 'string',
			'fields' => 'json',
			'dynamic_hostid' => 'db hosts.hostid'
		]);
	}

	protected function doAction() {
		$fields = $this->getForm()->getFieldsData();
		$error = null;

		$dynamic_widget_name = $this->getDefaultHeader();
		$same_host = true;
		$items = [];
		$histories = [];

		if ($fields['itemids']) {
			$items = API::Item()->get([
				'output' => ['itemid', 'hostid', 'name', 'key_', 'value_type', 'units', 'valuemapid'],
				'selectHosts' => ['name'],
				'itemids' => $fields['itemids'],
				'webitems' => true,
				'preservekeys' => true
			]);

			$dynamic_hostid = $this->getInput('dynamic_hostid', 0);

			$keys = [];
			foreach ($items as $item) {
				$keys[$item['key_']] = true;
			}

			if ($items && $fields['dynamic'] && $dynamic_hostid) {
				$items = API::Item()->get([
					'output' => ['itemid', 'hostid', 'name', 'key_', 'value_type', 'units', 'valuemapid'],
					'selectHosts' => ['name'],
					'filter' => [
						'hostid' => $dynamic_hostid,
						'key_' => array_keys($keys)
					],
					'webitems' => true,
					'preservekeys' => true
				]);
			}
		}

		if (!$items) {
			$error = _('No permissions to referred object or it does not exist!');
		}
		else {
			// macros
			$items = CMacrosResolverHelper::resolveItemNames($items);

			$histories = Manager::History()->getLastValues($items, $fields['show_lines']);

			if ($histories) {
				$histories = call_user_func_array('array_merge', $histories);

				foreach ($histories as &$history) {
					$history['value'] = formatHistoryValue($history['value'], $items[$history['itemid']], false);
					$history['value'] = $fields['show_as_html']
						? new CJsScript($history['value'])
						: new CPre($history['value']);
				}
				unset($history);
			}

			CArrayHelper::sort($histories, [
				['field' => 'clock', 'order' => TRX_SORT_DOWN],
				['field' => 'ns', 'order' => TRX_SORT_DOWN]
			]);

			$host_name = '';
			foreach ($items as $item) {
				if ($host_name === '') {
					$host_name = $item['hosts'][0]['name'];
				}
				elseif ($host_name !== $item['hosts'][0]['name']) {
					$same_host = false;
				}
			}

			$items_count = count($items);
			if ($items_count == 1) {
				$item = reset($items);
				$dynamic_widget_name = $host_name.NAME_DELIMITER.$item['name_expanded'];
			}
			elseif ($same_host && $items_count > 1) {
				$dynamic_widget_name = $host_name.NAME_DELIMITER._n('%1$s item', '%1$s items', $items_count);
			}
		}

		$this->setResponse(new CControllerResponseData([
			'name' => $this->getInput('name', $dynamic_widget_name),
			'items' => $items,
			'histories' => $histories,
			'style' => $fields['style'],
			'same_host' => $same_host,
			'show_lines' => $fields['show_lines'],
			'error' => $error,
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		]));
	}
}
