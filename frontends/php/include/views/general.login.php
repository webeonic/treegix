<?php



define('TRX_PAGE_NO_HEADER', 1);
define('TRX_PAGE_NO_FOOTER', 1);
define('TRX_PAGE_NO_MENU', true);

require_once dirname(__FILE__).'/../page_header.php';
$error = null;

if ($data['error']) {
	// remove debug code for login form message, trimming not in regex to relay only on [ ] in debug message.
	$message = trim(preg_replace('/\[.*\]/', '', $data['error']['message']));
	$error = (new CDiv($message))->addClass(TRX_STYLE_RED);
}

$http_login_link = $data['http_login_url']
	? (new CListItem(new CLink(_('Sign in with HTTP'), $data['http_login_url'])))->addClass(TRX_STYLE_SIGN_IN_TXT)
	: null;


global $TRX_SERVER_NAME;

(new CTag('main', true, [
	(isset($TRX_SERVER_NAME) && $TRX_SERVER_NAME !== '')
		? (new CDiv($TRX_SERVER_NAME))->addClass(TRX_STYLE_SERVER_NAME)
		: null,
	(new CDiv([
		(new CDiv())
			->addClass(TRX_STYLE_SIGNIN_LOGO)
			->addStyle(CBrandHelper::getLogoStyle()),
		(new CForm())
			->cleanItems()
			->setAttribute('aria-label', _('Sign in'))
			->addItem(hasRequest('request') ? new CVar('request', getRequest('request')) : null)
			->addItem(
				(new CList())
					->addItem([
						new CLabel(_('Username'), 'name'),
						(new CTextBox('name'))->setAttribute('autofocus', 'autofocus'),
						$error
					])
					->addItem([new CLabel(_('Password'), 'password'), (new CTextBox('password'))->setType('password')])
					->addItem(
						(new CCheckBox('autologin'))
							->setLabel(_('Remember me for 30 days'))
							->setChecked($data['autologin'])
					)
					->addItem(new CSubmit('enter', _('Sign in')))
					->addItem($http_login_link)
			)
	]))->addClass(TRX_STYLE_SIGNIN_CONTAINER),
]))->show();

?>
</body>
