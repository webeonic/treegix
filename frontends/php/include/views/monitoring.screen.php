<?php



$web_layout_mode = CView::getLayoutMode();

$widget = (new CWidget())->setWebLayoutMode($web_layout_mode);

if (in_array($web_layout_mode, [TRX_LAYOUT_NORMAL, TRX_LAYOUT_FULLSCREEN])) {
	$widget
		->setTitle(_('Screens'))
		->addItem((new CList())
			->setAttribute('role', 'navigation')
			->setAttribute('aria-label', _x('Hierarchy', 'screen reader'))
			->addClass(TRX_STYLE_OBJECT_GROUP)
			->addClass(TRX_STYLE_FILTER_BREADCRUMB)
			->addItem([
				(new CSpan())->addItem(new CLink(_('All screens'), 'screenconf.php')),
				'/',
				(new CSpan())
					->addClass(TRX_STYLE_SELECTED)
					->addItem(
						new CLink($data['screen']['name'], (new CUrl('screens.php'))
							->setArgument('elementid', $data['screen']['screenid'])
					))
		]));
}

$controls = (new CList())
	->addItem(
		(new CComboBox('config', 'screens.php', 'redirect(this.options[this.selectedIndex].value);', [
			'screens.php' => _('Screens'),
			'slides.php' => _('Slide shows')
		]))->removeId()
);

if (check_dynamic_items($data['screen']['screenid'], 0)) {
	$pageFilter = new CPageFilter([
		'groups' => [
			'monitored_hosts' => true,
			'with_items' => true
		],
		'hosts' => [
			'monitored_hosts' => true,
			'with_items' => true,
			'DDFirstLabel' => _('not selected')
		],
		'hostid' => getRequest('hostid'),
		'groupid' => getRequest('groupid')
	]);
	$_REQUEST['groupid'] = $pageFilter->groupid;
	$_REQUEST['hostid'] = $pageFilter->hostid;

	$controls
		->addItem([
			new CLabel(_('Group'), 'groupid'),
			(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
			$pageFilter->getGroupsCB()
		])
		->addItem([
			new CLabel(_('Host'), 'hostid'),
			(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
			$pageFilter->getHostsCB()
		]);
}

$controls
	->addItem($data['screen']['editable']
		? (new CButton('edit', _('Edit screen')))
			->onClick('redirect("screenedit.php?screenid='.$data['screen']['screenid'].'", "get", "", false, false)')
		: null
	)
	->addItem(get_icon('favourite', [
			'fav' => 'web.favorite.screenids',
			'elname' => 'screenid',
			'elid' => $data['screen']['screenid']
		]
	))
	->addItem(get_icon('fullscreen'));

$widget->setControls((new CTag('nav', true, (new CList())
	->addItem((new CForm('get'))
		->setName('headerForm')
		->addItem($controls)
	)))
		->setAttribute('aria-label', _('Content controls'))
);

// Append screens to widget.
$screenBuilder = new CScreenBuilder([
	'screenid' => $data['screen']['screenid'],
	'mode' => SCREEN_MODE_PREVIEW,
	'groupid' => getRequest('groupid'),
	'hostid' => getRequest('hostid'),
	'profileIdx' => $data['profileIdx'],
	'profileIdx2' => $data['profileIdx2'],
	'from' => $data['from'],
	'to' => $data['to']
]);

$widget->addItem(
	(new CFilter(new CUrl()))
		->setProfile($data['profileIdx'], $data['profileIdx2'])
		->setActiveTab($data['active_tab'])
		->addTimeSelector($screenBuilder->timeline['from'], $screenBuilder->timeline['to'],
			$web_layout_mode != TRX_LAYOUT_KIOSKMODE)
);

$widget->addItem((new CDiv($screenBuilder->show()))->addClass(TRX_STYLE_TABLE_FORMS_CONTAINER));

CScreenBuilder::insertScreenStandardJs($screenBuilder->timeline);

return $widget;
