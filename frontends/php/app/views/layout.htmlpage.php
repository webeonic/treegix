<?php


require_once 'include/menu.inc.php';

function local_generateHeader($data) {
	// only needed for trx_construct_menu
	global $page;

	header('Content-Type: text/html; charset=UTF-8');
	header('X-Content-Type-Options: nosniff');
	header('X-XSS-Protection: 1; mode=block');

	if (X_FRAME_OPTIONS !== null) {
		if (strcasecmp(X_FRAME_OPTIONS, 'SAMEORIGIN') == 0 || strcasecmp(X_FRAME_OPTIONS, 'DENY') == 0) {
			$x_frame_options = X_FRAME_OPTIONS;
		}
		else {
			$x_frame_options = 'SAMEORIGIN';
			$allowed_urls = explode(',', X_FRAME_OPTIONS);
			$url_to_check = array_key_exists('HTTP_REFERER', $_SERVER)
				? parse_url($_SERVER['HTTP_REFERER'], PHP_URL_HOST)
				: null;

			if ($url_to_check) {
				foreach ($allowed_urls as $allowed_url) {
					if (strcasecmp(trim($allowed_url), $url_to_check) == 0) {
						$x_frame_options = 'ALLOW-FROM '.$allowed_url;
						break;
					}
				}
			}
		}

		header('X-Frame-Options: '.$x_frame_options);
	}


	// construct menu
	$main_menu = [];
	$sub_menus = [];

	trx_construct_menu($main_menu, $sub_menus, $page, $data['controller']['action']);

	$pageHeader = new CView('layout.htmlpage.header', [
		'javascript' => [
			'files' => $data['javascript']['files']
		],
		'page' => [
			'title' => $data['page']['title']
		],
		'user' => [
			'lang' => CWebUser::$data['lang'],
			'theme' => CWebUser::$data['theme']
		],
		'web_layout_mode' => $data['web_layout_mode']
	]);
	echo $pageHeader->getOutput();

	if ($data['web_layout_mode'] === TRX_LAYOUT_NORMAL) {
		global $TRX_SERVER_NAME;

		$pageMenu = new CView('layout.htmlpage.menu', [
			'server_name' => isset($TRX_SERVER_NAME) ? $TRX_SERVER_NAME : '',
			'menu' => [
				'main_menu' => $main_menu,
				'sub_menus' => $sub_menus,
				'selected' => $page['menu']
			],
			'user' => [
				'is_guest' => CWebUser::isGuest(),
				'alias' => CWebUser::$data['alias'],
				'name' => CWebUser::$data['name'],
				'surname' => CWebUser::$data['surname']
			],
		]);
		echo $pageMenu->getOutput();
	}

	echo '<main'.(CView::getLayoutMode() === TRX_LAYOUT_KIOSKMODE ? ' class="'.TRX_STYLE_LAYOUT_KIOSKMODE.'"' : '').'>';

	// if a user logs in after several unsuccessful attempts, display a warning
	if ($failedAttempts = CProfile::get('web.login.attempt.failed', 0)) {
		$attempt_ip = CProfile::get('web.login.attempt.ip', '');
		$attempt_date = CProfile::get('web.login.attempt.clock', 0);

		$error_msg = _n('%4$s failed login attempt logged. Last failed attempt was from %1$s on %2$s at %3$s.',
			'%4$s failed login attempts logged. Last failed attempt was from %1$s on %2$s at %3$s.',
			$attempt_ip,
			trx_date2str(DATE_FORMAT, $attempt_date),
			trx_date2str(TIME_FORMAT, $attempt_date),
			$failedAttempts
		);
		error($error_msg);

		CProfile::update('web.login.attempt.failed', 0, PROFILE_TYPE_INT);
	}

	show_messages();
}

function local_generateFooter($data) {
	$pageFooter = new CView('layout.htmlpage.footer', [
		'user' => [
			'alias' => CWebUser::$data['alias'],
			'debug_mode' => CWebUser::$data['debug_mode']
		],
		'web_layout_mode' => $data['web_layout_mode']
	]);
	echo '</main>'."\n";
	echo $pageFooter->getOutput();
}

function local_showMessage() {
	global $TRX_MESSAGES;

	if (CSession::keyExists('messageOk') || CSession::keyExists('messageError')) {
		if (CSession::keyExists('messages')) {
			$TRX_MESSAGES = CSession::getValue('messages');
			CSession::unsetValue(['messages']);
		}

		if (CSession::keyExists('messageOk')) {
			show_messages(true, CSession::getValue('messageOk'));
		}
		else {
			show_messages(false, null, CSession::getValue('messageError'));
		}

		CSession::unsetValue(['messageOk', 'messageError']);
	}
}

$data['web_layout_mode'] = CView::getLayoutMode();

local_generateHeader($data);
local_showMessage();
echo $data['javascript']['pre'];
echo $data['main_block'];
echo $data['javascript']['post'];
local_generateFooter($data);
show_messages();

echo '</body></html>';
