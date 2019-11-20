<?php



/**
 * Database backend class for PostgreSQL.
 */
class PostgresqlDbBackend extends DbBackend {

	/**
	 * Check if 'dbversion' table exists.
	 *
	 * @return bool
	 */
	protected function checkDbVersionTable() {
		global $DB;

		$schema = trx_dbstr($DB['SCHEMA'] ? $DB['SCHEMA'] : 'public');

		$tableExists = DBfetch(DBselect('SELECT 1 FROM information_schema.tables'.
			' WHERE table_catalog='.trx_dbstr($DB['DATABASE']).
				' AND table_schema='.$schema.
				" AND table_name='dbversion'"
		));

		if (!$tableExists) {
			$this->setError(_('The frontend does not match Treegix database.'));
			return false;
		}

		return true;
	}
}
