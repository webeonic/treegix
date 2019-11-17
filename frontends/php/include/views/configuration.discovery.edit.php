<?php



require_once dirname(__FILE__).'/js/configuration.discovery.edit.js.php';

$widget = (new CWidget())->setTitle(_('Discovery rules'));

// create form
$discoveryForm = (new CForm())
	->setName('discoveryForm')
	->setAttribute('aria-labeledby', TRX_STYLE_PAGE_TITLE)
	->addVar('form', $this->data['form']);
if (!empty($this->data['druleid'])) {
	$discoveryForm->addVar('druleid', $this->data['druleid']);
}

// create form list
$discoveryFormList = (new CFormList())
	->addRow(
		(new CLabel(_('Name'), 'name'))->setAsteriskMark(),
		(new CTextBox('name', $this->data['drule']['name']))
			->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired()
			->setAttribute('autofocus', 'autofocus')
	);

// append proxy to form list
$proxyComboBox = (new CComboBox('proxy_hostid', $this->data['drule']['proxy_hostid']))
	->addItem(0, _('No proxy'));
foreach ($this->data['proxies'] as $proxy) {
	$proxyComboBox->addItem($proxy['proxyid'], $proxy['host']);
}

$discoveryFormList
	->addRow(_('Discovery by proxy'), $proxyComboBox)
	->addRow((new CLabel(_('IP range'), 'iprange'))->setAsteriskMark(),
		(new CTextArea('iprange', $this->data['drule']['iprange'], ['maxlength' => 2048]))
			->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired()
	)
	->addRow((new CLabel(_('Update interval'), 'delay'))->setAsteriskMark(),
		(new CTextBox('delay', $data['drule']['delay']))
			->setWidth(TRX_TEXTAREA_SMALL_WIDTH)
			->setAriaRequired()
	);

$discoveryFormList->addRow(
	(new CLabel(_('Checks'), 'dcheckList'))->setAsteriskMark(),
	(new CDiv(
		(new CTable())
			->setAttribute('style', 'width: 100%;')
			->setFooter(
				(new CRow(
					(new CCol(
						(new CButton('newCheck', _('New')))->addClass(TRX_STYLE_BTN_LINK)
					))->setColSpan(2)
				))->setId('dcheckListFooter')
			)
	))
		->addClass(TRX_STYLE_TABLE_FORMS_SEPARATOR)
		->setAttribute('style', 'width: '.TRX_TEXTAREA_STANDARD_WIDTH.'px;')
		->setId('dcheckList')
);

// append uniqueness criteria to form list
$discoveryFormList->addRow(_('Device uniqueness criteria'),
	(new CDiv(
		(new CRadioButtonList('uniqueness_criteria', (int) $this->data['drule']['uniqueness_criteria']))
			->makeVertical()
			->addValue(_('IP address'), -1, zbx_formatDomId('uniqueness_criteria_ip'))
	))
		->setAttribute('style', 'width: '.TRX_TEXTAREA_STANDARD_WIDTH.'px;')
		->addClass(TRX_STYLE_TABLE_FORMS_SEPARATOR)
);

// append host source to form list
$discoveryFormList->addRow(_('Host name'),
	(new CDiv(
		(new CRadioButtonList('host_source', $this->data['drule']['host_source']))
			->makeVertical()
			->addValue(_('DNS name'), TRX_DISCOVERY_DNS, 'host_source_chk_dns')
			->addValue(_('IP address'), TRX_DISCOVERY_IP, 'host_source_chk_ip')
	))
		->setAttribute('style', 'min-width: '.TRX_TEXTAREA_STANDARD_WIDTH.'px;')
		->addClass(TRX_STYLE_TABLE_FORMS_SEPARATOR)
);

// append name source to form list
$discoveryFormList->addRow(_('Visible name'),
	(new CDiv(
		(new CRadioButtonList('name_source', $this->data['drule']['name_source']))
			->makeVertical()
			->addValue(_('Host name'), TRX_DISCOVERY_UNSPEC, 'name_source_chk_host')
			->addValue(_('DNS name'), TRX_DISCOVERY_DNS, 'name_source_chk_dns')
			->addValue(_('IP address'), TRX_DISCOVERY_IP, 'name_source_chk_ip')
	))
		->setAttribute('style', 'min-width: '.TRX_TEXTAREA_STANDARD_WIDTH.'px;')
		->addClass(TRX_STYLE_TABLE_FORMS_SEPARATOR)
);

// append status to form list
$status = (empty($this->data['druleid']) && empty($this->data['form_refresh']))
	? true
	: ($this->data['drule']['status'] == DRULE_STATUS_ACTIVE);

$discoveryFormList->addRow(_('Enabled'), (new CCheckBox('status'))->setChecked($status));

// append tabs to form
$discoveryTabs = (new CTabView())->addTab('druleTab', _('Discovery rule'), $discoveryFormList);

// append buttons to form
if (isset($this->data['druleid']))
{
	$discoveryTabs->setFooter(makeFormFooter(
		new CSubmit('update', _('Update')),
		[
			new CButton('clone', _('Clone')),
			new CButtonDelete(_('Delete discovery rule?'), url_param('form').url_param('druleid')),
			new CButtonCancel()
		]
	));
}
else {
	$discoveryTabs->setFooter(makeFormFooter(
		new CSubmit('add', _('Add')),
		[new CButtonCancel()]
	));
}

$discoveryForm->addItem($discoveryTabs);

$widget->addItem($discoveryForm);

return $widget;
