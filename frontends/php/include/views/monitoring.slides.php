<?php



$web_layout_mode = CView::getLayoutMode();

$widget = (new CWidget())->setWebLayoutMode($web_layout_mode);

if (in_array($web_layout_mode, [TRX_LAYOUT_NORMAL, TRX_LAYOUT_FULLSCREEN])) {
	$widget
		->setTitle(_('Slide shows'))
		->addItem((new CList())
			->setAttribute('role', 'navigation')
			->setAttribute('aria-label', _x('Hierarchy', 'screen reader'))
			->addClass(TRX_STYLE_OBJECT_GROUP)
			->addClass(TRX_STYLE_FILTER_BREADCRUMB)
			->addItem([
				(new CSpan())->addItem(new CLink(_('All slide shows'), 'slideconf.php')),
				'/',
				(new CSpan())
					->addClass(TRX_STYLE_SELECTED)
					->addItem(
						new CLink($data['screen']['name'], (new CUrl('slides.php'))
							->setArgument('elementid', $data['screen']['slideshowid'])
						)
					)
			])
		);
}

$controls = (new CList())
	->addItem(
		new CComboBox('config', 'slides.php', 'redirect(this.options[this.selectedIndex].value);', [
			'screens.php' => _('Screens'),
			'slides.php' => _('Slide shows')
		])
	);

$favourite_icon = get_icon('favourite', [
	'fav' => 'web.favorite.screenids',
	'elname' => 'slideshowid',
	'elid' => $this->data['elementId']
]);

$refresh_icon = get_icon('screenconf');

$refresh_icon->setMenuPopup(CMenuPopupHelper::getRefresh(WIDGET_SLIDESHOW, 'x'.$this->data['refreshMultiplier'],
	true, ['elementid' => $this->data['elementId']]
));

if (isset($this->data['isDynamicItems'])) {
	$controls->addItem([
		new CLabel(_('Group'), 'groupid'),
		(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
		$this->data['pageFilter']->getGroupsCB()
	]);
	$controls->addItem([
		new CLabel(_('Host'), 'hostid'),
		(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
		$this->data['pageFilter']->getHostsCB()
	]);
}

$widget->setControls((new CList([
	(new CForm('get'))
		->setAttribute('aria-label', _('Main filter'))
		->setName('slideHeaderForm')
		->addItem($controls),
	(new CTag('nav', true, (new CList())
		->addItem($data['screen']['editable']
			? (new CButton('edit', _('Edit slide show')))
				->onClick('redirect("slideconf.php?form=update&slideshowid='.$data['screen']['slideshowid'].'")')
			: null
		)
		->addItem($favourite_icon)
		->addItem($refresh_icon)
		->addItem(get_icon('fullscreen'))
	))
		->setAttribute('aria-label', _('Content controls'))
])));

$widget
	->addItem(
		(new CFilter(new CUrl()))
			->setProfile($data['timeline']['profileIdx'], $data['timeline']['profileIdx2'])
			->setActiveTab($data['active_tab'])
			->addTimeSelector($data['timeline']['from'], $data['timeline']['to'],
				$web_layout_mode != TRX_LAYOUT_KIOSKMODE)
	)
	->addItem(
		(new CDiv((new CDiv())->addClass('preloader')))
			->setId(WIDGET_SLIDESHOW)
	);

require_once dirname(__FILE__).'/js/monitoring.slides.js.php';

return $widget;
