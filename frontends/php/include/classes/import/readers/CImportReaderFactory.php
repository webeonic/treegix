<?php



class CImportReaderFactory {

	const XML = 'xml';
	const JSON = 'json';

	/**
	 * Get reader class for required format.
	 *
	 * @static
	 * @throws Exception
	 *
	 * @param string $format
	 *
	 * @return CImportReader
	 */
	public static function getReader($format) {
		switch ($format) {
			case 'xml':
				return new CXmlImportReader();
			case 'json':
				return new CJsonImportReader();
			default:
				throw new Exception(_s('Unsupported import format "%1$s".', $format));
		}
	}

	/**
	 * Converts file extension to associated import format.
	 *
	 * @static
	 * @throws Exception
	 *
	 * @param string $ext
	 *
	 * @return string
	 */
	public static function fileExt2ImportFormat($ext) {
		switch ($ext) {
			case 'xml':
				return CImportReaderFactory::XML;
			case 'json':
				return CImportReaderFactory::JSON;
			default:
				throw new Exception(_s('Unsupported import file extension "%1$s".', $ext));
		}

	}
}
