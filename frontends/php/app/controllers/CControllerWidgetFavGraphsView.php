<?php



require_once dirname(__FILE__).'/../../include/blocks.inc.php';

class CControllerWidgetFavGraphsView extends CControllerWidget {

	public function __construct() {
		parent::__construct();

		$this->setType(WIDGET_FAV_GRAPHS);
		$this->setValidationRules([
			'name' => 'string',
			'fields' => 'json'
		]);
	}

	protected function doAction() {
		$graphs = [];
		$ids = ['graphid' => [], 'itemid' => []];

		foreach (CFavorite::get('web.favorite.graphids') as $favourite) {
			$ids[$favourite['source']][$favourite['value']] = true;
		}

		if ($ids['graphid']) {
			$db_graphs = API::Graph()->get([
				'output' => ['graphid', 'name'],
				'selectHosts' => ['hostid', 'name'],
				'expandName' => true,
				'graphids' => array_keys($ids['graphid'])
			]);

			foreach ($db_graphs as $db_graph) {
				$graphs[] = [
					'graphid' => $db_graph['graphid'],
					'label' => $db_graph['hosts'][0]['name'].NAME_DELIMITER.$db_graph['name'],
					'simple' => false
				];
			}
		}

		if ($ids['itemid']) {
			$db_items = API::Item()->get([
				'output' => ['itemid', 'hostid', 'name', 'key_'],
				'selectHosts' => ['hostid', 'name'],
				'itemids' => array_keys($ids['itemid']),
				'webitems' => true
			]);

			$db_items = CMacrosResolverHelper::resolveItemNames($db_items);

			foreach ($db_items as $db_item) {
				$graphs[] = [
					'itemid' => $db_item['itemid'],
					'label' => $db_item['hosts'][0]['name'].NAME_DELIMITER.$db_item['name_expanded'],
					'simple' => true
				];
			}
		}

		CArrayHelper::sort($graphs, ['label']);

		$this->setResponse(new CControllerResponseData([
			'name' => $this->getInput('name', $this->getDefaultHeader()),
			'graphs' => $graphs,
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		]));
	}
}
