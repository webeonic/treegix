<?php



/**
 * Shows surrogate screen filled with graphs generated by selected graph prototype or preview of graph prototype.
 */
class CScreenLldGraph extends CScreenLldGraphBase {

	/**
	 * @var array
	 */
	protected $createdGraphIds = [];

	/**
	 * @var array
	 */
	protected $graphPrototype = null;

	/**
	 * Returns screen items for surrogate screen.
	 *
	 * @return array
	 */
	protected function getSurrogateScreenItems() {
		$createdGraphIds = $this->getCreatedGraphIds();
		return $this->getGraphsForSurrogateScreen($createdGraphIds);
	}

	/**
	 * Retrieves graphs created for graph prototype given as resource for this screen item
	 * and returns array of the graph IDs.
	 *
	 * @return array
	 */
	protected function getCreatedGraphIds() {
		if (!$this->createdGraphIds) {
			$graphPrototype = $this->getGraphPrototype();

			if ($graphPrototype) {
				// Get all created (discovered) graphs for host of graph prototype.
				$allCreatedGraphs = API::Graph()->get([
					'output' => ['graphid', 'name'],
					'hostids' => [$graphPrototype['discoveryRule']['hostid']],
					'selectGraphDiscovery' => ['graphid', 'parent_graphid'],
					'expandName' => true,
					'filter' => ['flags' => ZBX_FLAG_DISCOVERY_CREATED],
				]);

				// Collect those graph IDs where parent graph is graph prototype selected for
				// this screen item as resource.
				foreach ($allCreatedGraphs as $graph) {
					if ($graph['graphDiscovery']['parent_graphid'] == $graphPrototype['graphid']) {
						$this->createdGraphIds[$graph['graphid']] = $graph['name'];
					}
				}
				natsort($this->createdGraphIds);
				$this->createdGraphIds = array_keys($this->createdGraphIds);
			}
		}

		return $this->createdGraphIds;
	}

	/**
	 * Makes graph screen items from given graph IDs.
	 *
	 * @param array $graphIds
	 *
	 * @return array
	 */
	protected function getGraphsForSurrogateScreen(array $graphIds) {
		$screenItemTemplate = $this->getScreenItemTemplate(SCREEN_RESOURCE_GRAPH);

		$screenItems = [];
		foreach ($graphIds as $graphId) {
			$screenItem = $screenItemTemplate;

			$screenItem['resourceid'] = $graphId;
			$screenItem['screenitemid'] = $graphId;

			$screenItems[] = $screenItem;
		}

		return $screenItems;
	}

	/**
	 * Resolves and retrieves effective graph prototype used in this screen item.
	 *
	 * @return array|bool
	 */
	protected function getGraphPrototype() {
		if ($this->graphPrototype === null) {
			$resourceid = array_key_exists('real_resourceid', $this->screenitem)
				? $this->screenitem['real_resourceid']
				: $this->screenitem['resourceid'];

			$options = [
				'output' => ['graphid', 'name', 'graphtype', 'show_legend', 'show_3d', 'show_work_period', 'templated'],
				'selectDiscoveryRule' => ['hostid']
			];

			/*
			 * If screen item is dynamic or is templated screen, real graph prototype is looked up by "name"
			 * used as resource ID for this screen item and by current host.
			 */
			if ($this->screenitem['dynamic'] == SCREEN_DYNAMIC_ITEM && $this->hostid) {
				$currentGraphPrototype = API::GraphPrototype()->get([
					'output' => ['name'],
					'graphids' => [$resourceid]
				]);
				$currentGraphPrototype = reset($currentGraphPrototype);

				$options['hostids'] = [$this->hostid];
				$options['filter'] = ['name' => $currentGraphPrototype['name']];
			}
			// otherwise just use resource ID given to this screen item.
			else {
				$options['graphids'] = [$resourceid];
			}

			$selectedGraphPrototype = API::GraphPrototype()->get($options);
			$this->graphPrototype = reset($selectedGraphPrototype);
		}

		return $this->graphPrototype;
	}

	/**
	 * Returns output for preview of graph prototype.
	 *
	 * @return CTag
	 */
	protected function getPreviewOutput() {
		$graph_prototype = $this->getGraphPrototype();

		switch ($graph_prototype['graphtype']) {
			case GRAPH_TYPE_NORMAL:
			case GRAPH_TYPE_STACKED:
				$src = (new CUrl('chart3.php'))->setArgument('showworkperiod', $graph_prototype['show_work_period']);
				break;

			case GRAPH_TYPE_EXPLODED:
			case GRAPH_TYPE_3D_EXPLODED:
			case GRAPH_TYPE_3D:
			case GRAPH_TYPE_PIE:
				$src = new CUrl('chart7.php');
				break;

			default:
				show_error_message(_('Graph prototype not found.'));
				exit;
		}

		$graph_prototype_items = API::GraphItem()->get([
			'output' => [
				'gitemid', 'itemid', 'sortorder', 'flags', 'type', 'calc_fnc', 'drawtype', 'yaxisside', 'color'
			],
			'graphids' => [$graph_prototype['graphid']]
		]);

		$src
			->setArgument('items', $graph_prototype_items)
			->setArgument('graphtype', $graph_prototype['graphtype'])
			->setArgument('period', 3600)
			->setArgument('legend', $graph_prototype['show_legend'])
			->setArgument('graph3d', $graph_prototype['show_3d'])
			->setArgument('width', $this->screenitem['width'])
			->setArgument('height', $this->screenitem['height'])
			->setArgument('name', $graph_prototype['name']);

		return new CSpan(new CImg($src->getUrl()));
	}
}
