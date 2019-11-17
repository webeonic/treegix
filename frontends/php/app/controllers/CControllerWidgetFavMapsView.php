<?php



require_once dirname(__FILE__).'/../../include/blocks.inc.php';

class CControllerWidgetFavMapsView extends CControllerWidget {

	public function __construct() {
		parent::__construct();

		$this->setType(WIDGET_FAV_MAPS);
		$this->setValidationRules([
			'name' => 'string',
			'fields' => 'json'
		]);
	}

	protected function doAction() {
		$maps = [];
		$mapids = [];

		foreach (CFavorite::get('web.favorite.sysmapids') as $favourite) {
			$mapids[$favourite['value']] = true;
		}

		if ($mapids) {
			$db_maps = API::Map()->get([
				'output' => ['sysmapid', 'name'],
				'sysmapids' => array_keys($mapids)
			]);

			foreach ($db_maps as $db_map) {
				$maps[] = [
					'sysmapid' => $db_map['sysmapid'],
					'label' => $db_map['name']
				];
			}
		}

		CArrayHelper::sort($maps, ['label']);

		$this->setResponse(new CControllerResponseData([
			'name' => $this->getInput('name', CWidgetConfig::getKnownWidgetTypes()[WIDGET_FAV_MAPS]),
			'maps' => $maps,
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		]));
	}
}
