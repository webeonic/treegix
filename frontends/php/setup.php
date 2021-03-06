<?php



require_once dirname(__FILE__).'/include/classes/core/Z.php';

$page['file'] = 'setup.php';

try {
	Z::getInstance()->run(ZBase::EXEC_MODE_SETUP);
}
catch (Exception $e) {
	(new CView('general.warning', [
		'header' => $e->getMessage(),
		'messages' => [],
		'theme' => TRX_DEFAULT_THEME
	]))->render();

	exit;
}

// VAR	TYPE	OPTIONAL	FLAGS	VALIDATION	EXCEPTION
$fields = [
	'type' =>				[T_TRX_STR, O_OPT, null,	IN('"'.TRX_DB_MYSQL.'","'.TRX_DB_POSTGRESQL.'","'.TRX_DB_ORACLE.'","'.TRX_DB_DB2.'"'), null],
	'server' =>				[T_TRX_STR, O_OPT, null,	null,				null],
	'port' =>				[T_TRX_INT, O_OPT, null,	BETWEEN(0, 65535),	null, _('Database port')],
	'database' =>			[T_TRX_STR, O_OPT, null,	NOT_EMPTY,			null, _('Database name')],
	'user' =>				[T_TRX_STR, O_OPT, null,	null,				null],
	'password' =>			[T_TRX_STR, O_OPT, null,	null, 				null],
	'schema' =>				[T_TRX_STR, O_OPT, null,	null, 				null],
	'trx_server' =>			[T_TRX_STR, O_OPT, null,	null,				null],
	'trx_server_name' =>	[T_TRX_STR, O_OPT, null,	null,				null],
	'trx_server_port' =>	[T_TRX_INT, O_OPT, null,	BETWEEN(0, 65535),	null, _('Port')],
	// actions
	'save_config' =>		[T_TRX_STR, O_OPT, P_SYS,	null,				null],
	'retry' =>				[T_TRX_STR, O_OPT, P_SYS,	null,				null],
	'cancel' =>				[T_TRX_STR, O_OPT, P_SYS,	null,				null],
	'finish' =>				[T_TRX_STR, O_OPT, P_SYS,	null,				null],
	'next' =>				[T_TRX_STR, O_OPT, P_SYS,	null,				null],
	'back' =>				[T_TRX_STR, O_OPT, P_SYS,	null,				null],
];

CSession::start();
CSession::setValue('check_fields_result', check_fields($fields, false));
if (!CSession::keyExists('step')) {
	CSession::setValue('step', 0);
}

// if a guest or a non-super admin user is logged in
if (CWebUser::$data && CWebUser::getType() < USER_TYPE_SUPER_ADMIN) {
	// on the last step of the setup we always have a guest user logged in;
	// when he presses the "Finish" button he must be redirected to the login screen
	if (CWebUser::isGuest() && CSession::getValue('step') == 5 && hasRequest('finish')) {
		CSession::clear();
		redirect('index.php');
	}
	// the guest user can also view the last step of the setup
	// all other user types must not have access to the setup
	elseif (!(CWebUser::isGuest() && CSession::getValue('step') == 5)) {
		access_deny(ACCESS_DENY_PAGE);
	}
}
// if a super admin or a non-logged in user presses the "Finish" or "Login" button - redirect him to the login screen
elseif (hasRequest('cancel') || hasRequest('finish')) {
	CSession::clear();
	redirect('index.php');
}

$theme = CWebUser::$data ? getUserTheme(CWebUser::$data) : TRX_DEFAULT_THEME;

DBclose();

/*
 * Setup wizard
 */
$TRX_SETUP_WIZARD = new CSetupWizard();

// if init fails due to missing configuration, set user as guest with default en_GB language
if (!CWebUser::$data) {
	CWebUser::setDefault();
}

// page title
(new CPageHeader(_('Installation')))
	->addCssFile('assets/styles/'.CHtml::encode($theme).'.css')
	->addJsFile((new CUrl('js/browsers.js'))->getUrl())
	->addJsFile((new CUrl('jsLoader.php'))
		->setArgument('ver', TREEGIX_VERSION)
		->setArgument('lang', CWebUser::$data['lang'])
		->getUrl()
	)
	->display();

/*
 * Displaying
 */
//todo tip

(new CTag('body', true, [(new CTag('main', true, [$TRX_SETUP_WIZARD, $sub_footer]))]))
	->setAttribute('lang', CWebUser::getLang())
	->show();
?>
</html>
