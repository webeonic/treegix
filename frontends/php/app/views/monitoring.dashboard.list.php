<?php


if ($data['uncheck']) {
	uncheckTableRows('dashboard');
}
$this->addJsFile('layout.mode.js');

$web_layout_mode = CView::getLayoutMode();

$widget = (new CWidget())
	->setTitle(_('Dashboards'))
	->setWebLayoutMode($web_layout_mode)
	->setControls((new CTag('nav', true,
		(new CList())
			->addItem(new CRedirectButton(_('Create dashboard'),
				(new CUrl('treegix.php'))
					->setArgument('action', 'dashboard.view')
					->setArgument('new', '1')
					->getUrl()
			))
		->addItem(get_icon('fullscreen'))
		))
		->setAttribute('aria-label', _('Content controls'))
	);

$form = (new CForm())->setName('dashboardForm');

$table = (new CTableInfo())
	->setHeader([
		(new CColHeader(
			(new CCheckBox('all_dashboards'))
				->onClick("checkAll('".$form->getName()."', 'all_dashboards', 'dashboardids');")
		))->addClass(TRX_STYLE_CELL_WIDTH),
		make_sorting_header(_('Name'), 'name', $data['sort'], $data['sortorder'])
	]);

$url = (new CUrl('treegix.php'))
	->setArgument('action', 'dashboard.view')
	->setArgument('dashboardid', '');

foreach ($data['dashboards'] as $dashboard) {
	$table->addRow([
		(new CCheckBox('dashboardids['.$dashboard['dashboardid'].']', $dashboard['dashboardid']))
			->setEnabled($dashboard['editable']),
		new CLink($dashboard['name'],
			$url
				->setArgument('dashboardid', $dashboard['dashboardid'])
				->getUrl()
		)
	]);
}

$form->addItem([
	$table,
	$data['paging'],
	new CActionButtonList('action', 'dashboardids', [
		'dashboard.delete' => ['name' => _('Delete'), 'confirm' => _('Delete selected dashboards?')]
	], 'dashboard')
]);

$widget->addItem($form);
$widget->show();
