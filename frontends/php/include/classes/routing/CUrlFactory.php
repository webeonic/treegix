<?php


class CUrlFactory {

	/**
	 * Configuration of context configurations. Top-level key is file name from $page['file'], below goes:
	 * 'remove' - to remove arguments
	 * 'add' - name of $_REQUEST keys to be kept as arguments
	 * 'callable' - callable to apply to argument list
	 *
	 * @var array
	 */
	protected static $contextConfigs = [
		'actionconf.php' => [
			'remove' => ['actionid']
		],
		'applications.php' => [
			'remove' => ['applicationid']
		],
		'disc_prototypes.php' => [
			'remove' => ['itemid'],
			'add' => ['hostid', 'parent_discoveryid']
		],
		'discoveryconf.php' => [
			'remove' => ['druleid']
		],
		'graphs.php' => [
			'remove' => ['graphid'],
			'add' => ['hostid', 'parent_discoveryid']
		],
		'host_discovery.php' => [
			'remove' => ['itemid'],
			'add' => ['hostid']
		],
		'host_prototypes.php' => [
			'remove' => ['hostid'],
			'add' => ['parent_discoveryid']
		],
		'hostgroups.php' => [
			'remove' => ['groupid']
		],
		'hosts.php' => [
			'remove' => ['hostid']
		],
		'httpconf.php' => [
			'remove' => ['httptestid']
		],
		'items.php' => [
			'remove' => ['itemid']
		],
		'maintenance.php' => [
			'remove' => ['maintenanceid']
		],
		'screenconf.php' => [
			'remove' => ['screenid'],
			'add' => ['templateid']
		],
		'slideconf.php' => [
			'remove' => ['slideshowid']
		],
		'sysmaps.php' => [
			'remove' => ['sysmapid']
		],
		'templates.php' => [
			'remove' => ['templateid']
		],
		'trigger_prototypes.php' => [
			'remove' =>  ['triggerid'],
			'add' => ['parent_discoveryid', 'hostid']
		],
		'triggers.php' => [
			'remove' => ['triggerid'],
			'add' => ['hostid']
		],
		'usergrps.php' => [
			'remove' => ['usrgrpid']
		],
		'__default' => [
			'remove' => ['cancel', 'form', 'delete']
		]
	];

	/**
	 * Creates new CUrl object based on giver URL (or $_REQUEST if null is given),
	 * and adds/removes parameters based on current page context.
	 *
	 * @param string $sourceUrl
	 *
	 * @return CUrl
	 */
	public static function getContextUrl($sourceUrl = null) {
		$config = self::resolveConfig();

		$url = new CUrl($sourceUrl);

		if (isset($config['remove'])) {
			foreach ($config['remove'] as $key) {
				$url->removeArgument($key);
			}
		}

		if (isset($config['add'])) {
			foreach ($config['add'] as $key) {
				$url->setArgument($key, getRequest($key));
			}
		}

		return $url;
	}

	/**
	 * Resolves context configuration for current file (based on $page['file'] global variable)
	 *
	 * @return array
	 */
	protected static function resolveConfig() {
		global $page;

		if (isset($page['file']) && isset(self::$contextConfigs[$page['file']])) {
			return array_merge_recursive(self::$contextConfigs['__default'], self::$contextConfigs[$page['file']]);
		}

		return self::$contextConfigs['__default'];
	}
}
