<?php
 


if (!$data['readonly']) {
	require_once dirname(__FILE__).'/js/hostmacros.js.php';
}

// form list
$macros_form_list = new CFormList('macrosFormList');

if ($data['readonly'] && !$data['macros']) {
	$table = _('No macros found.');
}
else {
	$table = (new CTable())
		->setId('tbl_macros')
		->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_CONTAINER);

	$actions_col = $data['readonly'] ? null : '';
	if ($data['show_inherited_macros']) {
		if (CWebUser::getType() == USER_TYPE_SUPER_ADMIN) {
			$link = (new CLink(_('configure'), 'adm.macros.php'))
				->setAttribute('target', '_blank');
			$link = [' (', $link, ')'];
		}
		else {
			$link = null;
		}
		$table->setHeader([
			_('Macro'), '', _('Effective value'), $actions_col, '', _('Template value'), '', [_('Global value'), $link]
		]);
	}
	else {
		$table->setHeader([_('Macro'), '', _('Value'), _('Description'), $actions_col]);
	}

	// fields
	foreach ($data['macros'] as $i => $macro) {
		$macro_input = (new CTextAreaFlexible('macros['.$i.'][macro]', $macro['macro'], [
			'readonly' => (
				$data['readonly'] || ($data['show_inherited_macros'] && ($macro['type'] & TRX_PROPERTY_INHERITED))
			)
		]))
			->addClass('macro')
			->setWidth(TRX_TEXTAREA_MACRO_WIDTH)
			->setAttribute('placeholder', '{$MACRO}');

		$macro_cell = [$macro_input];
		if (!$data['readonly']) {
			if (array_key_exists('hostmacroid', $macro)) {
				$macro_cell[] = new CVar('macros['.$i.'][hostmacroid]', $macro['hostmacroid']);
			}

			if ($data['show_inherited_macros'] && ($macro['type'] & TRX_PROPERTY_INHERITED)) {
				if (array_key_exists('template', $macro)) {
					$macro_cell[] = new CVar('macros['.$i.'][inherited][value]', $macro['template']['value']);
					$macro_cell[] = new CVar('macros['.$i.'][inherited][description]',
						$macro['template']['description']
					);
				}
				else {
					$macro_cell[] = new CVar('macros['.$i.'][inherited][value]', $macro['global']['value']);
					$macro_cell[] = new CVar('macros['.$i.'][inherited][description]',
						$macro['global']['description']
					);
				}
			}
		}

		if ($data['show_inherited_macros']) {
			$macro_cell[] = new CVar('macros['.$i.'][type]', $macro['type']);
		}

		$value_input = (new CTextAreaFlexible('macros['.$i.'][value]', $macro['value'], [
			'readonly' => (
				$data['readonly'] || ($data['show_inherited_macros'] && !($macro['type'] & TRX_PROPERTY_OWN))
			)
		]))
			->setWidth(TRX_TEXTAREA_MACRO_VALUE_WIDTH)
			->setAttribute('placeholder', _('value'));

		$row = [
			(new CCol($macro_cell))->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_PARENT),
			'&rArr;',
			(new CCol($value_input))->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_PARENT)
		];

		if (!$data['show_inherited_macros']) {
			$row[] = (new CCol(
				(new CTextAreaFlexible('macros['.$i.'][description]', $macro['description']))
					->setWidth(TRX_TEXTAREA_MACRO_VALUE_WIDTH)
					->setMaxlength(DB::getFieldLength('hostmacro', 'description'))
					->setReadonly($data['readonly'])
					->setAttribute('placeholder', _('description'))
			))->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_PARENT);
		}

		if (!$data['readonly']) {
			if ($data['show_inherited_macros']) {
				if (($macro['type'] & TRX_PROPERTY_BOTH) == TRX_PROPERTY_BOTH) {
					$row[] = (new CCol(
						(new CButton('macros['.$i.'][change]', _('Remove')))
							->addClass(TRX_STYLE_BTN_LINK)
							->addClass('element-table-change')
					))->addClass(TRX_STYLE_NOWRAP);
				}
				elseif ($macro['type'] & TRX_PROPERTY_INHERITED) {
					$row[] = (new CCol(
						(new CButton('macros['.$i.'][change]', _x('Change', 'verb')))
							->addClass(TRX_STYLE_BTN_LINK)
							->addClass('element-table-change')
					))->addClass(TRX_STYLE_NOWRAP);
				}
				else {
					$row[] = (new CCol(
						(new CButton('macros['.$i.'][remove]', _('Remove')))
							->addClass(TRX_STYLE_BTN_LINK)
							->addClass('element-table-remove')
					))->addClass(TRX_STYLE_NOWRAP);
				}
			}
			else {
				$row[] = (new CCol(
					(new CButton('macros['.$i.'][remove]', _('Remove')))
						->addClass(TRX_STYLE_BTN_LINK)
						->addClass('element-table-remove')
				))->addClass(TRX_STYLE_NOWRAP);
			}
		}

		if ($data['show_inherited_macros']) {
			if (array_key_exists('template', $macro)) {
				if ($macro['template']['rights'] == PERM_READ_WRITE) {
					$link = (new CLink(CHtml::encode($macro['template']['name']),
						'templates.php?form=update&templateid='.$macro['template']['templateid'])
					)
						->addClass('unknown')
						->setAttribute('target', '_blank');
				}
				else {
					$link = new CSpan(CHtml::encode($macro['template']['name']));
				}

				$row[] = '&lArr;';
				$row[] = (new CDiv([$link, NAME_DELIMITER, '"'.$macro['template']['value'].'"']))
					->addClass(TRX_STYLE_OVERFLOW_ELLIPSIS)
					->setWidth(TRX_TEXTAREA_MACRO_VALUE_WIDTH);
			}
			else {
				array_push($row, '',
					(new CDiv())
						->addClass(TRX_STYLE_OVERFLOW_ELLIPSIS)
						->setWidth(TRX_TEXTAREA_MACRO_VALUE_WIDTH)
				);
			}

			if (array_key_exists('global', $macro)) {
				$row[] = '&lArr;';
				$row[] = (new CDiv('"'.$macro['global']['value'].'"'))
					->addClass(TRX_STYLE_OVERFLOW_ELLIPSIS)
					->setWidth(TRX_TEXTAREA_MACRO_VALUE_WIDTH);
			}
			else {
				array_push($row, '',
					(new CDiv())
						->addClass(TRX_STYLE_OVERFLOW_ELLIPSIS)
						->setWidth(TRX_TEXTAREA_MACRO_VALUE_WIDTH)
				);
			}
		}

		$table->addRow($row, 'form_row');

		if ($data['show_inherited_macros']) {
			$table->addRow((new CRow([
				(new CCol(
					(new CTextAreaFlexible('macros['.$i.'][description]', $macro['description']))
						->setMaxlength(DB::getFieldLength('hostmacro' , 'description'))
						->setWidth(TRX_TEXTAREA_STANDARD_WIDTH)
						->setAttribute('placeholder', _('description'))
						->setReadonly($data['readonly'] || !($macro['type'] & TRX_PROPERTY_OWN))
				))
					->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_PARENT)
					->setColSpan(8)
			]))->addClass('form_row'));
		}
	}

	// buttons
	if (!$data['readonly']) {
		$table->setFooter(new CCol(
			(new CButton('macro_add', _('Add')))
				->addClass(TRX_STYLE_BTN_LINK)
				->addClass('element-table-add')
		));
	}
}

$macros_form_list
	->addRow(null,
		(new CRadioButtonList('show_inherited_macros', (int) $data['show_inherited_macros']))
			->addValue($data['is_template'] ? _('Template macros') : _('Host macros'), 0, null, 'this.form.submit()')
			->addValue($data['is_template'] ? _('Inherited and template macros') : _('Inherited and host macros'), 1,
				null, 'this.form.submit()'
			)
			->setModern(true)
	)
	->addRow(null, $table);

return $macros_form_list;
