<?php



/**
 * Class to perform low level trigger prototype related actions.
 */
class CTriggerPrototypeManager {

	/**
	 * Deletes trigger prototypes and related entities without permission check.
	 *
	 * @param array $triggerids
	 */
	public static function delete(array $triggerids) {
		$del_triggerids = [];

		// Selecting all inherited triggers.
		$parent_triggerids = array_flip($triggerids);
		do {
			$db_triggers = DBselect(
				'SELECT t.triggerid'.
				' FROM triggers t'.
				' WHERE '.dbConditionInt('t.templateid', array_keys($parent_triggerids))
			);

			$del_triggerids += $parent_triggerids;
			$parent_triggerids = [];

			while ($db_trigger = DBfetch($db_triggers)) {
				if (!array_key_exists($db_trigger['triggerid'], $del_triggerids)) {
					$parent_triggerids[$db_trigger['triggerid']] = true;
				}
			}
		} while ($parent_triggerids);

		$del_triggerids = array_keys($del_triggerids);

		// Deleting discovered triggers.
		$del_discovered_triggerids = DBfetchColumn(DBselect(
			'SELECT td.triggerid'.
			' FROM trigger_discovery td'.
			' WHERE '.dbConditionInt('td.parent_triggerid', $del_triggerids)
		), 'triggerid');

		if ($del_discovered_triggerids) {
			CTriggerManager::delete($del_discovered_triggerids);
		}

		DB::delete('trigger_depends', ['triggerid_down' => $del_triggerids]);
		DB::delete('trigger_depends', ['triggerid_up' => $del_triggerids]);
		DB::delete('trigger_tag', ['triggerid' => $del_triggerids]);
		DB::delete('triggers', ['triggerid' => $del_triggerids]);
	}
}
