<?php



class CControllerWidgetActionLogView extends CControllerWidget {

	public function __construct() {
		parent::__construct();

		$this->setType(WIDGET_ACTION_LOG);
		$this->setValidationRules([
			'name' => 'string',
			'fields' => 'json'
		]);
	}

	protected function doAction() {
		$fields = $this->getForm()->getFieldsData();

		list($sortfield, $sortorder) = self::getSorting($fields['sort_triggers']);
		$alerts = $this->getAlerts($sortfield, $sortorder, $fields['show_lines']);
		$db_users = $this->getDbUsers($alerts);

		$actions = API::Action()->get([
			'output' => ['actionid', 'name'],
			'actionids' => array_unique(trx_objectValues($alerts, 'actionid')),
			'preservekeys' => true
		]);

		$this->setResponse(new CControllerResponseData([
			'name' => $this->getInput('name', $this->getDefaultHeader()),
			'actions' => $actions,
			'alerts'  => $alerts,
			'db_users' => $db_users,
			'sortfield' => $sortfield,
			'sortorder' => $sortorder,
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		]));
	}

	/**
	 * Get alerts.
	 *
	 * @param string $sortfield
	 * @param string $sortorder
	 * @param int    $show_lines
	 *
	 * @return array
	 */
	private function getAlerts($sortfield, $sortorder, $show_lines)	{
		$alerts = API::Alert()->get([
			'output' => ['clock', 'sendto', 'subject', 'message', 'status', 'retries', 'error', 'userid', 'actionid',
				'mediatypeid', 'alerttype'
			],
			'selectMediatypes' => ['name', 'maxattempts'],
			'filter' => [
				'alerttype' => ALERT_TYPE_MESSAGE
			],
			'sortfield' => $sortfield,
			'sortorder' => $sortorder,
			'limit' => $show_lines
		]);

		foreach ($alerts as &$alert) {
			$alert['description'] = '';
			if ($alert['mediatypeid'] != 0 && array_key_exists(0, $alert['mediatypes'])) {
				$alert['description'] = $alert['mediatypes'][0]['name'];
				$alert['maxattempts'] = $alert['mediatypes'][0]['maxattempts'];
			}
			unset($alert['mediatypes']);

			$alert['action_type'] = TRX_EVENT_HISTORY_ALERT;
		}
		unset($alert);

		return $alerts;
	}

	/**
	 * Get users.
	 *
	 * @param array $alerts
	 *
	 * @return array
	 */
	private function getDbUsers(array $alerts) {
		$userids = [];

		foreach ($alerts as $alert) {
			$userids[$alert['userid']] = true;
		}
		unset($userids[0]);

		return $userids
			? API::User()->get([
				'output' => ['userid', 'alias', 'name', 'surname'],
				'userids' => array_keys($userids),
				'preservekeys' => true
			])
			: [];
	}

	/**
	 * Get sorting.
	 *
	 * @param int $sort_triggers
	 *
	 * @static
	 *
	 * @return array
	 */
	private static function getSorting($sort_triggers) {
		switch ($sort_triggers) {
			case SCREEN_SORT_TRIGGERS_TIME_ASC:
				return ['clock', TRX_SORT_UP];

			case SCREEN_SORT_TRIGGERS_TIME_DESC:
			default:
				return ['clock', TRX_SORT_DOWN];

			case SCREEN_SORT_TRIGGERS_TYPE_ASC:
				return ['mediatypeid', TRX_SORT_UP];

			case SCREEN_SORT_TRIGGERS_TYPE_DESC:
				return ['mediatypeid', TRX_SORT_DOWN];

			case SCREEN_SORT_TRIGGERS_STATUS_ASC:
				return ['status', TRX_SORT_UP];

			case SCREEN_SORT_TRIGGERS_STATUS_DESC:
				return ['status', TRX_SORT_DOWN];

			case SCREEN_SORT_TRIGGERS_RECIPIENT_ASC:
				return ['sendto', TRX_SORT_UP];

			case SCREEN_SORT_TRIGGERS_RECIPIENT_DESC:
				return ['sendto', TRX_SORT_DOWN];
		}
	}
}
