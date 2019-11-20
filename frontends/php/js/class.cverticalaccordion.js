


/**
 * JQuery class that adds interactivity of vertical accordion for CList element.
 */
jQuery(function ($) {
	"use strict";

	var methods = {
		/**
		 * Create CList based accordion.
		 *
		 * Supported options:
		 * - handler		- selector of UI element to open/close accordion section.
		 * - section		- selector of UI element for single accordion section.
		 * - body			- selector of UI element that should be opened/closed.
		 * - active_class	- CSS class that will be applied for active section.
		 * - closed_class	- CSS class that will be applied for closed section.
		 *
		 * @param options
		 */
		init: function(options) {
			options = $.extend({}, {
				handler: '.list-accordion-item-head',
				section: '.list-accordion-item',
				active_class: 'list-accordion-item-opened',
				closed_class: 'list-accordion-item-closed',
				body: '.list-accordion-item-body'
			}, options);

			this.each(function() {
				var accordion = $(this);

				// Bind collapse/expend.
				accordion
					.data('options', options)
					.on('click', options['handler'], function() {
						var section = $(this).closest(options['section']);

						if (section.hasClass(options['active_class'])) {
							methods['collapseAll'].apply(accordion);
						}
						else {
							methods['expandNth'].apply(accordion, [$(section).index()]);
						}
					});
			});
		},
		// Collapse all accordion rows.
		collapseAll: function() {
			var accordion = $(this),
				options = accordion.data('options');

			$('.' + options['active_class'], accordion)
				.removeClass(options['active_class'])
				.addClass(options['closed_class']);

			$('textarea', accordion).scrollTop(0);

			$(options['handler'], accordion).attr('title', t('S_EXPAND'));
		},
		// Expand N-th row in accordion. Collapse others.
		expandNth: function(n) {
			var accordion = $(this),
				options = accordion.data('options'),
				section = $(options['section'] + ':nth(' + n + ')', accordion),
				handler = $(options['handler'], section);

			methods['collapseAll'].apply(accordion);

			section
				.removeClass(options['closed_class'])
				.addClass(options['active_class']);
			handler.attr('title', t('S_COLLAPSE'));
		}
	};

	$.fn.trx_vertical_accordion = function(method) {
		if (methods[method]) {
			return methods[method].apply(this, Array.prototype.slice.call(arguments, 1));
		}

		return methods.init.apply(this, arguments);
	};
});
