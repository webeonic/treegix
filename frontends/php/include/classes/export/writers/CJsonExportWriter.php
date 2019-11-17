<?php



/**
 * Class for converting array with export data to JSON format.
 */
class CJsonExportWriter extends CExportWriter {

	/**
	 * Convert array with export data to JSON format.
	 *
	 * @param array $array
	 *
	 * @return string
	 */
	public function write(array $array) {
		return (new CJson())->encode($array, [], false, false);
	}
}
