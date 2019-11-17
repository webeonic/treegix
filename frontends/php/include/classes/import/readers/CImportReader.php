<?php



abstract class CImportReader {

	/**
	 * convert string with data in format supported by reader to php array.
	 *
	 * @abstract
	 *
	 * @param $string
	 *
	 * @return array
	 */
	abstract public function read($string);
}
