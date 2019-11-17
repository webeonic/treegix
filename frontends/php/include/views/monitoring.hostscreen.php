<?php



$web_layout_mode = CView::getLayoutMode();

$screen_widget = (new CWidget())->setWebLayoutMode($web_layout_mode);

if (empty($data['screen']) || empty($data['host'])) {
	$screen_widget
		->setTitle(_('Screens'))
		->addItem(new CTableInfo());
}
else {
	$screen_widget->setTitle([
		$data['screen']['name'].' '._('on').' ',
		new CSpan($data['host']['name'])
	]);

	$url = (new CUrl('host_screen.php'))
		->setArgument('hostid', $data['hostid'])
		->setArgument('screenid', $data['screenid']);

	// host screen list
	if (!empty($data['screens'])) {
		$screen_combobox = new CComboBox('screenList', $url->toString(),
			'javascript: redirect(this.options[this.selectedIndex].value);'
		);
		foreach ($data['screens'] as $screen) {
			$screen_combobox->addItem(
				$url
					->setArgument('screenid', $screen['screenid'])
					->toString(),
				$screen['name']
			);
		}

		$screen_widget->setControls((new CTag('nav', true,
			(new CForm('get'))
				->setAttribute('aria-label', _('Main filter'))
				->addItem((new CList())
					->addItem($screen_combobox)
					->addItem(get_icon('fullscreen'))
				)
			))
				->setAttribute('aria-label', _('Content controls'))
		);
	}

	// append screens to widget
	$screen_builder = new CScreenBuilder([
		'screen' => $data['screen'],
		'mode' => SCREEN_MODE_PREVIEW,
		'hostid' => $data['hostid'],
		'profileIdx' => $data['profileIdx'],
		'profileIdx2' => $data['profileIdx2'],
		'from' => $data['from'],
		'to' => $data['to']
	]);

	$screen_widget->addItem(
		(new CFilter(new CUrl()))
			->setProfile($data['profileIdx'], $data['profileIdx2'])
			->setActiveTab($data['active_tab'])
			->addTimeSelector($screen_builder->timeline['from'], $screen_builder->timeline['to'],
				$web_layout_mode != TRX_LAYOUT_KIOSKMODE)
	);

	$screen_widget->addItem((new CDiv($screen_builder->show()))->addClass(TRX_STYLE_TABLE_FORMS_CONTAINER));

	CScreenBuilder::insertScreenStandardJs($screen_builder->timeline);
}

return $screen_widget;
