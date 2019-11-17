<?php



$widget = (new CWidget())->setTitle(_('Screens').': '.$data['screen']['name']);

if ($data['screen']['templateid']) {
	$widget->addItem(get_header_host_table('screens', $data['screen']['templateid']));
}

$screenBuilder = new CScreenBuilder([
	'isFlickerfree' => false,
	'screen' => $data['screen'],
	'mode' => SCREEN_MODE_EDIT
]);

$widget->addItem(
	(new CDiv($screenBuilder->show()))->addClass(TRX_STYLE_TABLE_FORMS_CONTAINER)
);

$screenBuilder->insertInitScreenJs($data['screenid']);
$screenBuilder->insertProcessObjectsJs();

return $widget;
