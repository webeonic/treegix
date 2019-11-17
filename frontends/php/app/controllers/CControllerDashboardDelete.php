<?php



/**
 * Controller to delete dashboards.
 */
class CControllerDashboardDelete extends CController {

	protected function checkInput() {
		$fields = [
			'dashboardids' =>	'required|array_db dashboard.dashboardid'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		return true;
	}

	protected function doAction() {
		$dashboardids = $this->getInput('dashboardids');

		$result = (bool) API::Dashboard()->delete($dashboardids);

		$deleted = count($dashboardids);

		$url = (new CUrl('treegix.php'))
			->setArgument('action', 'dashboard.list')
			->setArgument('uncheck', '1');

		$response = new CControllerResponseRedirect($url->getUrl());

		if ($result) {
			$response->setMessageOk(_n('Dashboard deleted', 'Dashboards deleted', $deleted));
		}
		else {
			$response->setMessageError(_n('Cannot delete dashboard', 'Cannot delete dashboards', $deleted));
		}

		$this->setResponse($response);
	}
}
