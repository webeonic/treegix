<?php


$widget = (new CWidget())
	->setTitle(_('Slide shows'))
	->setControls((new CTag('nav', true,
		(new CForm('get'))
			->cleanItems()
			->addItem(
				(new CList())
					->addItem(
						new CComboBox('config', 'slides.php', 'redirect(this.options[this.selectedIndex].value);', [
							'screens.php' => _('Screens'),
							'slides.php' => _('Slide shows')
						])
					)
					->addItem(new CSubmit('form', _('Create slide show')))
			)
		))
			->setAttribute('aria-label', _('Content controls'))
	);

// filter
$widget->addItem(
	(new CFilter(new CUrl('slideconf.php')))
		->setProfile($data['profileIdx'])
		->setActiveTab($data['active_tab'])
		->addFilterTab(_('Filter'), [
			(new CFormList())->addRow(_('Name'),
				(new CTextBox('filter_name', $data['filter']['name']))
					->setWidth(ZBX_TEXTAREA_FILTER_STANDARD_WIDTH)
					->setAttribute('autofocus', 'autofocus')
			)
		])
);

// Create form.
$form = (new CForm())->setName('slideForm');

// create table
$slidesTable = (new CTableInfo())
	->setHeader([
		(new CColHeader(
			(new CCheckBox('all_shows'))->onClick("checkAll('".$form->getName()."', 'all_shows', 'shows');")
		))->addClass(ZBX_STYLE_CELL_WIDTH),
		make_sorting_header(_('Name'), 'name', $this->data['sort'], $this->data['sortorder']),
		make_sorting_header(_('Delay'), 'delay', $this->data['sort'], $this->data['sortorder']),
		make_sorting_header(_('Number of slides'), 'cnt', $this->data['sort'], $this->data['sortorder']),
		_('Actions')
	]);

foreach ($this->data['slides'] as $slide) {
	$user_type = CWebUser::getType();

	if ($user_type == USER_TYPE_SUPER_ADMIN || $user_type == USER_TYPE_TREEGIX_ADMIN || $slide['editable']) {
		$checkbox = new CCheckBox('shows['.$slide['slideshowid'].']', $slide['slideshowid']);
		$properties = (new CLink(_('Properties'), '?form=update&slideshowid='.$slide['slideshowid']))
			->addClass('action');
	}
	else {
		$checkbox = (new CCheckBox('shows['.$slide['slideshowid'].']', $slide['slideshowid']))
			->setAttribute('disabled', 'disabled');
		$properties = '';
	}

	$slidesTable->addRow([
		$checkbox,
		(new CLink($slide['name'], 'slides.php?elementid='.$slide['slideshowid']))->addClass('action'),
		$slide['delay'],
		$slide['cnt'],
		$properties
	]);
}

// append table to form
$form->addItem([
	$slidesTable,
	$this->data['paging'],
	new CActionButtonList('action', 'shows', [
		'slideshow.massdelete' => ['name' => _('Delete'), 'confirm' => _('Delete selected slide shows?')]
	])
]);

// append form to widget
$widget->addItem($form);

return $widget;
