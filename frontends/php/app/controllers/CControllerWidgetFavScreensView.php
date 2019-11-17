<?php



require_once dirname(__FILE__).'/../../include/blocks.inc.php';

class CControllerWidgetFavScreensView extends CControllerWidget {

	public function __construct() {
		parent::__construct();

		$this->setType(WIDGET_FAV_SCREENS);
		$this->setValidationRules([
			'name' => 'string',
			'fields' => 'json'
		]);
	}

	protected function doAction() {
		$screens = [];
		$ids = ['screenid' => [], 'slideshowid' => []];

		foreach (CFavorite::get('web.favorite.screenids') as $favourite) {
			$ids[$favourite['source']][$favourite['value']] = true;
		}

		if ($ids['screenid']) {
			$db_screens = API::Screen()->get([
				'output' => ['screenid', 'name'],
				'screenids' => array_keys($ids['screenid'])
			]);

			foreach ($db_screens as $db_screen) {
				$screens[] = [
					'screenid' => $db_screen['screenid'],
					'label' => $db_screen['name'],
					'slideshow' => false
				];
			}
		}

		if ($ids['slideshowid']) {
			foreach ($ids['slideshowid'] as $slideshowid) {
				if (slideshow_accessible($slideshowid, PERM_READ)) {
					$db_slideshow = get_slideshow_by_slideshowid($slideshowid, PERM_READ);

					if ($db_slideshow) {
						$screens[] = [
							'slideshowid' => $db_slideshow['slideshowid'],
							'label' => $db_slideshow['name'],
							'slideshow' => true
						];
					}
				}
			}
		}

		CArrayHelper::sort($screens, ['label']);

		$this->setResponse(new CControllerResponseData([
			'name' => $this->getInput('name', CWidgetConfig::getKnownWidgetTypes()[WIDGET_FAV_SCREENS]),
			'screens' => $screens,
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		]));
	}
}
