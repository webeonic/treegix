<?php



require_once dirname(__FILE__).'/include/config.inc.php';

$page['title'] = _('Configuration of trigger severities');
$page['file'] = 'adm.triggerseverities.php';
$page['scripts'] = ['colorpicker.js'];

require_once dirname(__FILE__).'/include/page_header.php';

$fields = [
	'severity_name_0' =>	[T_TRX_STR, O_OPT, null, NOT_EMPTY, 'isset({update})', _('Not classified')],
	'severity_color_0' =>	[T_TRX_CLR, O_OPT, null, null, 'isset({update})', _('Not classified')],
	'severity_name_1' =>	[T_TRX_STR, O_OPT, null, NOT_EMPTY, 'isset({update})', _('Information')],
	'severity_color_1' =>	[T_TRX_CLR, O_OPT, null, null, 'isset({update})', _('Information')],
	'severity_name_2' =>	[T_TRX_STR, O_OPT, null, NOT_EMPTY, 'isset({update})', _('Warning')],
	'severity_color_2' =>	[T_TRX_CLR, O_OPT, null, null, 'isset({update})', _('Warning')],
	'severity_name_3' =>	[T_TRX_STR, O_OPT, null, NOT_EMPTY, 'isset({update})', _('Average')],
	'severity_color_3' =>	[T_TRX_CLR, O_OPT, null, null, 'isset({update})', _('Average')],
	'severity_name_4' =>	[T_TRX_STR, O_OPT, null, NOT_EMPTY, 'isset({update})', _('High')],
	'severity_color_4' =>	[T_TRX_CLR, O_OPT, null, null, 'isset({update})', _('High')],
	'severity_name_5' =>	[T_TRX_STR, O_OPT, null, NOT_EMPTY, 'isset({update})', _('Disaster')],
	'severity_color_5' =>	[T_TRX_CLR, O_OPT, null, null, 'isset({update})', _('Disaster')],
	// actions
	'update' =>				[T_TRX_STR, O_OPT, P_SYS|P_ACT, null, null],
	'form_refresh' =>		[T_TRX_INT, O_OPT, null, null, null]
];
check_fields($fields);

/*
 * Actions
 */
if (hasRequest('update')) {
	DBstart();
	$result = update_config([
		'severity_name_0' => getRequest('severity_name_0'),
		'severity_color_0' => getRequest('severity_color_0'),
		'severity_name_1' => getRequest('severity_name_1'),
		'severity_color_1' => getRequest('severity_color_1'),
		'severity_name_2' => getRequest('severity_name_2'),
		'severity_color_2' => getRequest('severity_color_2'),
		'severity_name_3' => getRequest('severity_name_3'),
		'severity_color_3' => getRequest('severity_color_3'),
		'severity_name_4' => getRequest('severity_name_4'),
		'severity_color_4' => getRequest('severity_color_4'),
		'severity_name_5' => getRequest('severity_name_5'),
		'severity_color_5' => getRequest('severity_color_5')
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
		'severity_name_0' => getRequest('severity_name_0', $config['severity_name_0']),
		'severity_color_0' => getRequest('severity_color_0', $config['severity_color_0']),
		'severity_name_1' => getRequest('severity_name_1', $config['severity_name_1']),
		'severity_color_1' => getRequest('severity_color_1', $config['severity_color_1']),
		'severity_name_2' => getRequest('severity_name_2', $config['severity_name_2']),
		'severity_color_2' => getRequest('severity_color_2', $config['severity_color_2']),
		'severity_name_3' => getRequest('severity_name_3', $config['severity_name_3']),
		'severity_color_3' => getRequest('severity_color_3', $config['severity_color_3']),
		'severity_name_4' => getRequest('severity_name_4', $config['severity_name_4']),
		'severity_color_4' => getRequest('severity_color_4', $config['severity_color_4']),
		'severity_name_5' => getRequest('severity_name_5', $config['severity_name_5']),
		'severity_color_5' => getRequest('severity_color_5', $config['severity_color_5'])
	];
}
else {
	$data = [
		'severity_name_0' => $config['severity_name_0'],
		'severity_color_0' => $config['severity_color_0'],
		'severity_name_1' => $config['severity_name_1'],
		'severity_color_1' => $config['severity_color_1'],
		'severity_name_2' => $config['severity_name_2'],
		'severity_color_2' => $config['severity_color_2'],
		'severity_name_3' => $config['severity_name_3'],
		'severity_color_3' => $config['severity_color_3'],
		'severity_name_4' => $config['severity_name_4'],
		'severity_color_4' => $config['severity_color_4'],
		'severity_name_5' => $config['severity_name_5'],
		'severity_color_5' => $config['severity_color_5']
	];
}

$view = new CView('administration.general.triggerSeverity.edit', $data);
$view->render();
$view->show();

require_once dirname(__FILE__).'/include/page_footer.php';
