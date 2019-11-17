<?php



$widget = (new CWidget())
	->setTitle(_('Regular expressions'))
	->setControls((new CTag('nav', true,
		(new CForm())
			->cleanItems()
			->addItem((new CList())
				->addItem(makeAdministrationGeneralMenu('adm.regexps.php'))
				->addItem(new CSubmit('form', _('New regular expression')))
			)
		))
			->setAttribute('aria-label', _('Content controls'))
	);

$form = (new CForm())->setName('regularExpressionsForm');

$regExpTable = (new CTableInfo())
	->setHeader([
		(new CColHeader(
			(new CCheckBox('all_regexps'))->onClick("checkAll('".$form->getName()."', 'all_regexps', 'regexpids');")
		))->addClass(TRX_STYLE_CELL_WIDTH),
		_('Name'),
		_('Expressions')
	]);

$expressions = [];
$values = [];
foreach($data['db_exps'] as $exp) {
	if (!isset($expressions[$exp['regexpid']])) {
		$values[$exp['regexpid']] = 1;
	}
	else {
		$values[$exp['regexpid']]++;
	}

	if (!isset($expressions[$exp['regexpid']])) {
		$expressions[$exp['regexpid']] = new CTable();
	}

	$expressions[$exp['regexpid']]->addRow([
		new CCol($values[$exp['regexpid']]),
		new CCol(' &raquo; '),
		new CCol($exp['expression']),
		new CCol(' ['.expression_type2str($exp['expression_type']).']')
	]);
}
foreach($data['regexps'] as $regexpid => $regexp) {
	$regExpTable->addRow([
		new CCheckBox('regexpids['.$regexp['regexpid'].']', $regexp['regexpid']),
		new CLink($regexp['name'], 'adm.regexps.php?form=update'.'&regexpid='.$regexp['regexpid']),
		isset($expressions[$regexpid]) ? $expressions[$regexpid] : ''
	]);
}

// append table to form
$form->addItem([
	$regExpTable,
	new CActionButtonList('action', 'regexpids', [
		'regexp.massdelete' => ['name' => _('Delete'), 'confirm' => _('Delete selected regular expressions?')]
	])
]);

// append form to widget
$widget->addItem($form);

return $widget;
