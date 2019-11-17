<?php



class CConfigFile {

	const CONFIG_NOT_FOUND = 1;
	const CONFIG_ERROR = 2;

	const CONFIG_FILE_PATH = '/conf/treegix.conf.php';

	private static $supported_db_types = [
		TRX_DB_DB2 => true,
		TRX_DB_MYSQL => true,
		TRX_DB_ORACLE => true,
		TRX_DB_POSTGRESQL => true
	];

	public $configFile = null;
	public $config = [];
	public $error = '';

	private static function exception($error, $code = self::CONFIG_ERROR) {
		throw new ConfigFileException($error, $code);
	}

	public function __construct($file = null) {
		$this->setDefaults();

		if (!is_null($file)) {
			$this->setFile($file);
		}
	}

	public function setFile($file) {
		$this->configFile = $file;
	}

	public function load() {
		if (!file_exists($this->configFile)) {
			self::exception('Config file does not exist.', self::CONFIG_NOT_FOUND);
		}
		if (!is_readable($this->configFile)) {
			self::exception('Permission denied.');
		}

		ob_start();
		include($this->configFile);
		ob_end_clean();

		if (!isset($DB['TYPE'])) {
			self::exception('DB type is not set.');
		}

		if (!array_key_exists($DB['TYPE'], self::$supported_db_types)) {
			self::exception(
				'Incorrect value "'.$DB['TYPE'].'" for DB type. Possible values '.
				implode(', ', array_keys(self::$supported_db_types)).'.'
			);
		}

		$php_supported_db = array_keys(CFrontendSetup::getSupportedDatabases());

		if (!in_array($DB['TYPE'], $php_supported_db)) {
			self::exception('DB type "'.$DB['TYPE'].'" is not supported by current setup.'.
				($php_supported_db ? ' Possible values '.implode(', ', $php_supported_db).'.' : '')
			);
		}

		if (!isset($DB['DATABASE'])) {
			self::exception('DB database is not set.');
		}

		$this->setDefaults();

		$this->config['DB']['TYPE'] = $DB['TYPE'];
		$this->config['DB']['DATABASE'] = $DB['DATABASE'];

		if (isset($DB['SERVER'])) {
			$this->config['DB']['SERVER'] = $DB['SERVER'];
		}

		if (isset($DB['PORT'])) {
			$this->config['DB']['PORT'] = $DB['PORT'];
		}

		if (isset($DB['USER'])) {
			$this->config['DB']['USER'] = $DB['USER'];
		}

		if (isset($DB['PASSWORD'])) {
			$this->config['DB']['PASSWORD'] = $DB['PASSWORD'];
		}

		if (isset($DB['SCHEMA'])) {
			$this->config['DB']['SCHEMA'] = $DB['SCHEMA'];
		}

		if (isset($TRX_SERVER)) {
			$this->config['TRX_SERVER'] = $TRX_SERVER;
		}
		if (isset($TRX_SERVER_PORT)) {
			$this->config['TRX_SERVER_PORT'] = $TRX_SERVER_PORT;
		}
		if (isset($TRX_SERVER_NAME)) {
			$this->config['TRX_SERVER_NAME'] = $TRX_SERVER_NAME;
		}

		$this->makeGlobal();

		return $this->config;
	}

	public function makeGlobal() {
		global $DB, $TRX_SERVER, $TRX_SERVER_PORT, $TRX_SERVER_NAME;

		$DB = $this->config['DB'];
		$TRX_SERVER = $this->config['TRX_SERVER'];
		$TRX_SERVER_PORT = $this->config['TRX_SERVER_PORT'];
		$TRX_SERVER_NAME = $this->config['TRX_SERVER_NAME'];
	}

	public function save() {
		try {
			if (is_null($this->configFile)) {
				self::exception('Cannot save, config file is not set.');
			}

			$this->check();

			if (!file_put_contents($this->configFile, $this->getString())) {
				if (file_exists($this->configFile)) {
					if (file_get_contents($this->configFile) !== $this->getString()) {
						self::exception(_('Unable to overwrite the existing configuration file.'));
					}
				}
				else {
					self::exception(_('Unable to create the configuration file.'));
				}
			}

			return true;
		}
		catch (Exception $e) {
			$this->error = $e->getMessage();
			return false;
		}
	}

	public function getString() {
		return
'<?php
// Treegix GUI configuration file.
global $DB;

$DB[\'TYPE\']     = \''.addcslashes($this->config['DB']['TYPE'], "'\\").'\';
$DB[\'SERVER\']   = \''.addcslashes($this->config['DB']['SERVER'], "'\\").'\';
$DB[\'PORT\']     = \''.addcslashes($this->config['DB']['PORT'], "'\\").'\';
$DB[\'DATABASE\'] = \''.addcslashes($this->config['DB']['DATABASE'], "'\\").'\';
$DB[\'USER\']     = \''.addcslashes($this->config['DB']['USER'], "'\\").'\';
$DB[\'PASSWORD\'] = \''.addcslashes($this->config['DB']['PASSWORD'], "'\\").'\';

// Schema name. Used for IBM DB2 and PostgreSQL.
$DB[\'SCHEMA\'] = \''.addcslashes($this->config['DB']['SCHEMA'], "'\\").'\';

$TRX_SERVER      = \''.addcslashes($this->config['TRX_SERVER'], "'\\").'\';
$TRX_SERVER_PORT = \''.addcslashes($this->config['TRX_SERVER_PORT'], "'\\").'\';
$TRX_SERVER_NAME = \''.addcslashes($this->config['TRX_SERVER_NAME'], "'\\").'\';

$IMAGE_FORMAT_DEFAULT = IMAGE_FORMAT_PNG;
';
	}

	protected function setDefaults() {
		$this->config['DB'] = [
			'TYPE' => null,
			'SERVER' => 'localhost',
			'PORT' => '0',
			'DATABASE' => null,
			'USER' => '',
			'PASSWORD' => '',
			'SCHEMA' => ''
		];
		$this->config['TRX_SERVER'] = 'localhost';
		$this->config['TRX_SERVER_PORT'] = '10051';
		$this->config['TRX_SERVER_NAME'] = '';
	}

	protected function check() {
		if (!isset($this->config['DB']['TYPE'])) {
			self::exception('DB type is not set.');
		}

		if (!array_key_exists($this->config['DB']['TYPE'], self::$supported_db_types)) {
			self::exception(
				'Incorrect value "'.$this->config['DB']['TYPE'].'" for DB type. Possible values '.
				implode(', ', array_keys(self::$supported_db_types)).'.'
			);
		}

		if (!isset($this->config['DB']['DATABASE'])) {
			self::exception('DB database is not set.');
		}
	}
}
