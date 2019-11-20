<?php



require_once dirname(__FILE__).'/include/config.inc.php';

$page['title'] = _('Configuration of value mapping');
$page['file'] = 'adm.valuemapping.php';

require_once dirname(__FILE__).'/include/page_header.php';

// VAR	TYPE	OPTIONAL	FLAGS	VALIDATION	EXCEPTION
$fields = [
	'valuemapids' =>	[T_TRX_INT, O_OPT,	P_SYS,			DB_ID,		null],
	'valuemapid' =>		[T_TRX_INT, O_NO,	P_SYS,			DB_ID,		'isset({form}) && {form} == "update"'],
	'name' =>			[T_TRX_STR, O_OPT,	null,			NOT_EMPTY,	'isset({add}) || isset({update})'],
	'mappings' =>		[T_TRX_STR, O_OPT,	null,			null,		null],
	// actions
	'add' =>			[T_TRX_STR, O_OPT,	P_SYS|P_ACT,	null,		null],
	'update' =>			[T_TRX_STR, O_OPT,	P_SYS|P_ACT,	null,		null],
	'form' =>			[T_TRX_STR, O_OPT,	P_SYS,			null,		null],
	'form_refresh' =>	[T_TRX_INT, O_OPT,	null,			null,		null],
	'action' =>			[T_TRX_STR, O_OPT,	P_SYS|P_ACT,	IN('"valuemap.export","valuemap.delete"'), null],
	// sort and sortorder
	'sort' =>			[T_TRX_STR, O_OPT, P_SYS, IN('"name"'),									null],
	'sortorder' =>		[T_TRX_STR, O_OPT, P_SYS, IN('"'.TRX_SORT_DOWN.'","'.TRX_SORT_UP.'"'),	null]
];
check_fields($fields);

/*
 * Permissions
 */
if (hasRequest('valuemapid')) {
	$valuemaps = API::ValueMap()->get([
		'output' => [],
		'valuemapids' => getRequest('valuemapid')
	]);

	if (!$valuemaps) {
		access_deny();
	}
}

/*
 * Actions
 */
if (hasRequest('add') || hasRequest('update')) {
	$valuemap = [
		'name' => getRequest('name'),
		'mappings' => getRequest('mappings')
	];

	if (hasRequest('update')) {
		$valuemap['valuemapid'] = getRequest('valuemapid');

		$result = (bool) API::ValueMap()->update($valuemap);

		show_messages($result, _('Value map updated'), _('Cannot update value map'));
	}
	else {
		$result = (bool) API::ValueMap()->create($valuemap);

		show_messages($result, _('Value map added'), _('Cannot add value map'));
	}

	if ($result) {
		unset($_REQUEST['form']);
	}
}
elseif (getRequest('action') === 'valuemap.delete' && hasRequest('valuemapids')) {
	$valuemapids = getRequest('valuemapids', []);

	$result = (bool) API::ValueMap()->delete($valuemapids);

	if ($result) {
		unset($_REQUEST['form']);
		uncheckTableRows();
	}
	else {
		$valuemaps = API::ValueMap()->get([
			'output' => ['valuemapid'],
			'valuemapids' => getRequest('valuemapids')
		]);
		uncheckTableRows(null, trx_objectValues($valuemaps, 'valuemapid'));
	}

	$deleted = count($valuemapids);

	show_messages($result,
		_n('Value map deleted', 'Value maps deleted', $deleted),
		_n('Cannot delete value map', 'Cannot delete value maps', $deleted)
	);
}

/*
 * Display
 */
if (hasRequest('form')) {
	$data = [
		'form' => getRequest('form', ''),
		'valuemapid' => getRequest('valuemapid', 0),
		'valuemap_count' => 0,
		'sid' => substr(CWebUser::getSessionCookie(), 16, 16)
	];

	if ($data['valuemapid'] != 0 && !hasRequest('form_refresh')) {
		$valuemaps = API::ValueMap()->get([
			'output' => ['valuemapid', 'name'],
			'selectMappings' => ['value', 'newvalue'],
			'valuemapids' => $data['valuemapid']
		]);
		$valuemap = reset($valuemaps);

		$data = trx_array_merge($data, $valuemap);
		order_result($data['mappings'], 'value');
	}
	else {
		$data['name'] = getRequest('name', '');
		$data['mappings'] = getRequest('mappings', []);
	}

	if ($data['valuemapid'] != 0) {
		$data['valuemap_count'] += API::Item()->get([
			'countOutput' => true,
			'webitems' => true,
			'filter' => ['valuemapid' => $data['valuemapid']]
		]);
		$data['valuemap_count'] += API::ItemPrototype()->get([
			'countOutput' => true,
			'filter' => ['valuemapid' => $data['valuemapid']]
		]);
	}

	if (!$data['mappings']) {
		$data['mappings'][] = ['value' => '', 'newvalue' => ''];
	}

	$view = new CView('administration.general.valuemapping.edit', $data);
}
else {
	$sortfield = getRequest('sort', CProfile::get('web.'.$page['file'].'.sort', 'name'));
	$sortorder = getRequest('sortorder', CProfile::get('web.'.$page['file'].'.sortorder', TRX_SORT_UP));

	CProfile::update('web.'.$page['file'].'.sort', $sortfield, PROFILE_TYPE_STR);
	CProfile::update('web.'.$page['file'].'.sortorder', $sortorder, PROFILE_TYPE_STR);

	$data = [
		'sort' => $sortfield,
		'sortorder' => $sortorder
	];

	$data['valuemaps'] = API::ValueMap()->get([
		'output' => ['valuemapid', 'name'],
		'selectMappings' => ['value', 'newvalue'],
		'sortfield' => $sortfield,
		'limit' => $config['search_limit'] + 1
	]);

	order_result($data['valuemaps'], $sortfield, $sortorder);
	$data['paging'] = getPagingLine($data['valuemaps'], $sortorder, new CUrl('adm.valuemapping.php'));

	foreach ($data['valuemaps'] as &$valuemap) {
		order_result($valuemap['mappings'], 'value');

		$valuemap['used_in_items'] =
			(bool) API::Item()->get([
				'output' => [],
				'webitems' => true,
				'filter' => ['valuemapid' => $valuemap['valuemapid']],
				'limit' => 1
			])
			|| (bool) API::ItemPrototype()->get([
				'output' => [],
				'filter' => ['valuemapid' => $valuemap['valuemapid']],
				'limit' => 1
			]);
	}
	unset($valuemap);

	$view = new CView('administration.general.valuemapping.list', $data);
}

$view->render();
$view->show();

require_once dirname(__FILE__).'/include/page_footer.php';
