<?php


/**
 * A helper class for working with JavaScript.
 */
class CJs {

	/**
	 * The object used to encode values in JSON.
	 *
	 * @var CJson
	 */
	protected static $json;

	/**
	 * Encodes the data as a JSON string to be used in JavaScript code.
	 *
	 * @static
	 *
	 * @param mixed $data
	 * @param bool  $forceObject force all arrays to objects
	 *
	 * @return mixed
	 */
	public static function encodeJson($data, $forceObject = false) {
		if (self::$json === null) {
			self::$json = new CJson();
		}

		return self::$json->encode($data, [], $forceObject);
	}

	/**
	 * Decodes JSON sting.
	 *
	 * @static
	 *
	 * @param string $data
	 * @param bool   $asArray get result as array instead of object
	 *
	 * @return mixed
	 */
	public static function decodeJson($data, $asArray = true) {
		if (self::$json === null) {
			self::$json = new CJson();
		}

		return self::$json->decode($data, $asArray);
	}
}
