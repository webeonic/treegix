<?php



/**
 * Class to perform low level graph prototype related actions.
 */
class CGraphPrototypeManager {

	/**
	 * Deletes graph prototypes and related entities without permission check.
	 *
	 * @param array $graphids
	 */
	public static function delete(array $graphids) {
		$del_graphids = [];

		// Selecting all inherited graphs.
		$parent_graphids = array_flip($graphids);
		do {
			$db_graphs = DBselect(
				'SELECT g.graphid FROM graphs g WHERE '.dbConditionInt('g.templateid', array_keys($parent_graphids))
			);

			$del_graphids += $parent_graphids;
			$parent_graphids = [];

			while ($db_graph = DBfetch($db_graphs)) {
				if (!array_key_exists($db_graph['graphid'], $del_graphids)) {
					$parent_graphids[$db_graph['graphid']] = true;
				}
			}
		} while ($parent_graphids);

		$del_graphids = array_keys($del_graphids);

		// Deleting discovered graphs.
		$del_discovered_graphids = DBfetchColumn(DBselect(
			'SELECT gd.graphid FROM graph_discovery gd WHERE '.dbConditionInt('gd.parent_graphid', $del_graphids)
		), 'graphid');

		if ($del_discovered_graphids) {
			CGraphManager::delete($del_discovered_graphids);
		}

		DB::delete('screens_items', [
			'resourceid' => $del_graphids,
			'resourcetype' => SCREEN_RESOURCE_LLD_GRAPH
		]);

		DB::delete('graphs_items', ['graphid' => $del_graphids]);
		DB::delete('graphs', ['graphid' => $del_graphids]);
	}
}
