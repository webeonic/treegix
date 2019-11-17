<?php



/**
 * Abstract class to keep common dashboard controller logic.
 */
abstract class CControllerDashboardAbstract extends CController {

	/**
	 * Prepare editable flag.
	 *
	 * @param array $dashboards  An associative array of the dashboards.
	 */
	protected function prepareEditableFlag(array &$dashboards) {
		$dashboards_rw = API::Dashboard()->get([
			'output' => [],
			'dashboardids' => array_keys($dashboards),
			'editable' => true,
			'preservekeys' => true
		]);

		foreach ($dashboards as $dashboardid => &$dashboard) {
			$dashboard['editable'] = array_key_exists($dashboardid, $dashboards_rw);
		}
		unset($dashboard);
	}
}
