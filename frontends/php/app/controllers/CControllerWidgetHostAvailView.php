<?php
 


class CControllerWidgetHostAvailView extends CControllerWidget {

	public function __construct() {
		parent::__construct();

		$this->setType(WIDGET_HOST_AVAIL);
		$this->setValidationRules([
			'name' => 'string',
			'fields' => 'json',
		]);
	}

	protected function doAction() {
		$fields = $this->getForm()->getFieldsData();

		$type_fields = [
			INTERFACE_TYPE_AGENT => 'available',
			INTERFACE_TYPE_SNMP => 'snmp_available',
			INTERFACE_TYPE_JMX => 'jmx_available',
			INTERFACE_TYPE_IPMI => 'ipmi_available'
		];

		$interface_types = array_keys($type_fields);

		// Sanitize non-existing interface types.
		$fields['interface_type'] = array_values(array_intersect($interface_types, $fields['interface_type']));

		$groupids = $fields['groupids'] ? getSubGroups($fields['groupids']) : null;

		$hosts_types = $fields['interface_type'] ? $fields['interface_type'] : $interface_types;

		$hosts_total = array_fill_keys($interface_types, 0);
		$hosts_count = array_fill_keys($interface_types, [
			HOST_AVAILABLE_UNKNOWN => 0,
			HOST_AVAILABLE_TRUE => 0,
			HOST_AVAILABLE_FALSE => 0
		]);

		$db_hosts = API::Host()->get([
			'output' => ['available', 'snmp_available', 'jmx_available', 'ipmi_available'],
			'selectInterfaces' => ['interfaceid', 'type'],
			'groupids' => $groupids,
			'filter' => ($fields['maintenance'] == HOST_MAINTENANCE_STATUS_OFF)
				? ['status' => HOST_STATUS_MONITORED, 'maintenance_status' => HOST_MAINTENANCE_STATUS_OFF]
				: ['status' => HOST_STATUS_MONITORED]
		]);

		foreach ($db_hosts as $host) {
			$interfaces = [];
			foreach ($host['interfaces'] as $val) {
				$interfaces[$val['type']] = true;
			}

			foreach (array_keys($interfaces) as $type) {
				$hosts_count[$type][$host[$type_fields[$type]]]++;
				$hosts_total[$type]++;
			}
		}

		$this->setResponse(new CControllerResponseData([
			'name' => $this->getInput('name', $this->getDefaultHeader()),
			'layout' => $fields['layout'],
			'hosts_types' => $hosts_types,
			'hosts_count' => $hosts_count,
			'hosts_total' => $hosts_total,
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		]));
	}
}
