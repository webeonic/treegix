<?php



include('include/views/js/configuration.services.edit.js.php');

$widget = (new CWidget())->setTitle(_('Service parent'));

// create form
$servicesParentForm = (new CForm())
	->setName('servicesForm');
if (!empty($this->data['service'])) {
	$servicesParentForm->addVar('serviceid', $this->data['service']['serviceid']);
}

// create table
$servicesParentTable = (new CTableInfo())
	->setHeader([_('Service'), _('Status calculation'), _('Trigger')]);

$parentid = getRequest('parentid', 0);
$prefix = null;

// root
if ($parentid == 0) {
	$description = new CSpan(_('root'));
}
else {
	$description = (new CLink(_('root'), '#'))
		->onClick('javascript:
			jQuery(\'#parent_name\', window.opener.document).val('.trx_jsvalue(_('root')).');
			jQuery(\'#parentname\', window.opener.document).val('.trx_jsvalue(_('root')).');
			jQuery(\'#parentid\', window.opener.document).val('.trx_jsvalue(0).');
			self.close();
			return false;'
		);
}

$servicesParentTable->addRow([
		[$prefix, $description],
		_('Note'),
		'-'
]);

// others
foreach ($this->data['db_pservices'] as $db_service) {
	if (bccomp($parentid, $db_service['serviceid']) == 0) {
		$description = new CSpan($db_service['name']);
	}
	else {
		$description = (new CLink($db_service['name'], '#'))
			->addClass('link')
			->onClick('javascript:
				jQuery(\'#parent_name\', window.opener.document).val('.trx_jsvalue($db_service['name']).');
				jQuery(\'#parentname\', window.opener.document).val('.trx_jsvalue($db_service['name']).');
				jQuery(\'#parentid\', window.opener.document).val('.trx_jsvalue($db_service['serviceid']).');
				self.close();
				return false;'
			);
	}

	$servicesParentTable->addRow([[$prefix, $description], serviceAlgorithm($db_service['algorithm']),
		$db_service['trigger']
	]);
}

$servicesParentTable->setFooter(
	new CCol(
		(new CButton('cancel', _('Cancel')))
			->onClick('javascript: self.close();')
			->setAttribute('style', 'text-align:right;')
	)
);

// append table to form
$servicesParentForm->addItem($servicesParentTable);

// append form to widget
$widget->addItem($servicesParentForm);

return $widget;
