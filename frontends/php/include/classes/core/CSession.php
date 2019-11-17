<?php



/**
 * Session wrapper, currently uses native PHP session.
 */
class CSession {

	/**
	 * Flag indicating if session is created.
	 *
	 * @var CSession
	 */
	protected static $session_created = false;

	/**
	 * Initialize session.
	 * Set cookie path to path to current URI without file.
	 *
	 * @throw Exception if cannot start session
	 */
	public static function start() {
		if (!self::$session_created) {
			ob_start();
			session_set_cookie_params(0, self::getDefaultCookiePath(), null, HTTPS, true);

			if (!session_start()) {
				throw new Exception('Cannot start session.');
			}

			session_write_close();
			ob_flush();
			self::$session_created = true;
		}
	}

	/**
	 * Clears and implicitly flushes session.
	 */
	public static function clear() {
		self::open();
		$_SESSION = [];
		self::close();
	}

	/**
	 * Sets session value by key.
	 *
	 * @param mixed $key
	 * @param mixed $value
	 */
	public static function setValue($key, $value) {
		self::open();
		$_SESSION[$key] = $value;
		self::close();
	}

	/**
	 * Determines default cookie path.
	 *
	 * @return string
	 */
	public static function getDefaultCookiePath() {
		$path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);

		return rtrim(substr($path, 0, strrpos($path, '/')), '/');
	}

	/**
	 * Returns value stored in session.
	 *
	 * @param mixed $key
	 *
	 * @return mixed|null
	 */
	public static function getValue($key) {
		self::open();
		$result = array_key_exists($key, $_SESSION) ? $_SESSION[$key] : null;
		self::close();

		return $result;
	}

	/**
	 * Checks if session value exists (isset() calls).
	 *
	 * @param mixed $key
	 *
	 * @return bool
	 */
	public static function keyExists($key) {
		self::open();
		$result = array_key_exists($key, $_SESSION);
		self::close();

		return $result;
	}

	/**
	 * Unsets session value (unset() calls).
	 *
	 * @param array $keys
	 */
	public static function unsetValue(array $keys) {
		self::open();
		foreach ($keys as $key) {
			unset($_SESSION[$key]);
		}
		self::close();
	}

	/**
	 * Destroy session
	 */
	public static function destroy() {
		self::open();
		session_destroy();
		self::close();
	}

	/**
	 * Open session for writing
	 */
	private static function open() {
		static $options = [];

		session_start($options);

		if (!$options) {
			$options = ['use_cookies' => 0];
		}
	}

	/**
	 * Close session for writing
	 */
	private static function close() {
		session_write_close();
	}
}
