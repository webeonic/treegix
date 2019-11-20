<?php


$this->addJsFile('gtlc.js');
$this->addJsFile('flickerfreescreen.js');
$this->addJsFile('layout.mode.js');
$this->addJsFile('multiselect.js');

$widget = (new CWidget())
	->setTitle(_('Status of discovery'))
	->setWebLayoutMode(CView::getLayoutMode())
	->setControls((new CTag('nav', true,
		(new CList())
			->addItem(get_icon('fullscreen'))
		))
			->setAttribute('aria-label', _('Content controls'))
	)
	->addItem((new CFilter((new CUrl('treegix.php'))->setArgument('action', 'discovery.view')))
		->setProfile($data['profileIdx'])
		->setActiveTab($data['active_tab'])
		->addFilterTab(_('Filter'), [
			(new CFormList())
				->addRow(
					(new CLabel(_('Discovery rule'), 'filter_druleids__ms')),
					(new CMultiSelect([
						'name' => 'filter_druleids[]',
						'object_name' => 'drules',
						'data' => $data['filter']['drules'],
						'popup' => [
							'parameters' => [
								'srctbl' => 'drules',
								'srcfld1' => 'druleid',
								'dstfrm' => 'trx_filter',
								'dstfld1' => 'filter_druleids_'
							]
						]
					]))->setWidth(TRX_TEXTAREA_FILTER_STANDARD_WIDTH)
				)
		])
		->addVar('action', 'discovery.view')
	);

$discovery_table = CScreenBuilder::getScreen([
	'resourcetype' => SCREEN_RESOURCE_DISCOVERY,
	'mode' => SCREEN_MODE_JS,
	'dataId' => 'discovery',
	'data' => [
		'filter_druleids' => $data['filter']['druleids'],
		'sort' => $data['sort'],
		'sortorder' => $data['sortorder']
	]
])->get();

$widget->addItem($discovery_table)->show();
