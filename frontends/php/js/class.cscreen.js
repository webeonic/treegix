 

var TRX_SCREENS = []; // screens obj reference
Position.includeScrollOffsets = true;

// screenid always must be a string (js doesn't support uint64) !
function init_screen(screenid, obj_id, id) {
	if (typeof(id) == 'undefined') {
		id = TRX_SCREENS.length;
	}

	if (is_number(screenid) && screenid > 100000000000000) {
		throw('Error: Wrong type of arguments passed to function [init_screen]');
	}

	TRX_SCREENS[id] = new Object;
	TRX_SCREENS[id].screen = new Cscreen(screenid, obj_id, id);
}

var Cscreen = Class.create();
Cscreen.prototype = {
	id: 0,
	screenid: 0,
	screen_obj: null, // DOM ref to screen obj

	initialize: function(screenid, obj_id, id) {
		this.screenid = screenid;
		this.id = id;
		this.screen_obj = $(obj_id);

		function wedge() {
			return false;
		}

		jQuery('.draggable').draggable({
			revert: 'invalid',
			zIndex: 999,
			start: function() {
				if (IE) {
					Event.observe(document.body, 'drag', wedge, false);
					Event.observe(document.body, 'selectstart', wedge, false);
				}
			}
		});

		jQuery('.screenitem').droppable({
			accept: '.draggable',
			drop: this.on_drop,
			tolerance: 'pointer'
		});
	},

	on_drop: function(event, ui) {
		var element = ui.draggable;
		var dropDiv = jQuery(this).children('.draggable');

		var x1 = element.data('xcoord');
		var y1 = element.data('ycoord');
		var x2 = dropDiv.data('xcoord');
		var y2 = dropDiv.data('ycoord');

		var url = new Curl();
		var params = {
			ajaxAction: 'sw_pos',
			output: 'ajax',
			'sw_pos[0]': y1,
			'sw_pos[1]': x1,
			'sw_pos[2]': y2,
			'sw_pos[3]': x2,
			screenid: url.getArgument('screenid'),
			sid: url.getArgument('sid')
		};

		jQuery.post('screenedit.php', params, function(data) {
			if (!isset('result', data) || !data.result) {
				overlayDialogue({
					'title': 'Error',
					'content': jQuery('<span>').text('Ajax request error'),
					'buttons': []
				});
			}
			else {
				var parent = dropDiv.parent().get(0);
				element.parent().get(0).appendChild(dropDiv.get(0));
				parent.appendChild(element.get(0));

				// replace "change" link href for emtpy cells
				var link = jQuery(element).find('.empty_change_link')[0];
				if (link) {
					var href = jQuery(link).attr('href');
					href = href.replace(/\&x\=[0-9]+/, '&x=' + x2);
					href = href.replace(/\&y\=[0-9]+/, '&y=' + y2);
					jQuery(link).attr('href', href);
				}

				var link = jQuery(dropDiv).find('.empty_change_link')[0];
				if (link) {
					var href = jQuery(link).attr('href');
					href = href.replace(/\&x\=[0-9]+/, '&x=' + x1);
					href = href.replace(/\&y\=[0-9]+/, '&y=' + y1);
					jQuery(link).attr('href', href);
				}

				element.data({ycoord: y2, xcoord: x2});
				dropDiv.data({ycoord: y1, xcoord: x1});
			}
		}, 'json');

		element.css({top: '0px', left: '0px'});
	}
};
