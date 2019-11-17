<?php



ob_start(); ?>
jQuery(document).ready(function($) {
	$('#email_send_to').dynamicRows({
		template: '#email_send_to_table_row'
	});

	// Show/hide multiple "Send to" inputs and single "Send to" input and populate hidden "type" field.
	$('#mediatypeid')
		.on('change', function() {
			var mediatypes_by_type = <?= (new CJson())->encode($data['mediatypes']) ?>,
				mediatypeid = $(this).val();

			if (mediatypes_by_type[mediatypeid] == <?= MEDIA_TYPE_EMAIL ?>) {
				$('#mediatype_send_to').hide();
				$('#mediatype_email_send_to').show();
			}
			else {
				$('#mediatype_send_to').show();
				$('#mediatype_email_send_to').hide();
			}
		})
		.trigger("change");
});

/**
 * Send media form data to server for validation before adding them to user media tab.
 *
 * @param {string} formname		form name that is sent to server for validation
 */
function validateMedia(formname) {
	var form = window.document.forms[formname];

	jQuery(form).trimValues(['#period', '#sendto', 'input[name^="sendto_emails"]']);

	jQuery.ajax({
		url: jQuery(form).attr('action'),
		data: jQuery(form).serialize(),
		success: function(ret) {
			jQuery(form).parent().find('.msg-bad, .msg-good').remove();

			if (typeof ret.errors !== 'undefined') {
				jQuery(ret.errors).insertBefore(jQuery(form));
			}
			else {
				add_media(ret.dstfrm, ret.media, ret.mediatypeid, ret.sendto, ret.period, ret.active, ret.severity);
			}
		},
		dataType: 'json',
		type: 'post'
	});
}
<?php return ob_get_clean(); ?>
