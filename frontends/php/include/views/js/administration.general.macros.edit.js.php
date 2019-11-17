<script type="text/x-jquery-tmpl" id="macro-row-tmpl">
	<?= (new CRow([
			(new CCol(
				(new CTextAreaFlexible('macros[#{rowNum}][macro]', '', ['add_post_js' => false]))
					->addClass('macro')
					->setWidth(TRX_TEXTAREA_MACRO_WIDTH)
					->setAttribute('placeholder', '{$MACRO}')
			))->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_PARENT),
			'&rArr;',
			(new CCol(
				(new CTextAreaFlexible('macros[#{rowNum}][value]', '', ['add_post_js' => false]))
					->setWidth(TRX_TEXTAREA_MACRO_VALUE_WIDTH)
					->setAttribute('placeholder', _('value'))
			))->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_PARENT),
			(new CCol(
				(new CTextAreaFlexible('macros[#{rowNum}][description]', '', ['add_post_js' => false]))
					->setWidth(TRX_TEXTAREA_MACRO_VALUE_WIDTH)
					->setAttribute('placeholder', _('description'))
			))->addClass(TRX_STYLE_TEXTAREA_FLEXIBLE_PARENT),
			(new CCol(
				(new CButton('macros[#{rowNum}][remove]', _('Remove')))
					->addClass(TRX_STYLE_BTN_LINK)
					->addClass('element-table-remove')
			))->addClass(TRX_STYLE_NOWRAP)
		]))
			->addClass('form_row')
			->toString()
	?>
</script>

<script type="text/javascript">
	jQuery(function($) {
		$('#tbl_macros')
			.on('click', 'button.element-table-remove', function() {
				// check if the macro has an hidden ID element, if it does - increment the deleted macro counter
				var macroNum = $(this).attr('id').split('_')[1];
				if ($('#macros_' + macroNum + '_globalmacroid').length) {
					var count = $('#update').data('removedCount') + 1;
					$('#update').data('removedCount', count);
				}
			})
			.dynamicRows({template: '#macro-row-tmpl'})
			.on('click', 'button.element-table-add', function() {
				$('#tbl_macros .<?= TRX_STYLE_TEXTAREA_FLEXIBLE ?>').textareaFlexible();
			})
			.on('blur', '.<?= TRX_STYLE_TEXTAREA_FLEXIBLE ?>', function() {
				if ($(this).hasClass('macro')) {
					macroToUpperCase(this);
				}
				$(this).trigger('input');
			});

		$('#update').click(function() {
			var removedCount = $(this).data('removedCount');

			if (removedCount) {
				return confirm(<?= CJs::encodeJson(_('Are you sure you want to delete')) ?> + ' ' + removedCount + ' ' + <?= CJs::encodeJson(_('macro(s)')) ?> + '?');
			}
		});

		$('form[name="macrosForm"]').submit(function() {
			$('input.macro').each(function() {
				macroToUpperCase(this);
			});
		});

		function macroToUpperCase(element) {
			var macro = $(element).val(),
				end = macro.indexOf(':');

			if (end == -1) {
				$(element).val(macro.toUpperCase());
			}
			else {
				var macro_part = macro.substr(0, end),
					context_part = macro.substr(end, macro.length);

				$(element).val(macro_part.toUpperCase() + context_part);
			}
		}
	});
</script>
