<?php



require_once dirname(__FILE__).'/include/config.inc.php';

$page['title'] = _('Configuration of macros');
$page['file'] = 'adm.macros.php';
$page['scripts'] = ['textareaflexible.js'];

require_once dirname(__FILE__).'/include/page_header.php';

// VAR	TYPE	OPTIONAL	FLAGS	VALIDATION	EXCEPTION
$fields = [
	'macros'		=> [T_ZBX_STR, O_OPT, P_SYS,			null,	null],
	// actions
	'update'		=> [T_ZBX_STR, O_OPT, P_SYS|P_ACT,	null,	null],
	'form_refresh'	=> [T_ZBX_INT, O_OPT,	null,	null,	null]
];
check_fields($fields);

/*
 * Actions
 */
if (hasRequest('update')) {
	$dbMacros = API::UserMacro()->get([
		'output' => ['globalmacroid', 'macro', 'value', 'description'],
		'globalmacro' => true,
		'preservekeys' => true
	]);

	$macros = getRequest('macros', []);

	// remove empty new macro lines
	foreach ($macros as $idx => $macro) {
		if (!array_key_exists('globalmacroid', $macro) && $macro['macro'] === '' && $macro['value'] === ''
				&& $macro['description'] === '') {
			unset($macros[$idx]);
		}
	}

	// update
	$macrosToUpdate = [];
	foreach ($macros as $idx => $macro) {
		if (array_key_exists('globalmacroid', $macro) && array_key_exists($macro['globalmacroid'], $dbMacros)) {
			$dbMacro = $dbMacros[$macro['globalmacroid']];

			// remove item from new macros array
			unset($macros[$idx], $dbMacros[$macro['globalmacroid']]);

			// if the macro is unchanged - skip it
			if ($dbMacro['macro'] === $macro['macro'] && $dbMacro['value'] === $macro['value']
					&& $dbMacro['description'] === $macro['description']) {
				continue;
			}

			$macrosToUpdate[] = $macro;
		}
	}

	$result = true;

	if ($macrosToUpdate || $dbMacros || $macros) {
		DBstart();

		// update
		if ($macrosToUpdate) {
			$result = (bool) API::UserMacro()->updateGlobal($macrosToUpdate);
		}

		// deletehe
		if ($dbMacros) {
			$result = $result && (bool) API::UserMacro()->deleteGlobal(array_keys($dbMacros));
		}

		// create
		if ($macros) {
			$result = $result && (bool) API::UserMacro()->createGlobal(array_values($macros));
		}

		$result = DBend($result);
	}

	show_messages($result, _('Macros updated'), _('Cannot update macros'));

	if ($result) {
		unset($_REQUEST['form_refresh']);
	}
}

/*
 * Display
 */
$data = [];

if (hasRequest('form_refresh')) {
	$data['macros'] = getRequest('macros', []);
}
else {
	$data['macros'] = API::UserMacro()->get([
		'output' => ['globalmacroid', 'macro', 'value', 'description'],
		'globalmacro' => true
	]);
	$data['macros'] = array_values(order_macros($data['macros'], 'macro'));
}

if (!$data['macros']) {
	$data['macros'][] = ['macro' => '', 'value' => '', 'description' => ''];
}

(new CView('administration.general.macros.edit', $data))->render()->show();

require_once dirname(__FILE__).'/include/page_footer.php';
