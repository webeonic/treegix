<?php



require_once dirname(__FILE__).'/include/config.inc.php';
require_once dirname(__FILE__).'/include/screens.inc.php';
require_once dirname(__FILE__).'/include/ident.inc.php';
require_once dirname(__FILE__).'/include/forms.inc.php';
require_once dirname(__FILE__).'/include/maps.inc.php';

$page['type'] = detect_page_type(PAGE_TYPE_HTML);
$page['title'] = _('Configuration of screens');
$page['file'] = 'screenconf.php';
$page['scripts'] = ['multiselect.js'];

require_once dirname(__FILE__).'/include/page_header.php';

// VAR	TYPE	OPTIONAL	FLAGS	VALIDATION	EXCEPTION
$fields = [
	'screens' =>		[T_TRX_INT, O_OPT, P_SYS,	DB_ID,			null],
	'screenid' =>		[T_TRX_INT, O_NO,  P_SYS,	DB_ID,
		'isset({form}) && ({form} === "update" || {form} === "full_clone")'
	],
	'templateid' =>		[T_TRX_INT, O_OPT, P_SYS,	DB_ID,			null],
	'name' =>			[T_TRX_STR, O_OPT, null,	NOT_EMPTY,		'isset({add}) || isset({update})', _('Name')],
	'hsize' =>			[T_TRX_INT, O_OPT, null,	BETWEEN(SCREEN_MIN_SIZE, SCREEN_MAX_SIZE),
		'isset({add}) || isset({update})', _('Columns')
	],
	'vsize' =>			[T_TRX_INT, O_OPT, null,	BETWEEN(SCREEN_MIN_SIZE, SCREEN_MAX_SIZE),
		'isset({add}) || isset({update})', _('Rows')
	],
	'userid' =>			[T_TRX_INT, O_OPT, P_SYS,	DB_ID,			null],
	'private' =>		[T_TRX_INT, O_OPT, null,	BETWEEN(0, 1),	null],
	'users' =>			[T_TRX_INT, O_OPT, null,	null,			null],
	'userGroups' =>		[T_TRX_INT, O_OPT, null,	null,			null],
	// actions
	'action' =>			[T_TRX_STR, O_OPT, P_SYS|P_ACT, IN('"screen.export","screen.massdelete"'),		null],
	'add' =>			[T_TRX_STR, O_OPT, P_SYS|P_ACT, null,		null],
	'update' =>			[T_TRX_STR, O_OPT, P_SYS|P_ACT, null,		null],
	'delete' =>			[T_TRX_STR, O_OPT, P_SYS|P_ACT, null,		null],
	'cancel' =>			[T_TRX_STR, O_OPT, P_SYS,	null,			null],
	'form' =>			[T_TRX_STR, O_OPT, P_SYS,	null,			null],
	'form_refresh' =>	[T_TRX_INT, O_OPT, null,	null,			null],
	// filter
	'filter_set' =>		[T_TRX_STR, O_OPT, P_SYS,	null,			null],
	'filter_rst' =>		[T_TRX_STR, O_OPT, P_SYS,	null,			null],
	'filter_name' =>	[T_TRX_STR, O_OPT, null,	null,			null],
	// sort and sortorder
	'sort' =>					[T_TRX_STR, O_OPT, P_SYS, IN('"name"'),								null],
	'sortorder' =>				[T_TRX_STR, O_OPT, P_SYS, IN('"'.TRX_SORT_DOWN.'","'.TRX_SORT_UP.'"'),	null]
];
check_fields($fields);

/*
 * Permissions
 */
if (hasRequest('screenid')) {
	if (hasRequest('templateid')) {
		$screens = API::TemplateScreen()->get([
			'output' => ['screenid', 'name', 'hsize', 'vsize', 'templateid'],
			'screenids' => getRequest('screenid'),
			'editable' => true
		]);
	}
	else {
		$screens = API::Screen()->get([
			'output' => ['screenid', 'name', 'hsize', 'vsize', 'templateid', 'userid', 'private'],
			'selectUsers' => ['userid', 'permission'],
			'selectUserGroups' => ['usrgrpid', 'permission'],
			'screenids' => getRequest('screenid'),
			'editable' => true
		]);
	}

	if (!$screens) {
		access_deny();
	}

	$screen = reset($screens);
}
else {
	$screen = [];
}

/*
 * Actions
 */
