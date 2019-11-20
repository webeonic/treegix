<?php



require_once dirname(__FILE__).'/include/config.inc.php';
require_once dirname(__FILE__).'/include/maps.inc.php';

$page['title'] = _('Map');
$page['file'] = 'map.php';
$page['type'] = PAGE_TYPE_JSON;

require_once dirname(__FILE__).'/include/page_header.php';

$severity_min = getRequest('severity_min');
if (!trx_ctype_digit($severity_min)) {
	$severity_min = null;
}
$map_data = CMapHelper::get(getRequest('sysmapid'), ['severity_min' => $severity_min]);

if (hasRequest('uniqueid')) {
	// Rewrite actions to force Submaps be opened in same widget, instead of separate window.
	foreach ($map_data['elements'] as &$element) {
		$actions = CJs::decodeJson($element['actions']);
		$actions['data']['widget_uniqueid'] = getRequest('uniqueid');
		$element['actions'] = CJs::encodeJson($actions);
	}
	unset($element);
}

// No need to get all data.
$options = [
	'mapid' => $map_data['id'],
	'canvas' => $map_data['canvas'],
	'background' => $map_data['background'],
	'elements' => $map_data['elements'],
	'links' => $map_data['links'],
	'shapes' => $map_data['shapes'],
	'aria_label' => $map_data['aria_label'],
	'label_location' => $map_data['label_location'],
	'timestamp' => $map_data['timestamp']
];

if ($map_data['id'] == -1) {
	$options['timestamp'] = null;
}

echo CJs::encodeJson($options);

require_once dirname(__FILE__).'/include/page_footer.php';
