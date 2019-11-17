<?php
 


/**
 * The Treegix autoloader class.
 */
class CAutoloader {

	/**
	 * An array of directories, where the autoloader will look for the classes.
	 *
	 * @var array
	 */
	protected $includePaths = [];

	/**
	 * Initializes object with array of include paths.
	 *
	 * @param array $includePaths absolute paths
	 */
	public function __construct(array $includePaths) {
		$this->includePaths = $includePaths;
	}

	/**
	 * Add "loadClass" method as an autoload handler.
	 *
	 * @return bool
	 */
	public function register() {
		return spl_autoload_register([$this, 'loadClass']);
	}

	/**
	 * Attempts to find and load the given class.
	 *
	 * @param $className
	 */
	protected function loadClass($className) {
		if ($classFile = $this->findClassFile($className)) {
			require $classFile;
		}
	}

	/**
	 * Attempts to find corresponding file for given class name in the current include directories.
	 *
	 * @param string $className
	 *
	 * @return bool|string
	 */
	protected function findClassFile($className) {
		foreach ($this->includePaths as $includePath) {
			$filePath = $includePath.'/'.$className.'.php';

			if (is_file($filePath)) {
				return $filePath;
			}
		}

		return false;
	}
}
