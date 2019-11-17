<?php



// reset the LC_CTYPE locale so that case transformation functions would work correctly
// it is also required for PHP to work with the Turkish locale (https://bugs.php.net/bug.php?id=18556)
// WARNING: this must be done before executing any other code, otherwise code execution could fail!
// this will be unnecessary in PHP 5.5
setlocale(LC_CTYPE, [
	'C', 'POSIX', 'en', 'en_US', 'en_US.UTF-8', 'English_United States.1252', 'en_GB', 'en_GB.UTF-8'
]);

require_once dirname(__FILE__).'/classes/core/Z.php';

try {
	Z::getInstance()->run(ZBase::EXEC_MODE_DEFAULT);
}
catch (DBException $e) {
	(new CView('general.warning', [
		'header' => 'Database error',
		'messages' => [$e->getMessage()],
		'theme' => TRX_DEFAULT_THEME
	]))->render();

	exit;
}
catch (ConfigFileException $e) {
	switch ($e->getCode()) {
		case CConfigFile::CONFIG_NOT_FOUND:
			redirect('setup.php');
			exit;

		case CConfigFile::CONFIG_ERROR:
			(new CView('general.warning', [
				'header' => 'Configuration file error',
				'messages' => [$e->getMessage()],
				'theme' => TRX_DEFAULT_THEME
			]))->render();

			exit;
	}
}
catch (Exception $e) {
	(new CView('general.warning', [
		'header' => $e->getMessage(),
		'messages' => [],
		'theme' => TRX_DEFAULT_THEME
	]))->render();

	exit;
}

CProfiler::getInstance()->start();

global $TRX_SERVER, $TRX_SERVER_PORT, $page;

$page = [
	'title' => null,
	'file' => null,
	'scripts' => null,
	'type' => null,
	'menu' => null
];
