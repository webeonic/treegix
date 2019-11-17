


jQuery(function($) {
	var $layout_mode_btn = $('.layout-mode');

	if ($layout_mode_btn.length) {
		$layout_mode_btn.on('click', function(e) {
			e.stopPropagation();
			updateUserProfile('web.layout.mode', $layout_mode_btn.data('layout-mode'), []).always(function(){
				var url = new Curl('', false);
				url.unsetArgument('fullscreen');
				url.unsetArgument('kiosk');
				history.replaceState(history.state, '', url.getUrl());
				location.reload();
			});
		});

		if ($layout_mode_btn.hasClass('btn-dashbrd-normal')) {
			$(window).on('mousemove keyup scroll', function() {
				clearTimeout($layout_mode_btn.data('timer'));
				$layout_mode_btn
					.removeClass('hidden')
					.data('timer', setTimeout(function() {
						$layout_mode_btn.addClass('hidden');
					}, 2000));
			}).trigger('mousemove');
		}
	}
});
