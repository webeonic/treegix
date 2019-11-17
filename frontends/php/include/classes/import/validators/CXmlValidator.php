<?php



/**
 * Base XML validation class.
 */
class CXmlValidator {

	/**
	 * Accepted versions.
	 *
	 * @var CXmlValidator[]
	 */
	protected $versionValidators = [];

	public function __construct() {
		$this->versionValidators = [
			'1.0' => 'C10XmlValidator',
			'2.0' => 'C20XmlValidator',
			'3.0' => 'C30XmlValidator',
			'3.2' => 'C32XmlValidator',
			'3.4' => 'C34XmlValidator',
			'4.0' => 'C40XmlValidator',
			'4.2' => 'C42XmlValidator',
			'4.4' => 'C44XmlValidator'
		];
	}

	/**
	 * Base validation function.
	 *
	 * @param array  $data    Import data.
	 * @param string $format  Format of import source.
	 *
	 * @throws Exception if $data does not correspond to validation rules.
	 *
	 * @return array  Validator does some manipulations for the incoming data. For example, converts empty tags to an
	 *                array, if desired. Converted array is returned.
	 */
	public function validate(array $data, $format) {
		$rules = ['type' => XML_ARRAY, 'rules' => [
			'treegix_export' => ['type' => XML_ARRAY | XML_REQUIRED, 'check_unexpected' => false, 'rules' => [
				'version' => ['type' => XML_STRING | XML_REQUIRED]
			]]
		]];

		$data = (new CXmlValidatorGeneral($rules, $format))->validate($data, '/');
		$version = $data['treegix_export']['version'];

		if (!array_key_exists($version, $this->versionValidators)) {
			throw new Exception(
				_s('Invalid tag "%1$s": %2$s.', '/treegix_export/version', _('unsupported version number'))
			);
		}

		$data['treegix_export'] = (new $this->versionValidators[$version]($format))
			->validate($data['treegix_export'], '/treegix_export');

		return $data;
	}
}
