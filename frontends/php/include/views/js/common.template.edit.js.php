<?php



?>
<script type="text/javascript">
	/**
	 * @see init.js add.popup event
	 */
	function addPopupValues(data) {
		if (!isset('object', data) || data.object !== 'hostid') {
			return false;
		}

		for (var i = 0, len = data.values.length; i < len; i++) {
			create_var(data.parentId, 'add_templates[' + data.values[i].id + ']', data.values[i].id, false);
		}

		submitFormWithParam(data.parentId, "add_template", "1");
	}
</script>
