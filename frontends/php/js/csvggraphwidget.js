


/**
 * Call SVG graph widget internal processes.
 *
 * @param {string} hook_name - trigger name.
 */
function zbx_svggraph_widget_trigger(hook_name) {
	var grid = Array.prototype.slice.call(arguments, -1),
		grid = grid.length ? grid[0] : null;

	switch (hook_name) {
		case 'onResizeEnd':
			jQuery('.dashbrd-grid-container').dashboardGrid('refreshWidget', grid.widget['uniqueid']);
			break;

		case 'onEditStart':
			jQuery('svg', grid.widget['content_body']).svggraph('disableSBox');
			break;
	}
}
