<script type="text/x-jquery-tmpl" id="tag-row-tmpl">
	<?= renderTagTableRow('#{rowNum}', '', '', ['add_post_js' => false]) ?>
</script>

<script type="text/javascript">
	jQuery(function($) {
		<?php if (CWebUser::getType() == USER_TYPE_SUPER_ADMIN): ?>
			$('input[name=mass_update_groups]').on('change', function() {
				$('#groups_').multiSelect('modify', {
					'addNew': ($(this).val() == <?= TRX_ACTION_ADD ?> || $(this).val() == <?= TRX_ACTION_REPLACE ?>)
				});
			});
		<?php endif ?>

		$('#tags-table')
			.dynamicRows({template: '#tag-row-tmpl'})
			.on('click', 'button.element-table-add', function() {
				$('#tags-table .<?= TRX_STYLE_TEXTAREA_FLEXIBLE ?>').textareaFlexible();
			});

		$('#mass_replace_tpls').on('change', function() {
			$('#mass_clear_tpls').prop('disabled', !this.checked);
		}).trigger('change');
	});
</script>
