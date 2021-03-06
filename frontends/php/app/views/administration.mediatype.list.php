<?php



if ($data['uncheck']) {
	uncheckTableRows('mediatype');
}

$widget = (new CWidget())
	->setTitle(_('Media types'))
	->setControls((new CTag('nav', true,
		(new CList())
			->addItem(new CRedirectButton(_('Create media type'), 'treegix.php?action=mediatype.edit'))
			->addItem(new CRedirectButton(_('Import'), 'conf.import.php?rules_preset=mediatype'))
		))
			->setAttribute('aria-label', _('Content controls'))
	)
	->addItem((new CFilter((new CUrl('treegix.php'))->setArgument('action', 'mediatype.list')))
		->setProfile($data['profileIdx'])
		->setActiveTab($data['active_tab'])
		->addFilterTab(_('Filter'), [
			(new CFormList())->addRow(_('Name'),
				(new CTextBox('filter_name', $data['filter']['name']))
					->setWidth(TRX_TEXTAREA_FILTER_SMALL_WIDTH)
					->setAttribute('autofocus', 'autofocus')
			),
			(new CFormList())->addRow(_('Status'),
				(new CRadioButtonList('filter_status', (int) $data['filter']['status']))
					->addValue(_('Any'), -1)
					->addValue(_('Enabled'), MEDIA_TYPE_STATUS_ACTIVE)
					->addValue(_('Disabled'), MEDIA_TYPE_STATUS_DISABLED)
					->setModern(true)
			)
		])
		->addVar('action', 'mediatype.list')
	);

// create form
$mediaTypeForm = (new CForm())->setName('mediaTypesForm');

// create table
$mediaTypeTable = (new CTableInfo())
	->setHeader([
		(new CColHeader(
			(new CCheckBox('all_media_types'))
				->onClick("checkAll('".$mediaTypeForm->getName()."', 'all_media_types', 'mediatypeids');")
		))->addClass(TRX_STYLE_CELL_WIDTH),
		make_sorting_header(_('Name'), 'name', $data['sort'], $data['sortorder']),
		make_sorting_header(_('Type'), 'type', $data['sort'], $data['sortorder']),
		_('Status'),
		_('Used in actions'),
		_('Details'),
		_('Action')
	]);

foreach ($data['mediatypes'] as $mediaType) {
	switch ($mediaType['typeid']) {
		case MEDIA_TYPE_EMAIL:
			$details =
				_('SMTP server').NAME_DELIMITER.'"'.$mediaType['smtp_server'].'", '.
				_('SMTP helo').NAME_DELIMITER.'"'.$mediaType['smtp_helo'].'", '.
				_('SMTP email').NAME_DELIMITER.'"'.$mediaType['smtp_email'].'"';
			break;

		case MEDIA_TYPE_EXEC:
			$details = _('Script name').NAME_DELIMITER.'"'.$mediaType['exec_path'].'"';
			break;

		case MEDIA_TYPE_SMS:
			$details = _('GSM modem').NAME_DELIMITER.'"'.$mediaType['gsm_modem'].'"';
			break;

		default:
			$details = '';
			break;
	}

	// action list
	$actionLinks = [];
	if (!empty($mediaType['listOfActions'])) {
		foreach ($mediaType['listOfActions'] as $action) {
			$actionLinks[] = new CLink($action['name'], 'actionconf.php?form=update&actionid='.$action['actionid']);
			$actionLinks[] = ', ';
		}
		array_pop($actionLinks);
	}
	else {
		$actionLinks = '';
	}
	$actionColumn = new CCol($actionLinks);
	$actionColumn->setAttribute('style', 'white-space: normal;');

	$statusLink = 'treegix.php'.
		'?action='.($mediaType['status'] == MEDIA_TYPE_STATUS_DISABLED
			? 'mediatype.enable'
			: 'mediatype.disable'
		).
		'&mediatypeids[]='.$mediaType['mediatypeid'];

	$status = (MEDIA_TYPE_STATUS_ACTIVE == $mediaType['status'])
		? (new CLink(_('Enabled'), $statusLink))
			->addClass(TRX_STYLE_LINK_ACTION)
			->addClass(TRX_STYLE_GREEN)
			->addSID()
		: (new CLink(_('Disabled'), $statusLink))
			->addClass(TRX_STYLE_LINK_ACTION)
			->addClass(TRX_STYLE_RED)
			->addSID();

	$test_link = (new CButton('mediatypetest_edit', _('Test')))
		->addClass(TRX_STYLE_BTN_LINK)
		->setEnabled(MEDIA_TYPE_STATUS_ACTIVE == $mediaType['status'])
		->onClick('return PopUp("popup.mediatypetest.edit",'.CJs::encodeJson([
			'mediatypeid' => $mediaType['mediatypeid']
		]).', "mediatypetest_edit", this);');

	$name = new CLink($mediaType['name'], '?action=mediatype.edit&mediatypeid='.$mediaType['mediatypeid']);

	// append row
	$mediaTypeTable->addRow([
		new CCheckBox('mediatypeids['.$mediaType['mediatypeid'].']', $mediaType['mediatypeid']),
		(new CCol($name))->addClass(TRX_STYLE_NOWRAP),
		media_type2str($mediaType['typeid']),
		$status,
		$actionColumn,
		$details,
		$test_link
	]);
}

// append table to form
$mediaTypeForm->addItem([
	$mediaTypeTable,
	$data['paging'],
	new CActionButtonList('action', 'mediatypeids', [
		'mediatype.enable' => ['name' => _('Enable'), 'confirm' => _('Enable selected media types?')],
		'mediatype.disable' => ['name' => _('Disable'), 'confirm' => _('Disable selected media types?')],
		'mediatype.export' => ['name' => _('Export'), 'redirect' =>
			(new CUrl('treegix.php'))
				->setArgument('action', 'export.mediatypes.xml')
				->setArgument('backurl', (new CUrl('treegix.php'))
					->setArgument('action', 'mediatype.list')
					->getUrl())
				->getUrl()
		],
		'mediatype.delete' => ['name' => _('Delete'), 'confirm' => _('Delete selected media types?')]
	], 'mediatype')
]);

// append form to widget
$widget->addItem($mediaTypeForm)->show();