if (hasRequest('add') || hasRequest('update')) {
	DBstart();

	if (hasRequest('update')) {
		$screen = [
			'screenid' => getRequest('screenid'),
			'name' => getRequest('name'),
			'hsize' => getRequest('hsize'),
			'vsize' => getRequest('vsize')
		];

		$messageSuccess = _('Screen updated');
		$messageFailed = _('Cannot update screen');

		if (hasRequest('templateid')) {
			$screenOld = API::TemplateScreen()->get([
				'screenids' => getRequest('screenid'),
				'output' => API_OUTPUT_EXTEND,
				'editable' => true
			]);
			$screenOld = reset($screenOld);

			$result = API::TemplateScreen()->update($screen);
		}
		else {
			$screen['userid'] = getRequest('userid', '');
			$screen['private'] = getRequest('private', PRIVATE_SHARING);
			$screen['users'] = getRequest('users', []);
			$screen['userGroups'] = getRequest('userGroups', []);

			// Only administrators can set screen owner.
			if (CWebUser::getType() == USER_TYPE_TREEGIX_USER) {
				unset($screen['userid']);
			}
			// Screen update with inaccessible user.
			elseif (CWebUser::getType() == USER_TYPE_TREEGIX_ADMIN && $screen['userid'] === '') {
				$user_exist = API::User()->get([
					'output' => ['userid'],
					'userids' => [$screen['userid']]
				]);

				if (!$user_exist) {
					unset($screen['userid']);
				}
			}

			$screenOld = API::Screen()->get([
				'screenids' => getRequest('screenid'),
				'output' => API_OUTPUT_EXTEND,
				'editable' => true
			]);
			$screenOld = reset($screenOld);

			$result = API::Screen()->update($screen);
		}

		if ($result) {
			add_audit_ext(AUDIT_ACTION_UPDATE, AUDIT_RESOURCE_SCREEN, $screen['screenid'], $screen['name'], 'screens',
				$screenOld, $screen
			);
		}
	}
	else {
		$screen = [
			'name' => getRequest('name'),
			'hsize' => getRequest('hsize'),
			'vsize' => getRequest('vsize')
		];

		$messageSuccess = _('Screen added');
		$messageFailed = _('Cannot add screen');

		if (getRequest('form') === 'full_clone') {
			$output = ['resourcetype', 'resourceid', 'width', 'height', 'x', 'y', 'colspan', 'rowspan', 'elements',
				'valign', 'haligh', 'style', 'url', 'max_columns'
			];

			if (hasRequest('templateid')) {
				$screen['screenitems'] = API::TemplateScreenItem()->get([
					'output' => $output,
					'screenids' => [getRequest('screenid')]
				]);
			}
			else {
				array_push($output, 'dynamic', 'sort_triggers', 'application');

				$screen['screenitems'] = API::ScreenItem()->get([
					'output' => $output,
					'screenids' => [getRequest('screenid')]
				]);
			}

			$max_x = $screen['hsize'] - 1;
			$max_y = $screen['vsize'] - 1;

			foreach ($screen['screenitems'] as $key => $screen_item) {
				if ($screen_item['x'] > $max_x || $screen_item['y'] > $max_y) {
					unset($screen['screenitems'][$key]);
				}
			}
		}

		if (hasRequest('templateid')) {
			$screen['templateid'] = getRequest('templateid');

			$screenids = API::TemplateScreen()->create($screen);
		}
		else {
			$screen['userid'] = getRequest('userid', '');
			$screen['private'] = getRequest('private', PRIVATE_SHARING);
			$screen['users'] = getRequest('users', []);
			$screen['userGroups'] = getRequest('userGroups', []);

			$screenids = API::Screen()->create($screen);
		}

		$result = (bool) $screenids;

		if ($result) {
			$screenid = reset($screenids);
			$screenid = reset($screenid);

			add_audit_details(AUDIT_ACTION_ADD, AUDIT_RESOURCE_SCREEN, $screenid, $screen['name']);
		}
	}

	$result = DBend($result);

	if ($result) {
		unset($_REQUEST['form'], $_REQUEST['screenid']);
		uncheckTableRows();
	}
	show_messages($result, $messageSuccess, $messageFailed);
}
elseif ((hasRequest('delete') && hasRequest('screenid'))
		|| (hasRequest('action') && getRequest('action') === 'screen.massdelete' && hasRequest('screens'))) {
	$screenids = getRequest('screens', []);
	if (hasRequest('screenid')) {
		$screenids[] = getRequest('screenid');
	}

	DBstart();

	if (hasRequest('templateid')) {
		$parent_id = getRequest('templateid');

		$screens = API::TemplateScreen()->get([
			'screenids' => $screenids,
			'output' => API_OUTPUT_EXTEND,
			'editable' => true
		]);

		$result = API::TemplateScreen()->delete($screenids);
	}
	else {
		$parent_id = null;

		$screens = API::Screen()->get([
			'screenids' => $screenids,
			'output' => API_OUTPUT_EXTEND,
			'editable' => true
		]);

		$result = API::Screen()->delete($screenids);
	}

	$result = DBend($result);

	if ($result) {
		foreach ($screens as $screen) {
			add_audit_details(AUDIT_ACTION_DELETE, AUDIT_RESOURCE_SCREEN, $screen['screenid'], $screen['name']);
		}
		unset($_REQUEST['screenid'], $_REQUEST['form']);
		uncheckTableRows($parent_id);
	}
	else {
		uncheckTableRows($parent_id, trx_objectValues($screens, 'screenid'));
	}
	show_messages($result, _('Screen deleted'), _('Cannot delete screen'));
}

/*
 * Display
 */
