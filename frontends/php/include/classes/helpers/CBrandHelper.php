<?php


/**
 * A class for Treegix re-branding.
 */
class CBrandHelper {

	const BRAND_CONFIG_FILE_PATH = '/../../../local/conf/brand.conf.php';

	/**
	 * Brand configuration array.
	 *
	 * @var array
	 */
	private static $config = [];

	/**
	 * Lazy configuration loading.
	 */
	private static function loadConfig() {
		if (!self::$config) {
			$config_file_path = realpath(dirname(__FILE__).self::BRAND_CONFIG_FILE_PATH);

			if (file_exists($config_file_path)) {
				self::$config = include $config_file_path;
				if (is_array(self::$config)) {
					self::$config['IS_REBRANDED'] = true;
				}
				else {
					self::$config = [];
				}
			}
		}
	}

	/**
	 * Get value by key from configuration (load configuration if need).
	 *
	 * @param string $key      configuration key
	 * @param mixed  $default  default value
	 *
	 * @return mixed
	 */
	private static function getValue($key, $default = false) {
		self::loadConfig();

		return (array_key_exists($key, self::$config) ? self::$config[$key] : $default);
	}

	/**
	 * Is branding active ?
	 *
	 * @return boolean
	 */
	public static function isRebranded() {
		return self::getValue('IS_REBRANDED');
	}


	/**
	 * Get logo style.
	 *
	 * @return string|null
	 */
	public static function getLogoStyle() {
		$logo = self::getValue('BRAND_LOGO', null);
		return ($logo !== null)
			? 'background: url("'.$logo.'") no-repeat center center; background-size: contain;'
			: null;
	}

}
