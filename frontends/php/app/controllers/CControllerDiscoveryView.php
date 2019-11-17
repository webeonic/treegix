<?php



class CControllerDiscoveryView extends CController {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		$fields = [
			'sort' =>				'in ip',
			'filter_set' =>			'in 1',
			'filter_rst' =>			'in 1',
			'filter_druleids' =>	'array_id',
			'sortorder' =>			'in '.TRX_SORT_DOWN.','.TRX_SORT_UP
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		return ($this->getUserType() >= USER_TYPE_TREEGIX_ADMIN);
	}

	protected function doAction() {
		$sortField = $this->getInput('sort', CProfile::get('web.discovery.php.sort', 'ip'));
		$sortOrder = $this->getInput('sortorder', CProfile::get('web.discovery.php.sortorder', TRX_SORT_UP));

		CProfile::update('web.discovery.php.sort', $sortField, PROFILE_TYPE_STR);
		CProfile::update('web.discovery.php.sortorder', $sortOrder, PROFILE_TYPE_STR);

		// filter
		if (hasRequest('filter_set')) {
			CProfile::updateArray('web.discovery.filter.druleids', $this->getInput('filter_druleids', []),
				PROFILE_TYPE_ID
			);
		}
		elseif (hasRequest('filter_rst')) {
			CProfile::deleteIdx('web.discovery.filter.druleids');
		}

		$filter_druleids = CProfile::getArray('web.discovery.filter.druleids', []);

		/*
		 * Display
		 */
		$data = [
			'sort' => $sortField,
			'sortorder' => $sortOrder,
			'filter' => [
				'druleids' => $filter_druleids,
				'drules' => $filter_druleids
					? CArrayHelper::renameObjectsKeys(API::DRule()->get([
						'output' => ['druleid', 'name'],
						'druleids' => $filter_druleids,
						'filter' => ['status' => DRULE_STATUS_ACTIVE]
					]), ['druleid' => 'id'])
					: [],
			],
			'profileIdx' => 'web.discovery.filter',
			'active_tab' => CProfile::get('web.discovery.filter.active', 1)
		];

		CView::$has_web_layout_mode = true;

		$response = new CControllerResponseData($data);
		$response->setTitle(_('Status of discovery'));
		$this->setResponse($response);
	}
}
