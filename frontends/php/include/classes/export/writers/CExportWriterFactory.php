<?php



class CExportWriterFactory {

	const XML = 'xml';
	const JSON = 'json';

	/**
	 * Get the writer object for specified type.
	 *
	 * @static
	 * @throws Exception
	 *
	 * @param string $type
	 *
	 * @return CExportWriter
	 */
	public static function getWriter($type) {
		switch ($type) {
			case self::XML:
				return new CXmlExportWriter();

			case self::JSON:
				return new CJsonExportWriter();

			default:
				throw new Exception('Incorrect export writer type.');
		}
	}
}
