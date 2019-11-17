<?php



function getImageByIdent($ident) {
	zbx_value2array($ident);

	if (!array_key_exists('name', $ident) || $ident['name'] === '') {
		return 0;
	}

	static $images;

	if ($images === null) {
		$images = [];

		$dbImages = API::Image()->get([
			'output' => ['imageid', 'name']
		]);

		foreach ($dbImages as $image) {
			if (!isset($images[$image['name']])) {
				$images[$image['name']] = [];
			}

			$images[$image['name']][] = $image;
		}
	}

	$ident['name'] = trim($ident['name'], ' ');

	return isset($images[$ident['name']]) ? reset($images[$ident['name']]) : 0;
}
