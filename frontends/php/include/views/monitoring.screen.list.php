<?php



$widget = (new CWidget())->setTitle(_('Screens'));
$form = (new CForm('get'))->cleanItems();

$content_control = (new CList())->addItem(new CSubmit('form', _('Create screen')));

if ($data['templateid']) {
	$form->addItem((new CVar('templateid', $data['templateid']))->removeId());
	$widget->addItem(get_header_host_table('screens', $data['templateid']));
}
else {
	$form->addItem((new CList())
		->addItem(
			(new CComboBox('config', 'screens.php', 'redirect(this.options[this.selectedIndex].value);', [
				'screens.php' => _('Screens'),
				'slides.php' => _('Slide shows')
			]))->removeId()
		)
	);
	$content_control->addItem(
		(new CButton('form', _('Import')))
			->onClick('redirect("screen.import.php?rules_preset=screen")')
			->removeId()
	);
}

$form->addItem($content_control);
$widget->setControls((new CTag('nav', true, $form))
	->setAttribute('aria-label', _('Content controls'))
);

// filter
if (!$data['templateid']) {
	$widget->addItem(
		(new CFilter(new CUrl('screenconf.php')))
			->setProfile($data['profileIdx'])
			->setActiveTab($data['active_tab'])
			->addFilterTab(_('Filter'), [
				(new CFormList())->addRow(_('Name'),
					(new CTextBox('filter_name', $data['filter']['name']))
						->setWidth(TRX_TEXTAREA_FILTER_STANDARD_WIDTH)
						->setAttribute('autofocus', 'autofocus')
				)
			])
	);
}

// create form
$screenForm = (new CForm())
	->setName('screenForm')
	->addVar('templateid', $data['templateid']);

// create table
$screenTable = (new CTableInfo())
	->setHeader([
		(new CColHeader(
			(new CCheckBox('all_screens'))->onClick("checkAll('".$screenForm->getName()."', 'all_screens', 'screens');")
		))->addClass(TRX_STYLE_CELL_WIDTH),
		make_sorting_header(_('Name'), 'name', $data['sort'], $data['sortorder']),
		_('Dimension (cols x rows)'),
		_('Actions')
	]);

foreach ($data['screens'] as $screen) {
	$user_type = CWebUser::getType();

	if ($data['templateid'] || $user_type == USER_TYPE_SUPER_ADMIN || $user_type == USER_TYPE_TREEGIX_ADMIN
			|| $screen['editable']) {
		$checkbox = new CCheckBox('screens['.$screen['screenid'].']', $screen['screenid']);
		$action = new CLink(_('Properties'), '?form=update&screenid='.$screen['screenid'].url_param('templateid'));
		$constructor = new CLink(_('Constructor'),
			'screenedit.php?screenid='.$screen['screenid'].url_param('templateid')
		);
	}
	else {
		$checkbox = (new CCheckBox('screens['.$screen['screenid'].']', $screen['screenid']))
			->setAttribute('disabled', 'disabled');
		$action = '';
		$constructor = '';
	}

	$screenTable->addRow([
		$checkbox,
		$data['templateid']
			? $screen['name']
			: new CLink($screen['name'], 'screens.php?elementid='.$screen['screenid']),
		$screen['hsize'].' x '.$screen['vsize'],
		new CHorList([$action, $constructor])
	]);
}

// buttons
$buttons = [];

if (!$data['templateid']) {
	$buttons['screen.export'] = ['name' => _('Export'), 'redirect' =>
		(new CUrl('treegix.php'))
			->setArgument('action', 'export.screens.xml')
			->setArgument('backurl', (new CUrl('screenconf.php'))
				->setArgument('page', getPageNumber())
				->getUrl())
			->getUrl()
	];
}

$buttons['screen.massdelete'] = ['name' => _('Delete'), 'confirm' => _('Delete selected screens?')];

// append table to form
$screenForm->addItem([
	$screenTable,
	$data['paging'],
	new CActionButtonList('action', 'screens', $buttons, $data['templateid'] ? $data['templateid'] : null)
]);

// append form to widget
$widget->addItem($screenForm);

return $widget;
