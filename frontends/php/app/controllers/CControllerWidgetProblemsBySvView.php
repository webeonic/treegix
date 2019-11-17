<?php



require_once dirname(__FILE__).'/../../include/blocks.inc.php';

class CControllerWidgetProblemsBySvView extends CControllerWidget {

	public function __construct() {
		parent::__construct();

		$this->setType(WIDGET_PROBLEMS_BY_SV);
		$this->setValidationRules([
			'name' => 'string',
			'fields' => 'json'
		]);
	}

	protected function doAction() {
		$fields = $this->getForm()->getFieldsData();

		$config = select_config();

		$filter = [
			'groupids' => getSubGroups($fields['groupids']),
			'exclude_groupids' => getSubGroups($fields['exclude_groupids']),
			'hostids' => $fields['hostids'],
			'problem' => $fields['problem'],
			'severities' => $fields['severities'],
			'show_type' => $fields['show_type'],
			'layout' => $fields['layout'],
			'show_suppressed' => $fields['show_suppressed'],
			'hide_empty_groups' => $fields['hide_empty_groups'],
			'show_opdata' => $fields['show_opdata'],
			'ext_ack' => $fields['ext_ack'],
			'show_timeline' => $fields['show_timeline']
		];

		$data = getSystemStatusData($filter);

		$severity_names = [
			'severity_name_0' => $config['severity_name_0'],
			'severity_name_1' => $config['severity_name_1'],
			'severity_name_2' => $config['severity_name_2'],
			'severity_name_3' => $config['severity_name_3'],
			'severity_name_4' => $config['severity_name_4'],
			'severity_name_5' => $config['severity_name_5']
		];

		if ($filter['show_type'] == WIDGET_PROBLEMS_BY_SV_SHOW_TOTALS) {
			$data['groups'] = getSystemStatusTotals($data, $severity_names);
		}

		$this->setResponse(new CControllerResponseData([
			'name' => $this->getInput('name', $this->getDefaultHeader()),
			'data' => $data,
			'severity_names' => $severity_names,
			'filter' => $filter,
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		]));
	}
}
