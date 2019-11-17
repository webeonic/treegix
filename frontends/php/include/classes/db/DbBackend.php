<?php



/**
 * Abstract database backend class.
 */
abstract class DbBackend {

	protected $error;

	/**
	 * Check if 'dbversion' table exists.
	 *
	 * @return boolean
	 */
	abstract protected function checkDbVersionTable();

	/**
	 * Check if connected database version matches with frontend version.
	 *
	 * @return bool
	 */
	public function checkDbVersion() {
		if (!$this->checkDbVersionTable()) {
			return false;
		}

		$version = DBfetch(DBselect('SELECT dv.mandatory,dv.optional FROM dbversion dv'));
		if ($version['mandatory'] != TREEGIX_DB_VERSION) {
			$this->setError(_s('The frontend does not match Treegix database. Current database version (mandatory/optional): %d/%d. Required mandatory version: %d. Contact your system administrator.',
				$version['mandatory'], $version['optional'], TREEGIX_DB_VERSION));
			return false;
		}

		return true;
	}

	/**
	 * Check the integrity of the table "config".
	 *
	 * @return bool
	 */
	public function checkConfig() {
		if (!DBfetch(DBselect('SELECT NULL FROM config c'))) {
			$this->setError(_('Unable to select configuration.'));
			return false;
		}

		return true;
	}

	/**
	 * Create INSERT SQL query for MySQL, PostgreSQL and IBM DB2.
	 * Creation example:
	 *	INSERT INTO applications (name,hostid,templateid,applicationid)
	 *	VALUES ('CPU','10113','13','868'),('Filesystems','10113','5','869'),('General','10113','21','870');
	 *
	 * @param string $table
	 * @param array $fields
	 * @param array $values
	 *
	 * @return string
	 */
	public function createInsertQuery($table, array $fields, array $values) {
		$sql = 'INSERT INTO '.$table.' ('.implode(',', $fields).') VALUES ';

		foreach ($values as $row) {
			$sql .= '('.implode(',', array_values($row)).'),';
		}

		$sql = substr($sql, 0, -1);

		return $sql;
	}

	/**
	 * Set error string.
	 *
	 * @param string $error
	 */
	public function setError($error) {
		$this->error = $error;
	}

	/**
	 * Return error or null if no error occurred.
	 *
	 * @return mixed
	 */
	public function getError() {
		return $this->error;
	}
}
