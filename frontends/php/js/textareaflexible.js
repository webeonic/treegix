


(function($) {
	'use strict';

	var methods = {
		init: function(options) {
			var settings = $.extend({}, options);

			return this.each(function() {
				var $textarea = $(this);

				$textarea
					.on('input keydown paste', function(e) {
						if (e.which === 13) {
							return false;
						}

						var old_value = $textarea.val(),
							new_value = old_value.replace(/\r?\n/gi, '');

						if (old_value.length !== new_value.length) {
							$textarea.val(new_value);
						}

						$textarea.height(0).innerHeight($textarea[0].scrollHeight);
					})
					.trigger('input');
			});
		}
	};

	/**
	 * Flexible textarea helper.
	 */
	$.fn.textareaFlexible = function(method) {
		if (methods[method]) {
			return methods[method].apply(this, Array.prototype.slice.call(arguments, 1));
		}

		return methods.init.apply(this, arguments);
	};
})(jQuery);
