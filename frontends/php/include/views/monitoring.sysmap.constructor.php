<?php



include dirname(__FILE__).'/js/monitoring.sysmaps.js.php';

// create menu
$menu = (new CList())
	->addClass(TRX_STYLE_OBJECT_GROUP)
	->addItem([
		_('Map element').':&nbsp;',
		(new CButton('selementAdd', _('Add')))->addClass(TRX_STYLE_BTN_LINK),
		'&nbsp;/&nbsp;',
		(new CButton('selementRemove', _('Remove')))->addClass(TRX_STYLE_BTN_LINK)
	])
	->addItem([
		_('Shape').':&nbsp;',
		(new CButton('shapeAdd', _('Add')))->addClass(TRX_STYLE_BTN_LINK),
		'&nbsp;/&nbsp;',
		(new CButton('shapesRemove', _('Remove')))->addClass(TRX_STYLE_BTN_LINK)
	])
	->addItem([
		_('Link').':&nbsp;',
		(new CButton('linkAdd', _('Add')))->addClass(TRX_STYLE_BTN_LINK),
		'&nbsp;/&nbsp;',
		(new CButton('linkRemove', _('Remove')))->addClass(TRX_STYLE_BTN_LINK)
	])
	->addItem([
		_('Expand macros').':&nbsp;',
		(new CButton('expand_macros',
			($this->data['sysmap']['expand_macros'] == SYSMAP_EXPAND_MACROS_ON) ? _('On') : _('Off')
		))->addClass(TRX_STYLE_BTN_LINK)
	])
	->addItem([
		_('Grid').':&nbsp;',
		(new CButton('gridshow',
			($data['sysmap']['grid_show'] == SYSMAP_GRID_SHOW_ON) ? _('Shown') : _('Hidden')
		))->addClass(TRX_STYLE_BTN_LINK),
		'&nbsp;/&nbsp;',
		(new CButton('gridautoalign',
			($data['sysmap']['grid_align'] == SYSMAP_GRID_ALIGN_ON) ? _('On') : _('Off')
		))->addClass(TRX_STYLE_BTN_LINK)
	])
	->addItem(new CComboBox('gridsize', $data['sysmap']['grid_size'], null, [
		20 => '20x20',
		40 => '40x40',
		50 => '50x50',
		75 => '75x75',
		100 => '100x100'
	]))
	->addItem((new CButton('gridalignall', _('Align map elements')))->addClass(TRX_STYLE_BTN_LINK))
	->addItem((new CSubmit('update', _('Update')))->setId('sysmap_update'));

$container = (new CDiv())->setId(TRX_STYLE_MAP_AREA);

// create elements
zbx_add_post_js('TREEGIX.apps.map.run("'.TRX_STYLE_MAP_AREA.'", '.CJs::encodeJson([
	'theme' => $data['theme'],
	'sysmap' => $data['sysmap'],
	'iconList' => $data['iconList'],
	'defaultAutoIconId' => $data['defaultAutoIconId'],
	'defaultIconId' => $data['defaultIconId'],
	'defaultIconName' => $data['defaultIconName']
], true).');');

return (new CWidget())
	->setTitle(_('Network maps'))
	->addItem($menu)
	->addItem(
		(new CDiv(
			(new CDiv())
				->addClass(TRX_STYLE_TABLE_FORMS_CONTAINER)
				->addItem($container)
		))->addClass('sysmap-scroll-container')
	);
