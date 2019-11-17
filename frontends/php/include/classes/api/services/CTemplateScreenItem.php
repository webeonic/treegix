<?php



/**
 * Class containing methods for operations with template screen items.
 */
class CTemplateScreenItem extends CApiService {

	protected $tableName = 'screens_items';
	protected $tableAlias = 'si';

	protected $sortColumns = [
		'screenitemid',
		'screenid'
	];

	public function __construct() {
		parent::__construct();

		$this->getOptions = zbx_array_merge($this->getOptions, [
			'screenitemids'	=> null,
			'screenids'		=> null,
			'hostids'		=> null,
			'editable'		=> false,
			'sortfield'		=> '',
			'sortorder'		=> '',
			'preservekeys'	=> false,
			'countOutput'	=> false
		]);
	}

	/**
	 * Get screen item data.
	 *
	 * @param array $options
	 * @param array $options['hostid']			Use hostid to get real resource id
	 * @param array $options['screenitemids']	Search by screen item IDs
	 * @param array $options['screenids']		Search by screen IDs
	 * @param array $options['filter']			Result filter
	 * @param array $options['limit']			The size of the result set
	 *
	 * @return array
	 */
	public function get(array $options = []) {
		$options = zbx_array_merge($this->getOptions, $options);

		// build and execute query
		$sql = $this->createSelectQuery($this->tableName(), $options);
		$res = DBselect($sql, $options['limit']);

		// fetch results
		$result = [];
		while ($row = DBfetch($res)) {
			// count query, return a single result
			if ($options['countOutput']) {
				$result = $row['rowscount'];
			}
			// normal select query
			else {
				if ($options['preservekeys']) {
					$result[$row['screenitemid']] = $row;
				}
				else {
					$result[] = $row;
				}
			}
		}

		// fill result with real resourceid
		if ($options['hostids'] && $result) {
			if (empty($options['screenitemid'])) {
				$options['screenitemid'] = zbx_objectValues($result, 'screenitemid');
			}

			$dbTemplateScreens = API::TemplateScreen()->get([
				'output' => ['screenitemid'],
				'screenitemids' => $options['screenitemid'],
				'hostids' => $options['hostids'],
				'selectScreenItems' => API_OUTPUT_EXTEND
			]);

			if ($dbTemplateScreens) {
				foreach ($result as &$screenItem) {
					foreach ($dbTemplateScreens as $dbTemplateScreen) {
						foreach ($dbTemplateScreen['screenitems'] as $dbScreenItem) {
							if ($screenItem['screenitemid'] == $dbScreenItem['screenitemid']
									&& isset($dbScreenItem['real_resourceid']) && $dbScreenItem['real_resourceid']) {
								$screenItem['real_resourceid'] = $dbScreenItem['real_resourceid'];
							}
						}
					}
				}
				unset($screenItem);
			}
		}

		return $result;
	}

	protected function applyQueryFilterOptions($tableName, $tableAlias, array $options, array $sqlParts) {
		$sqlParts = parent::applyQueryFilterOptions($tableName, $tableAlias, $options, $sqlParts);

		// screen ids
		if ($options['screenids'] !== null) {
			zbx_value2array($options['screenids']);
			$sqlParts = $this->addQuerySelect($this->fieldId('screenid'), $sqlParts);
			$sqlParts['where'][] = dbConditionInt($this->fieldId('screenid'), $options['screenids']);
		}

		return $sqlParts;
	}
}
