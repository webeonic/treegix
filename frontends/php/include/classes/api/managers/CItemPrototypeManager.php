<?php



/**
 * Class to perform low level item related actions.
 */
class CItemPrototypeManager {

	/**
	 * Deletes item prototypes and related entities without permission check.
	 *
	 * @param array $itemids
	 */
	public static function delete(array $itemids) {
		$del_itemids = [];

		// Selecting all inherited items.
		$parent_itemids = array_flip($itemids);
		do {
			$db_items = DBselect(
				'SELECT i.itemid FROM items i WHERE '.dbConditionInt('i.templateid', array_keys($parent_itemids))
			);

			$del_itemids += $parent_itemids;
			$parent_itemids = [];

			while ($db_item = DBfetch($db_items)) {
				if (!array_key_exists($db_item['itemid'], $del_itemids)) {
					$parent_itemids[$db_item['itemid']] = true;
				}
			}
		} while ($parent_itemids);

		// Selecting all dependent items.
		$dep_itemids = $del_itemids;
		$del_itemids = [];

		do {
			$db_items = DBselect(
				'SELECT i.itemid'.
				' FROM items i'.
				' WHERE i.type='.ITEM_TYPE_DEPENDENT.
					' AND '.dbConditionInt('i.master_itemid', array_keys($dep_itemids))
			);

			$del_itemids += $dep_itemids;
			$dep_itemids = [];

			while ($db_item = DBfetch($db_items)) {
				if (!array_key_exists($db_item['itemid'], $del_itemids)) {
					$dep_itemids[$db_item['itemid']] = true;
				}
			}
		} while ($dep_itemids);

		$del_itemids = array_keys($del_itemids);

		// Deleting graph prototypes, which will remain without item prototypes.
		$db_graphs = DBselect(
			'SELECT DISTINCT gi.graphid'.
			' FROM graphs_items gi'.
			' WHERE '.dbConditionInt('gi.itemid', $del_itemids).
				' AND NOT EXISTS ('.
					'SELECT NULL'.
					' FROM graphs_items gii,items i'.
					' WHERE gi.graphid=gii.graphid'.
						' AND gii.itemid=i.itemid'.
						' AND '.dbConditionInt('gii.itemid', $del_itemids, true).
						' AND '.dbConditionInt('i.flags', [TRX_FLAG_DISCOVERY_PROTOTYPE]).
				')'
		);

		$del_graphids = [];

		while ($db_graph = DBfetch($db_graphs)) {
			$del_graphids[] = $db_graph['graphid'];
		}

		if ($del_graphids) {
			CGraphPrototypeManager::delete($del_graphids);
		}

		// Cleanup ymin_itemid and ymax_itemid fields for graphs and graph prototypes.
		DB::update('graphs', [
			'values' => [
				'ymin_type' => GRAPH_YAXIS_TYPE_CALCULATED,
				'ymin_itemid' => null
			],
			'where' => ['ymin_itemid' => $del_itemids]
		]);

		DB::update('graphs', [
			'values' => [
				'ymax_type' => GRAPH_YAXIS_TYPE_CALCULATED,
				'ymax_itemid' => null
			],
			'where' => ['ymax_itemid' => $del_itemids]
		]);

		// Deleting discovered items.
		$del_discovered_itemids = DBfetchColumn(DBselect(
			'SELECT id.itemid FROM item_discovery id WHERE '.dbConditionInt('id.parent_itemid', $del_itemids)
		), 'itemid');

		if ($del_discovered_itemids) {
			CItemManager::delete($del_discovered_itemids);
		}

		// Deleting trigger prototypes.
		$del_triggerids = DBfetchColumn(DBselect(
			'SELECT DISTINCT f.triggerid'.
			' FROM functions f'.
			' WHERE '.dbConditionInt('f.itemid', $del_itemids)
		), 'triggerid');

		if ($del_triggerids) {
			CTriggerPrototypeManager::delete($del_triggerids);
		}

		// Screen items.
		DB::delete('screens_items', [
			'resourceid' => $del_itemids,
			'resourcetype' => [SCREEN_RESOURCE_LLD_SIMPLE_GRAPH]
		]);

		// Unlink application prototypes and delete those who are no longer linked to any other item prototypes.
		$del_application_prototypeids = DBfetchColumn(DBselect(
			'SELECT DISTINCT iap.application_prototypeid'.
			' FROM item_application_prototype iap'.
			' WHERE '.dbConditionInt('iap.itemid', $del_itemids)
		), 'application_prototypeid');

		if ($del_application_prototypeids) {
			DB::delete('item_application_prototype', ['itemid' => $del_itemids]);

			self::deleteUnusedApplicationPrototypes($del_application_prototypeids);
		}

		DB::delete('items', ['itemid' => $del_itemids]);
	}

	/*
	 * Finds and deletes application prototypes by given IDs. Looks for discovered applications that were created from
	 * prototypes and deletes them if they are not discovered by other rules.
	 *
	 * @param array $application_prototypeids
	 */
	public static function deleteUnusedApplicationPrototypes(array $application_prototypeids) {
		$used_application_prototypes = DBselect(
			'SELECT DISTINCT iap.application_prototypeid'.
			' FROM item_application_prototype iap'.
			' WHERE '.dbConditionInt('iap.application_prototypeid', $application_prototypeids)
		);

		$application_prototypeids = array_flip($application_prototypeids);

		while ($used_application_prototype = DBfetch($used_application_prototypes)) {
			unset($application_prototypeids[$used_application_prototype['application_prototypeid']]);
		}

		if ($application_prototypeids) {
			$application_prototypeids = array_keys($application_prototypeids);

			// Find discovered applications for deletable application prototypes.
			$discovered_applicationids = DBfetchColumn(DBselect(
				'SELECT DISTINCT ad.applicationid'.
				' FROM application_discovery ad'.
				' WHERE '.dbConditionInt('ad.application_prototypeid', $application_prototypeids)
			), 'applicationid');

			// unlink templated application prototype
			DB::update('application_prototype', [
				'values' => ['templateid' => null],
				'where' => ['templateid' => $application_prototypeids]
			]);

			DB::delete('application_prototype', ['application_prototypeid' => $application_prototypeids]);

			/*
			 * Deleting an application prototype will automatically delete the link in 'item_application_prototype',
			 * but it will not delete the actual discovered application. When the link is gone,
			 * delete the discoveted application. Link between a regular item does not matter any more.
			 */
			if ($discovered_applicationids) {
				API::Application()->delete($discovered_applicationids, true);
			}
		}
	}
}
