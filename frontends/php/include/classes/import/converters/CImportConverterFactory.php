<?php



/**
 * Factory for creating import conversions.
 */
class CImportConverterFactory extends CRegistryFactory {

	public function __construct() {
		parent::__construct([
			'1.0' => 'C10ImportConverter',
			'2.0' => 'C20ImportConverter',
			'3.0' => 'C30ImportConverter',
			'3.2' => 'C32ImportConverter',
			'3.4' => 'C34ImportConverter',
			'4.0' => 'C40ImportConverter',
			'4.2' => 'C42ImportConverter'
		]);
	}
}
