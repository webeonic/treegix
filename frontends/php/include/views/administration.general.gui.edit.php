<?php



require_once dirname(__FILE__).'/js/administration.general.gui.php';

$widget = (new CWidget())
	->setTitle(_('GUI'))
	->setControls((new CTag('nav', true,
		(new CForm())
			->cleanItems()
			->addItem((new CList())
				->addItem(makeAdministrationGeneralMenu('adm.gui.php'))
			)
		))
			->setAttribute('aria-label', _('Content controls'))
	);

$guiTab = (new CFormList())
	->addRow(_('Default theme'),
		(new CComboBox('default_theme', $data['default_theme'], null, Z::getThemes()))
			->setAttribute('autofocus', 'autofocus')
	)
	->addRow(_('Dropdown first entry'), [
		new CComboBox('dropdown_first_entry', $data['dropdown_first_entry'], null, [
			TRX_DROPDOWN_FIRST_NONE => _('None'),
			TRX_DROPDOWN_FIRST_ALL => _('All')
		]),
		(new CDiv())->addClass(TRX_STYLE_FORM_INPUT_MARGIN),
		(new CCheckBox('dropdown_first_remember'))
			->setLabel(_('remember selected'))
			->setChecked($data['dropdown_first_remember'] == 1)
	])
	->addRow((new CLabel(_('Limit for search and filter results'), 'search_limit'))->setAsteriskMark(),
		(new CNumericBox('search_limit', $data['search_limit'], 6))
			->setAriaRequired()
			->setWidth(TRX_TEXTAREA_NUMERIC_STANDARD_WIDTH)
	)
	->addRow((new CLabel(_('Max count of elements to show inside table cell'), 'max_in_table'))->setAsteriskMark(),
		(new CNumericBox('max_in_table', $data['max_in_table'], 5))
			->setAriaRequired()
			->setWidth(TRX_TEXTAREA_NUMERIC_STANDARD_WIDTH)
	)
	->addRow(_('Show warning if Treegix server is down'),
		(new CCheckBox('server_check_interval', SERVER_CHECK_INTERVAL))
			->setChecked($data['server_check_interval'] == SERVER_CHECK_INTERVAL)
	);

$guiView = (new CTabView())
	->addTab('gui', _('GUI'), $guiTab)
	->setFooter(makeFormFooter(new CSubmit('update', _('Update'))));

$guiForm = (new CForm())
	->setAttribute('aria-labeledby', TRX_STYLE_PAGE_TITLE)
	->addItem($guiView);

$widget->addItem($guiForm);

return $widget;
