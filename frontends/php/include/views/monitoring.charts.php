<?php



$controls = (new CForm('get'))
	->cleanItems()
	->setAttribute('aria-label', _('Main filter'))
	->addVar('page', 1)
	->addItem((new CList())
		->addItem([
			new CLabel(_('Group'), 'groupid'),
			(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
			$this->data['pageFilter']->getGroupsCB()
		])
		->addItem([
			new CLabel(_('Host'), 'hostid'),
			(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
			$this->data['pageFilter']->getHostsCB()
		])
		->addItem([
			new CLabel(_('Graph'), 'graphid'),
			(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
			$this->data['pageFilter']->getGraphsCB()
		])
		->addItem([
			new CLabel(_('View as'), 'action'),
			(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
			(new CComboBox('action', $data['action'], 'submit()', $data['actions']))->setEnabled((bool) $data['graphid'])
		])
	);

$content_control = (new CList());

if ($this->data['graphid']) {
	$content_control->addItem(get_icon('favourite', ['fav' => 'web.favorite.graphids', 'elname' => 'graphid',
		'elid' => $this->data['graphid']])
	);
}

$content_control->addItem(get_icon('fullscreen'));
$content_control = (new CTag('nav', true, $content_control))->setAttribute('aria-label', _('Content controls'));

$web_layout_mode = CView::getLayoutMode();

$chartsWidget = (new CWidget())
	->setTitle(_('Graphs'))
	->setWebLayoutMode($web_layout_mode)
	->setControls(new CList([$controls, $content_control]))
	->addItem(
		(new CFilter(new CUrl('charts.php')))
			->setProfile($data['timeline']['profileIdx'], $data['timeline']['profileIdx2'])
			->setActiveTab($data['active_tab'])
			->addTimeSelector($data['timeline']['from'], $data['timeline']['to'],
				$web_layout_mode != TRX_LAYOUT_KIOSKMODE)
	);

if (!empty($this->data['graphid'])) {
	// append chart to widget

	if ($data['action'] === HISTORY_VALUES) {
		$screen = CScreenBuilder::getScreen([
			'resourcetype' => SCREEN_RESOURCE_HISTORY,
			'action' => HISTORY_VALUES,
			'graphid' => $data['graphid'],
			'profileIdx' => $data['timeline']['profileIdx'],
			'profileIdx2' => $data['timeline']['profileIdx2'],
			'from' => $data['timeline']['from'],
			'to' => $data['timeline']['to']
		]);
	}
	else {
		$screen = CScreenBuilder::getScreen([
			'resourcetype' => SCREEN_RESOURCE_CHART,
			'graphid' => $this->data['graphid'],
			'profileIdx' => $data['timeline']['profileIdx'],
			'profileIdx2' => $data['timeline']['profileIdx2']
		]);
	}

	$chartTable = (new CTable())
		->setAttribute('style', 'width: 100%;')
		->addRow($screen->get());

	$chartsWidget->addItem($chartTable);

	CScreenBuilder::insertScreenStandardJs($screen->timeline);
}
else {
	$screen = new CScreenBuilder();
	CScreenBuilder::insertScreenStandardJs($screen->timeline);

	$chartsWidget->addItem(new CTableInfo());
}

return $chartsWidget;
