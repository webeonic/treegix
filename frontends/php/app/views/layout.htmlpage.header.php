<?php


global $DB, $TRX_SERVER, $TRX_SERVER_NAME, $TRX_SERVER_PORT;

$page_title = $data['page']['title'];
if (isset($TRX_SERVER_NAME) && $TRX_SERVER_NAME !== '') {
	$page_title = $TRX_SERVER_NAME.NAME_DELIMITER.$page_title;
}

$pageHeader = new CPageHeader($page_title);

$scripts = $data['javascript']['files'];

$theme = TRX_DEFAULT_THEME;
if (!empty($DB['DB'])) {
	$config = select_config();
	$theme = getUserTheme($data['user']);

	$pageHeader->addStyle(getTriggerSeverityCss($config));
	$pageHeader->addStyle(getTriggerStatusCss($config));

	// perform Treegix server check only for standard pages
	if ($config['server_check_interval'] && !empty($TRX_SERVER) && !empty($TRX_SERVER_PORT)) {
		$scripts[] = 'servercheck.js';
	}
}

// Show GUI messages in pages with menus and in fullscreen and kiosk mode.
$show_gui_messaging = (!defined('TRX_PAGE_NO_MENU')
	|| in_array($data['web_layout_mode'], [TRX_LAYOUT_FULLSCREEN, TRX_LAYOUT_KIOSKMODE]))
		? intval(!CWebUser::isGuest())
		: null;

$pageHeader
	->addCssFile('assets/styles/'.CHtml::encode($theme).'.css')
	->addJsBeforeScripts(
		'var PHP_TZ_OFFSET = '.date('Z').','.
			'PHP_TRX_FULL_DATE_TIME = "'.TRX_FULL_DATE_TIME.'";'
	)
	->addJsFile((new CUrl('js/browsers.js'))->getUrl())
	->addJsFile((new CUrl('jsLoader.php'))
		->setArgument('lang', $data['user']['lang'])
		->setArgument('showGuiMessaging', $show_gui_messaging)
		->getUrl()
	);

if ($scripts) {
	$pageHeader->addJsFile((new CUrl('jsLoader.php'))
		->setArgument('lang', $data['user']['lang'])
		->setArgument('files', $scripts)
		->getUrl()
	);
}
$pageHeader->display();

echo '<body lang="'.CWebUser::getLang().'">';
echo '<output class="'.TRX_STYLE_MSG_GLOBAL_FOOTER.' '.TRX_STYLE_MSG_WARNING.'" id="msg-global-footer"></output>';
