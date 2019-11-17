<?php



/**
 * A class for creating a storing instances of DB objects managers.
 */
class Manager extends CRegistryFactory {

	/**
	 * An instance of the manager factory.
	 *
	 * @var Manager
	 */
	protected static $instance;

	/**
	 * Returns an instance of the manager factory object.
	 *
	 * @return Manager
	 */
	public static function getInstance() {
		if (!self::$instance) {
			$class = __CLASS__;
			self::$instance = new $class([
				'application' => 'CApplicationManager',
				'history' => 'CHistoryManager',
				'httptest' => 'CHttpTestManager'
			]);
		}

		return self::$instance;
	}

	/**
	 * @return CApplicationManager
	 */
	public static function Application() {
		return self::getInstance()->getObject('application');
	}

	/**
	 * @return CHistoryManager
	 */
	public static function History() {
		return self::getInstance()->getObject('history');
	}

	/**
	 * @return CHttpTestManager
	 */
	public static function HttpTest() {
		return self::getInstance()->getObject('httptest');
	}
}
