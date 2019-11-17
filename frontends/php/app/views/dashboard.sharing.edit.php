<?php


$form = (new CForm('post'))->setName('dashboard_sharing_form');

$table_user_groups = (new CTable())
	->setHeader([_('User groups'), _('Permissions'), _('Action')])
	->addRow(
		(new CRow(
			(new CCol(
				(new CButton(null, _('Add')))
					->onClick('return PopUp("popup.generic",'.
						CJs::encodeJson([
							'srctbl' => 'usrgrp',
							'srcfld1' => 'usrgrpid',
							'srcfld2' => 'name',
							'dstfrm' => $form->getName(),
							'multiselect' => '1'
						]).', null, this);'
					)
					->addClass(ZBX_STYLE_BTN_LINK)
			))->setColSpan(3)
		))->setId('user_group_list_footer')
	)
	->addStyle('width: 100%;');

$table_users = (new CTable())
	->setHeader([_('Users'), _('Permissions'), _('Action')])
	->addRow(
		(new CRow(
			(new CCol(
				(new CButton(null, _('Add')))
					->onClick('return PopUp("popup.generic",'.
						CJs::encodeJson([
							'srctbl' => 'users',
							'srcfld1' => 'userid',
							'srcfld2' => 'fullname',
							'dstfrm' => $form->getName(),
							'multiselect' => '1'
						]).', null, this);'
					)
					->addClass(ZBX_STYLE_BTN_LINK)
			))->setColSpan(3)
		))->setId('user_list_footer')
	)
	->addStyle('width: 100%;');

$form
	->addItem(getMessages())
	->addItem(new CInput('hidden', 'dashboardid', $data['dashboard']['dashboardid']))
	// indicator to help delete all users
	->addItem(new CInput('hidden', 'users['.CControllerDashboardShareUpdate::EMPTY_USER.']', '1'))
	// indicator to help delete all user groups
	->addItem(new CInput('hidden', 'userGroups['.CControllerDashboardShareUpdate::EMPTY_GROUP.']', '1'))
	->addItem((new CFormList('sharing_form'))
		->addRow(_('Type'),
			(new CRadioButtonList('private', PRIVATE_SHARING))
				->addValue(_('Private'), PRIVATE_SHARING)
				->addValue(_('Public'), PUBLIC_SHARING)
				->setModern(true)
		)
		->addRow(_('List of user group shares'),
			(new CDiv($table_user_groups))
				->addClass(ZBX_STYLE_TABLE_FORMS_SEPARATOR)
				->addStyle('min-width: '.ZBX_TEXTAREA_STANDARD_WIDTH.'px;')
		)
		->addRow(_('List of user shares'),
			(new CDiv($table_users))
				->addClass(ZBX_STYLE_TABLE_FORMS_SEPARATOR)
				->addStyle('min-width: '.ZBX_TEXTAREA_STANDARD_WIDTH.'px;')
		)
	);

$output = [
	'header' => _('Dashboard sharing'),
	'body' => $form->toString(),
	'script_inline' => 
		'jQuery(document).ready(function($) {'.
			'$("[name='.$form->getName().']").fillDashbrdSharingForm('.json_encode($data['dashboard']).');'.
		'});',
	'buttons' => [
		[
			'title' => _('Update'),
			'isSubmit' => true,
			'action' => 'return dashbrdConfirmSharing();'
		]
	]
];

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
