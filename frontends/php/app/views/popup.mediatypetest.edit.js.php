<?php


return <<<'JS'
/**
 * Send media type test data to server and get a response.
 *
 * @param {string} formname  Form name that is sent to server for validation.
 */
function mediatypeTestSend(formname) {
	var form = window.document.forms[formname],
		$form = jQuery(form),
		$form_fields = $form.find('#sendto, #subject, #message'),
		$submit_btn = jQuery('.submit-test-btn'),
		data = $form.serialize(),
		url = new Curl($form.attr('action'));

	$form.trimValues(['#sendto', '#subject', '#message']);

	$form_fields.prop('disabled', true);

	jQuery('<span></span>')
		.addClass('preloader')
		.insertAfter($submit_btn)
		.css({
			'display': 'inline-block',
			'margin': '0 10px -8px'
		});

	$submit_btn
		.prop('disabled', true)
		.hide();

	jQuery.ajax({
		url: url.getUrl(),
		data: data,
		success: function(ret) {
			$form.parent().find('.msg-bad, .msg-good').remove();

			if (typeof ret.messages !== 'undefined') {
				jQuery(ret.messages).insertBefore($form);
				$form.parent().find('.link-action').click();
			}

			$form_fields.prop('disabled', false);

			jQuery('.preloader').remove();
			$submit_btn
				.prop('disabled', false)
				.show();
		},
		error: function(request, status, error) {
			if (request.status == 200) {
				alert(error);
			}
			else if (window.document.forms[formname]) {
				var request = this,
					retry = function() {
						jQuery.ajax(request);
					};

				// Retry with 2s interval.
				setTimeout(retry, 2000);
			}
		},
		dataType: 'json',
		type: 'post'
	});
}
JS;
