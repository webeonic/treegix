<?php



/**
 * Database backend class for DB2.
 */
class Db2DbBackend extends DbBackend {

	/**
	 * Check if 'dbversion' table exists.
	 *
	 * @return boolean
	 */
	protected function checkDbVersionTable() {
		global $DB;

		$tabSchema = trx_dbstr(!empty($DB['SCHEMA']) ? $DB['SCHEMA'] : strtoupper($DB['USER']));
		$tableExists = DBfetch(DBselect('SELECT 1 FROM SYSCAT.TABLES'.
			" WHERE TABNAME='DBVERSION'".
				" AND TABSCHEMA=".$tabSchema));

		if (!$tableExists) {
			$this->setError(_('The frontend does not match Treegix database.'));
			return false;
		}

		return true;
	}
}
