<?php



require_once dirname(__FILE__).'/include/config.inc.php';

$page['title'] = _('Other configuration parameters');
$page['file'] = 'adm.other.php';

require_once dirname(__FILE__).'/include/page_header.php';

$fields = [
	'refresh_unsupported' =>	[T_TRX_STR, O_OPT, null, null, 'isset({update})', _('Refresh unsupported items')],
	'discovery_groupid' =>		[T_TRX_INT, O_OPT, null, DB_ID, 'isset({update})',
		_('Group for discovered hosts')
	],
	'default_inventory_mode' =>	[T_TRX_INT, O_OPT, null,
		IN(HOST_INVENTORY_DISABLED.','.HOST_INVENTORY_MANUAL.','.HOST_INVENTORY_AUTOMATIC), 'isset({update})',
		_('Default host inventory mode')
	],
	'alert_usrgrpid' =>			[T_TRX_INT, O_OPT, null, DB_ID, 'isset({update})',
		_('User group for database down message')
	],
	'snmptrap_logging' =>		[T_TRX_INT, O_OPT, null, IN('1'), null, _('Log unmatched SNMP traps')],
	// actions
	'update' =>					[T_TRX_STR, O_OPT, P_SYS|P_ACT, null, null],
	'form_refresh' =>			[T_TRX_INT, O_OPT, null, null, null]
];
check_fields($fields);

/*
 * Actions
 */
if (hasRequest('update')) {
	DBstart();
	$result = update_config([
		'refresh_unsupported' => getRequest('refresh_unsupported'),
		'alert_usrgrpid' => getRequest('alert_usrgrpid'),
		'discovery_groupid' => getRequest('discovery_groupid'),
		'default_inventory_mode' => getRequest('default_inventory_mode'),
		'snmptrap_logging' => getRequest('snmptrap_logging', 0)
	]);
	$result = DBend($result);

	show_messages($result, _('Configuration updated'), _('Cannot update configuration'));
}

/*
 * Display
 */
$config = select_config();

if (hasRequest('form_refresh')) {
	$data = [
		'refresh_unsupported' => getRequest('refresh_unsupported', $config['refresh_unsupported']),
		'discovery_groupid' => getRequest('discovery_groupid', $config['discovery_groupid']),
		'default_inventory_mode' => getRequest('default_inventory_mode', $config['default_inventory_mode']),
		'alert_usrgrpid' => getRequest('alert_usrgrpid', $config['alert_usrgrpid']),
		'snmptrap_logging' => getRequest('snmptrap_logging', 0)
	];
}
else {
	$data = [
		'refresh_unsupported' => $config['refresh_unsupported'],
		'discovery_groupid' => $config['discovery_groupid'],
		'default_inventory_mode' => $config['default_inventory_mode'],
		'alert_usrgrpid' => $config['alert_usrgrpid'],
		'snmptrap_logging' => $config['snmptrap_logging']
	];
}

$data['discovery_groups'] = API::HostGroup()->get([
	'output' => ['groupid', 'name'],
	'filter' => ['flags' => TRX_FLAG_DISCOVERY_NORMAL],
	'editable' => true
]);
order_result($data['discovery_groups'], 'name');

$data['alert_usrgrps'] = DBfetchArray(DBselect('SELECT u.usrgrpid,u.name FROM usrgrp u'));
order_result($data['alert_usrgrps'], 'name');

$view = new CView('administration.general.other.edit', $data);
$view->render();
$view->show();

require_once dirname(__FILE__).'/include/page_footer.php';
