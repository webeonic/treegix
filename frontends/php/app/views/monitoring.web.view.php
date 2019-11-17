<?php



$this->addJsFile('gtlc.js');
$this->addJsFile('flickerfreescreen.js');
$this->addJsFile('layout.mode.js');

(new CWidget())
	->setTitle(_('Web monitoring'))
	->setWebLayoutMode(CView::getLayoutMode())
	->setControls((new CList([
		(new CForm('get'))
			->cleanItems()
			->setAttribute('aria-label', _('Main filter'))
			->addVar('action', 'web.view')
			->addItem((new CList())
				->addItem([
					new CLabel(_('Group'), 'groupid'),
					(new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN),
					$data['pageFilter']->getGroupsCB()
				])
				->addItem([
					new CLabel(_('Host'), 'hostid'),
					(new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN),
					$data['pageFilter']->getHostsCB()
				])
			),
		(new CTag('nav', true, get_icon('fullscreen')))
			->setAttribute('aria-label', _('Content controls'))
		])))
	->addItem(
		CScreenBuilder::getScreen([
			'resourcetype' => SCREEN_RESOURCE_HTTPTEST,
			'mode' => SCREEN_MODE_JS,
			'dataId' => 'httptest',
			'groupid' => $data['pageFilter']->groupids,
			'hostid' => $data['pageFilter']->hostid,
			'page' => $data['page'],
			'data' => [
				'hosts_selected' => $data['pageFilter']->hostsSelected,
				'sort' => $data['sort'],
				'sortorder' => $data['sortorder'],
				'groupid' => $data['pageFilter']->groupid
			]
		])->get()
	)
	->show();
