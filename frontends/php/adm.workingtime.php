<?php



require_once dirname(__FILE__).'/include/config.inc.php';

$page['title'] = _('Configuration of working time');
$page['file'] = 'adm.workingtime.php';

require_once dirname(__FILE__).'/include/page_header.php';

$fields = [
	'work_period' =>	[T_TRX_TP, O_OPT, null, null, 'isset({update})', _('Working time')],
	// actions
	'update' =>			[T_TRX_STR, O_OPT, P_SYS|P_ACT, null, null],
	'form_refresh' =>	[T_TRX_INT, O_OPT, null, null, null]
];
check_fields($fields);

/*
 * Actions
 */
if (hasRequest('update')) {
	DBstart();
	$result = update_config(['work_period' => getRequest('work_period')]);
	$result = DBend($result);

	show_messages($result, _('Configuration updated'), _('Cannot update configuration'));
}

/*
 * Display
 */
$config = select_config();

if (hasRequest('form_refresh')) {
	$data = ['work_period' => getRequest('work_period', $config['work_period'])];
}
else {
	$data = ['work_period' => $config['work_period']];
}

$view = new CView('administration.general.workingtime.edit', $data);
$view->render();
$view->show();

require_once dirname(__FILE__).'/include/page_footer.php';
