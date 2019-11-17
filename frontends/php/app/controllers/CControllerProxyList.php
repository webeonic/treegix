<?php



class CControllerProxyList extends CController {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		$fields = [
			'sort' =>			'in host',
			'sortorder' =>		'in '.TRX_SORT_DOWN.','.TRX_SORT_UP,
			'uncheck' =>		'in 1',
			'filter_set' =>		'in 1',
			'filter_rst' =>		'in 1',
			'filter_name' =>	'string',
			'filter_status' =>	'in -1,'.HOST_STATUS_PROXY_ACTIVE.','.HOST_STATUS_PROXY_PASSIVE
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		return ($this->getUserType() == USER_TYPE_SUPER_ADMIN);
	}

	protected function doAction() {
		$sortField = $this->getInput('sort', CProfile::get('web.proxies.php.sort', 'host'));
		$sortOrder = $this->getInput('sortorder', CProfile::get('web.proxies.php.sortorder', TRX_SORT_UP));

		CProfile::update('web.proxies.php.sort', $sortField, PROFILE_TYPE_STR);
		CProfile::update('web.proxies.php.sortorder', $sortOrder, PROFILE_TYPE_STR);

		// filter
		if (hasRequest('filter_set')) {
			CProfile::update('web.proxies.filter_name', getRequest('filter_name', ''), PROFILE_TYPE_STR);
			CProfile::update('web.proxies.filter_status', getRequest('filter_status', -1), PROFILE_TYPE_INT);
		}
		elseif (hasRequest('filter_rst')) {
			CProfile::delete('web.proxies.filter_name');
			CProfile::delete('web.proxies.filter_status');
		}

		$filter = [
			'name' => CProfile::get('web.proxies.filter_name', ''),
			'status' => CProfile::get('web.proxies.filter_status', -1)
		];

		$config = select_config();

		$data = [
			'uncheck' => $this->hasInput('uncheck'),
			'sort' => $sortField,
			'sortorder' => $sortOrder,
			'filter' => $filter,
			'config' => [
				'max_in_table' => $config['max_in_table']
			],
			'profileIdx' => 'web.proxies.filter',
			'active_tab' => CProfile::get('web.proxies.filter.active', 1)
		];

		$data['proxies'] = API::Proxy()->get([
			'output' => ['proxyid', $sortField],
			'search' => [
				'host' => ($filter['name'] === '') ? null : $filter['name']
			],
			'filter' => [
				'status' => ($filter['status'] == -1) ? null : $filter['status']
			],
			'sortfield' => $sortField,
			'limit' => $config['search_limit'] + 1,
			'editable' => true,
			'preservekeys' => true
		]);
		// sorting & paging
		order_result($data['proxies'], $sortField, $sortOrder);

		$url = (new CUrl('treegix.php'))
			->setArgument('action', 'proxy.list');

		$data['paging'] = getPagingLine($data['proxies'], $sortOrder, $url);

		$data['proxies'] = API::Proxy()->get([
			'output' => ['proxyid', 'host', 'status', 'lastaccess', 'tls_connect', 'tls_accept', 'auto_compress'],
			'selectHosts' => ['hostid', 'name', 'status'],
			'proxyids' => array_keys($data['proxies']),
			'editable' => true,
			'preservekeys' => true
		]);
		order_result($data['proxies'], $sortField, $sortOrder);

		foreach ($data['proxies'] as &$proxy) {
			order_result($proxy['hosts'], 'name');
			$proxy['hosts'] = array_slice($proxy['hosts'], 0, $data['config']['max_in_table'] + 1);
		}
		unset($proxy);

		if ($data['proxies']) {
			global $TRX_SERVER, $TRX_SERVER_PORT;

			$server = new CTreegixServer($TRX_SERVER, $TRX_SERVER_PORT, TRX_SOCKET_TIMEOUT, TRX_SOCKET_BYTES_LIMIT);
			$server_status = $server->getStatus(get_cookie(TRX_SESSION_NAME));

			if ($server_status !== false) {
				$defaults = [
					'host_count' => 0,
					'item_count' => 0
				];
				if (array_key_exists('required performance', $server_status)) {
					$defaults['vps_total'] = 0;
				}
				foreach ($data['proxies'] as &$proxy) {
					$proxy += $defaults;
				}
				unset($proxy);

				// hosts
				foreach ($server_status['host stats'] as $stats) {
					if ($stats['attributes']['status'] == HOST_STATUS_MONITORED) {
						if (array_key_exists($stats['attributes']['proxyid'], $data['proxies'])) {
							$data['proxies'][$stats['attributes']['proxyid']]['host_count'] += $stats['count'];
						}
					}
				}

				// items
				foreach ($server_status['item stats'] as $stats) {
					if ($stats['attributes']['status'] == ITEM_STATUS_ACTIVE) {
						if (array_key_exists($stats['attributes']['proxyid'], $data['proxies'])) {
							$data['proxies'][$stats['attributes']['proxyid']]['item_count'] += $stats['count'];
						}
					}
				}

				// performance
				if (array_key_exists('required performance', $server_status)) {
					foreach ($server_status['required performance'] as $stats) {
						if (array_key_exists($stats['attributes']['proxyid'], $data['proxies'])) {
							$data['proxies'][$stats['attributes']['proxyid']]['vps_total'] += round($stats['count'], 2);
						}
					}
				}
			}
		}

		$response = new CControllerResponseData($data);
		$response->setTitle(_('Configuration of proxies'));
		$this->setResponse($response);
	}
}
