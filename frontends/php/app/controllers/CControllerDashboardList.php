<?php



/**
 * controller dashboard list
 *
 */
class CControllerDashboardList extends CControllerDashboardAbstract {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		$fields = [
			'sort' =>		'in name',
			'sortorder' =>	'in '.TRX_SORT_DOWN.','.TRX_SORT_UP,
			'uncheck' =>	'in 1'
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
		CProfile::delete('web.dashbrd.dashboardid');
		CProfile::update('web.dashbrd.list_was_opened', 1, PROFILE_TYPE_INT);

		$sort_field = $this->getInput('sort', CProfile::get('web.dashbrd.list.sort', 'name'));
		$sort_order = $this->getInput('sortorder', CProfile::get('web.dashbrd.list.sortorder', TRX_SORT_UP));

		CProfile::update('web.dashbrd.list.sort', $sort_field, PROFILE_TYPE_STR);
		CProfile::update('web.dashbrd.list.sortorder', $sort_order, PROFILE_TYPE_STR);

		$config = select_config();

		$data = [
			'uncheck' => $this->hasInput('uncheck'),
			'sort' => $sort_field,
			'sortorder' => $sort_order
		];

		// list of dashboards
		$data['dashboards'] = API::Dashboard()->get([
			'output' => ['dashboardid', 'name'],
			'limit' => $config['search_limit'] + 1,
			'preservekeys' => true
		]);

		// sorting & paging
		order_result($data['dashboards'], $sort_field, $sort_order);

		$url = (new CUrl('treegix.php'))->setArgument('action', 'dashboard.list');

		$data['paging'] = getPagingLine($data['dashboards'], $sort_order, $url);

		if ($data['dashboards']) {
			$this->prepareEditableFlag($data['dashboards']);
		}

		CView::$has_web_layout_mode = true;

		$response = new CControllerResponseData($data);
		$response->setTitle(_('Dashboards'));
		$this->setResponse($response);
	}
}
