<?php



$url_list = (new CUrl('treegix.php'))->setArgument('action', 'dashboard.list');

$breadcrumbs = [
	(new CSpan())->addItem(new CLink(_('All dashboards'), $url_list->getUrl()))
];

if ($data['dashboard']['dashboardid'] != 0) {
	$url_view = (new CUrl('treegix.php'))
		->setArgument('action', 'dashboard.view')
		->setArgument('dashboardid', $data['dashboard']['dashboardid']);

	$breadcrumbs[] = '/';
	$breadcrumbs[] = (new CSpan())
		->addItem((new CLink($data['dashboard']['name'], $url_view->getUrl()))
			->setId('dashboard-direct-link')
		)
		->addClass(TRX_STYLE_SELECTED);
}

return $breadcrumbs;
