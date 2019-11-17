<?php



/**
 * Get mapping for value.
 *
 * @param int|float|string $value		Value that mapping should be applied to.
 * @param string           $valuemapid	Value map ID which should be used.
 *
 * @return string|bool     If there is no mapping return false, return mapped value otherwise.
 */
function getMappedValue($value, $valuemapid) {
	static $valuemaps = [];

	if ($valuemapid == 0) {
		return false;
	}

	if (!array_key_exists($valuemapid, $valuemaps)) {
		$valuemaps[$valuemapid] = [];

		$db_valuemaps = API::ValueMap()->get([
			'output' => [],
			'selectMappings' => ['value', 'newvalue'],
			'valuemapids' => [$valuemapid]
		]);

		if ($db_valuemaps) {
			foreach ($db_valuemaps[0]['mappings'] as $mapping) {
				$valuemaps[$valuemapid][$mapping['value']] = $mapping['newvalue'];
			}
		}
	}

	return array_key_exists((string)$value, $valuemaps[$valuemapid]) ? $valuemaps[$valuemapid][(string)$value] : false;
}

/**
 * Apply value mapping to value.
 * If value map or mapping is not found, unchanged value is returned,
 * otherwise mapped value returned in format: "<mapped_value> (<initial_value>)".
 *
 * @param string $value			Value that mapping should be applied to.
 * @param string $valuemapid	Value map ID which should be used.
 *
 * @return string
 */
function applyValueMap($value, $valuemapid) {
	$newvalue = getMappedValue($value, $valuemapid);

	return ($newvalue === false) ? $value : $newvalue.' ('.$value.')';
}