if (hasRequest('form')) {
	$current_userid = CWebUser::$data['userid'];
	$userids[$current_userid] = true;
	$user_groupids = [];

	if (!hasRequest('templateid') && (!array_key_exists('templateid', $screen) || !$screen['templateid'])) {
		if (!hasRequest('screenid') || hasRequest('form_refresh')) {
			// Screen owner.
			$screen_owner = getRequest('userid', $current_userid);
			$userids[$screen_owner] = true;

			foreach (getRequest('users', []) as $user) {
				$userids[$user['userid']] = true;
			}

			foreach (getRequest('userGroups', []) as $user_group) {
				$user_groupids[$user_group['usrgrpid']] = true;
			}
		}
		else {
			// Screen owner.
			$userids[$screen['userid']] = true;

			foreach ($screen['users'] as $user) {
				$userids[$user['userid']] = true;
			}

			foreach ($screen['userGroups'] as $user_group) {
				$user_groupids[$user_group['usrgrpid']] = true;
			}
		}

		$data['users'] = API::User()->get([
			'output' => ['userid', 'alias', 'name', 'surname'],
			'userids' => array_keys($userids),
			'preservekeys' => true
		]);

		$data['user_groups'] = API::UserGroup()->get([
			'output' => ['usrgrpid', 'name'],
			'usrgrpids' => array_keys($user_groupids),
			'preservekeys' => true
		]);
	}

	if (!hasRequest('screenid') || hasRequest('form_refresh')) {
		$data['screen'] = [
			'screenid' => getRequest('screenid'),
			'name' => getRequest('name', ''),
			'hsize' => getRequest('hsize', 1),
			'vsize' => getRequest('vsize', 1)
		];

		if (hasRequest('templateid')) {
			$data['screen']['templateid'] = getRequest('templateid');
		}
		else {
			$data['screen']['userid'] = getRequest('userid', hasRequest('form_refresh') ? '' : $current_userid);
			$data['screen']['private'] = getRequest('private', PRIVATE_SHARING);
			$data['screen']['users'] = getRequest('users', []);
			$data['screen']['userGroups'] = getRequest('userGroups', []);
			$data['screen']['templateid'] = null;
		}
	}
	else {
		$data['screen'] = $screen;
	}

	$data['form'] = getRequest('form');
	$data['current_user_userid'] = $current_userid;
	$data['form_refresh'] = getRequest('form_refresh');

	// render view
	$screenView = new CView('monitoring.screen.edit', $data);
	$screenView->render();
	$screenView->show();
}
else {
	CProfile::delete('web.screens.elementid');

	$sortField = getRequest('sort', CProfile::get('web.'.$page['file'].'.sort', 'name'));
	$sortOrder = getRequest('sortorder', CProfile::get('web.'.$page['file'].'.sortorder', TRX_SORT_UP));

	CProfile::update('web.'.$page['file'].'.sort', $sortField, PROFILE_TYPE_STR);
	CProfile::update('web.'.$page['file'].'.sortorder', $sortOrder, PROFILE_TYPE_STR);

	$config = select_config();

	$data = [
		'templateid' => getRequest('templateid'),
		'sort' => $sortField,
		'sortorder' => $sortOrder
	];

	if ($data['templateid']) {
		$data['screens'] = API::TemplateScreen()->get([
			'output' => ['screenid', 'name', 'hsize', 'vsize'],
			'templateids' => $data['templateid'],
			'sortfield' => $sortField,
			'limit' => $config['search_limit'] + 1,
			'editable' => true,
			'preservekeys' => true
		]);
	}
	else {
		if (hasRequest('filter_set')) {
			CProfile::update('web.screenconf.filter_name', getRequest('filter_name', ''), PROFILE_TYPE_STR);
		}
		elseif (hasRequest('filter_rst')) {
			DBStart();
			CProfile::delete('web.screenconf.filter_name');
			DBend();
		}

		$data['filter'] = [
			'name' => CProfile::get('web.screenconf.filter_name', '')
		];

		$data['screens'] = API::Screen()->get([
			'output' => ['screenid', 'name', 'hsize', 'vsize'],
			'sortfield' => $sortField,
			'limit' => $config['search_limit'] + 1,
			'search' => [
				'name' => ($data['filter']['name'] === '') ? null : $data['filter']['name']
			],
			'preservekeys' => true
		]);

		$user_type = CWebUser::getType();

		if ($user_type != USER_TYPE_SUPER_ADMIN && $user_type != USER_TYPE_TREEGIX_ADMIN) {
			$editable_screens = API::Screen()->get([
				'output' => [],
				'screenids' => array_keys($data['screens']),
				'editable' => true,
				'preservekeys' => true
			]);

			foreach ($data['screens'] as &$screen) {
				$screen['editable'] = array_key_exists($screen['screenid'], $editable_screens);
			}
			unset($screen);
		}

		$data += [
			'profileIdx' => 'web.screenconf.filter',
			'active_tab' => CProfile::get('web.screenconf.filter.active', 1)
		];
	}
	order_result($data['screens'], $sortField, $sortOrder);

	// paging
	$data['paging'] = getPagingLine($data['screens'], $sortOrder, new CUrl('screenconf.php'));

	// render view
	$screenView = new CView('monitoring.screen.list', $data);
	$screenView->render();
	$screenView->show();
}

require_once dirname(__FILE__).'/include/page_footer.php';
