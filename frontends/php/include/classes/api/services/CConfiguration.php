<?php



/**
 * Class containing methods for operations with configuration.
 */
class CConfiguration extends CApiService {

	/**
	 * @param array $params
	 *
	 * @return string
	 */
	public function export(array $params) {
		$api_input_rules = ['type' => API_OBJECT, 'fields' => [
			'format' =>		['type' => API_STRING_UTF8, 'flags' => API_REQUIRED, 'in' => implode(',', [CExportWriterFactory::XML, CExportWriterFactory::JSON])],
			'options' =>	['type' => API_OBJECT, 'flags' => API_REQUIRED, 'fields' => [
				'groups' =>		['type' => API_IDS],
				'hosts' =>		['type' => API_IDS],
				'images' =>		['type' => API_IDS],
				'maps' =>		['type' => API_IDS],
				'mediaTypes' =>	['type' => API_IDS],
				'screens' =>	['type' => API_IDS],
				'templates' =>	['type' => API_IDS],
				'valueMaps' =>	['type' => API_IDS]
			]]
		]];
		if (!CApiInputValidator::validate($api_input_rules, $params, '/', $error)) {
			self::exception(TRX_API_ERROR_PARAMETERS, $error);
		}

		$export = new CConfigurationExport($params['options']);
		$export->setBuilder(new CConfigurationExportBuilder());
		$writer = CExportWriterFactory::getWriter($params['format']);
		$writer->formatOutput(false);
		$export->setWriter($writer);

		$export_data = $export->export();

		if ($export_data === false) {
			self::exception(TRX_API_ERROR_PERMISSIONS, _('No permissions to referred object or it does not exist!'));
		}

		return $export_data;
	}

	/**
	 * @param array $params
	 *
	 * @return bool
	 */
	public function import($params) {
		$api_input_rules = ['type' => API_OBJECT, 'fields' => [
			'format' =>				['type' => API_STRING_UTF8, 'flags' => API_REQUIRED, 'in' => implode(',', [CImportReaderFactory::XML, CImportReaderFactory::JSON])],
			'source' =>				['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
			'rules' =>				['type' => API_OBJECT, 'flags' => API_REQUIRED, 'fields' => [
				'applications' =>		['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'deleteMissing' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'discoveryRules' =>		['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'updateExisting' =>		['type' => API_BOOLEAN, 'default' => false],
					'deleteMissing' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'graphs' =>				['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'updateExisting' =>		['type' => API_BOOLEAN, 'default' => false],
					'deleteMissing' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'groups' =>				['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'hosts' =>				['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'updateExisting' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'httptests' =>			['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'updateExisting' =>		['type' => API_BOOLEAN, 'default' => false],
					'deleteMissing' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'images' =>				['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'updateExisting' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'items' =>				['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'updateExisting' =>		['type' => API_BOOLEAN, 'default' => false],
					'deleteMissing' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'maps' =>				['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'updateExisting' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'mediaTypes' =>			['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'updateExisting' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'screens' =>			['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'updateExisting' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'templateLinkage' =>	['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'templates' =>			['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'updateExisting' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'templateScreens' =>	['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'updateExisting' =>		['type' => API_BOOLEAN, 'default' => false],
					'deleteMissing' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'triggers' =>			['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'updateExisting' =>		['type' => API_BOOLEAN, 'default' => false],
					'deleteMissing' =>		['type' => API_BOOLEAN, 'default' => false]
				]],
				'valueMaps' =>			['type' => API_OBJECT, 'fields' => [
					'createMissing' =>		['type' => API_BOOLEAN, 'default' => false],
					'updateExisting' =>		['type' => API_BOOLEAN, 'default' => false]
				]]
			]]
		]];
		if (!CApiInputValidator::validate($api_input_rules, $params, '/', $error)) {
			self::exception(TRX_API_ERROR_PARAMETERS, $error);
		}

		$importReader = CImportReaderFactory::getReader($params['format']);
		$data = $importReader->read($params['source']);

		$importValidatorFactory = new CImportValidatorFactory($params['format']);
		$importConverterFactory = new CImportConverterFactory();

		$data = (new CXmlValidator)->validate($data, $params['format']);

		foreach (['1.0', '2.0', '3.0', '3.2', '3.4', '4.0', '4.2'] as $version) {
			if ($data['treegix_export']['version'] !== $version) {
				continue;
			}

			$data = $importConverterFactory
				->getObject($version)
				->convert($data);
			$data = (new CXmlValidator)->validate($data, $params['format']);
		}

		// Get schema for converters.
		$schema = $importValidatorFactory
			->getObject(TREEGIX_EXPORT_VERSION)
			->getSchema();

		// Convert human readable import constants to values Treegix API can work with.
		$data = (new CConstantImportConverter($schema))->convert($data);

		// Add default values in place of missed tags.
		$data = (new CDefaultImportConverter($schema))->convert($data);

		// Normalize array keys.
		$data = (new CArrayKeysImportConverter($schema))->convert($data);

		$adapter = new CImportDataAdapter();
		$adapter->load($data);

		$configurationImport = new CConfigurationImport(
			$params['rules'],
			new CImportReferencer(),
			new CImportedObjectContainer()
		);

		return $configurationImport->import($adapter);
	}
}
