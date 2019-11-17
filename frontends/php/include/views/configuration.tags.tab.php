<?php



if (!$data['readonly']) {
	require_once dirname(__FILE__).'/js/configuration.tags.tab.js.php';
}

$show_inherited_tags = (array_key_exists('show_inherited_tags', $data) && $data['show_inherited_tags']);

// form list
$tags_form_list = new CFormList('tagsFormList');

$table = (new CTable())
	->setId('tags-table')
	->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_CONTAINER)
	->setHeader([
		_('Name'),
		_('Value'),
		_('Action'),
		$show_inherited_tags ? _('Parent templates') : null
	]);

// fields
foreach ($data['tags'] as $i => $tag) {
	if (!array_key_exists('type', $tag)) {
		$tag['type'] = TRX_PROPERTY_OWN;
	}

	$readonly = ($data['readonly'] || ($show_inherited_tags && $tag['type'] == TRX_PROPERTY_INHERITED));

	$tag_input = (new CTextAreaFlexible('tags['.$i.'][tag]', $tag['tag'], ['readonly' => $readonly]))
		->setWidth(TRX_TEXTAREA_TAG_WIDTH)
		->setAttribute('placeholder', _('tag'));

	$tag_cell = [$tag_input];

	if ($show_inherited_tags) {
		$tag_cell[] = new CVar('tags['.$i.'][type]', $tag['type']);
	}

	$value_input = (new CTextAreaFlexible('tags['.$i.'][value]', $tag['value'], ['readonly' => $readonly]))
		->setWidth(TRX_TEXTAREA_TAG_VALUE_WIDTH)
		->setAttribute('placeholder', _('value'));

	$row = [
		(new CCol($tag_cell))->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_PARENT),
		(new CCol($value_input))->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_PARENT)
	];

	$row[] = (new CCol(
		($show_inherited_tags && ($tag['type'] & TRX_PROPERTY_INHERITED))
			? (new CButton('tags['.$i.'][disable]', _('Remove')))
				->addClass(TRX_STYLE_BTN_LINK)
				->addClass('element-table-disable')
				->setEnabled(!$readonly)
			: (new CButton('tags['.$i.'][remove]', _('Remove')))
				->addClass(TRX_STYLE_BTN_LINK)
				->addClass('element-table-remove')
				->setEnabled(!$readonly)
	))->addClass(TRX_STYLE_NOWRAP);

	if ($show_inherited_tags) {
		$template_list = [];

		if (array_key_exists('parent_templates', $tag)) {
			CArrayHelper::sort($tag['parent_templates'], ['name']);

			foreach ($tag['parent_templates'] as $templateid => $template) {
				if ($template['permission'] == PERM_READ_WRITE) {
					$template_list[] = (new CLink($template['name'],
						(new CUrl('templates.php'))
							->setArgument('form', 'update')
							->setArgument('templateid', $templateid)
					))->setAttribute('target', '_blank');
				}
				else {
					$template_list[] = (new CSpan($template['name']))->addClass(TRX_STYLE_GREY);
				}

				$template_list[] = ', ';
			}

			array_pop($template_list);
		}

		$row[] = $template_list;
	}

	$table->addRow($row, 'form_row');
}

// buttons
$table->setFooter(new CCol(
	(new CButton('tag_add', _('Add')))
		->addClass(TRX_STYLE_BTN_LINK)
		->addClass('element-table-add')
		->setEnabled(!$data['readonly'])
));

if ($data['source'] === 'trigger' || $data['source'] === 'trigger_prototype') {
	$tags_form_list->addRow(null,
		(new CRadioButtonList('show_inherited_tags', (int) $data['show_inherited_tags']))
			->addValue(_('Trigger tags'), 0, null, 'this.form.submit()')
			->addValue(_('Inherited and trigger tags'), 1, null, 'this.form.submit()')
			->setModern(true)
	);
}

$tags_form_list->addRow(null, $table);

return $tags_form_list;
