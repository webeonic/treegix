


function organizeInterfaces(interfaceType) {
	var selectedInterfaceId = +jQuery('#selectedInterfaceId').val();
	var matchingInterfaces = jQuery('#interfaceid option[data-interfacetype="' + interfaceType + '"]');

	var selectedInterfaceOption;
	if (selectedInterfaceId) {
		selectedInterfaceOption = jQuery('#interfaceid option[value="' + selectedInterfaceId + '"]');
	}

	if (jQuery('#visible_interface').data('multipleInterfaceTypes') && !jQuery('#visible_type').is(':checked')) {
		jQuery('#interface_not_defined').html(t('To set a host interface select a single item type for all items')).show();
		jQuery('#interfaceid').hide();
	}
	else {
		// a specific interface is required
		if (interfaceType > 0) {
			// we have some matching interfaces available
			if (matchingInterfaces.length) {
				jQuery('#interfaceid option')
					.prop('selected', false)
					.prop('disabled', true)
					.filter('[value="0"]').remove();
				matchingInterfaces.prop('disabled', false);

				// select the interface by interfaceid, if it's available
				if (selectedInterfaceId && !selectedInterfaceOption.prop('disabled')) {
					jQuery('#interfaceid').val(selectedInterfaceId);
				}
				// if no interfaceid is given, select the first suitable interface
				else {
					matchingInterfaces.first().prop('selected', true);
				}

				jQuery('#interfaceid').show();
				jQuery('#interface_not_defined').hide();
			}
			// no matching interfaces available
			else {
				// hide combobox and display warning text
				if (!jQuery('#interfaceid option[value="0"]').length) {
					jQuery('#interfaceid').prepend('<option value="0"></option>');
				}
				jQuery('#interfaceid').hide().val(0);
				jQuery('#interface_not_defined').html(t('No interface found')).show();
			}
		}
		// any interface or no interface
		else {
			// no interface required
			if (interfaceType === null) {
				if (!jQuery('#interfaceid option[value="0"]').length) {
					jQuery('#interfaceid').prepend('<option value="0"></option>');
				}

				jQuery('#interfaceid option')
					.prop('disabled', true)
					.filter('[value="0"]').prop('disabled', false);
				jQuery('#interfaceid').val(0);
			}
			// any interface
			else {
				jQuery('#interfaceid option')
					.prop('disabled', false)
					.filter('[value="0"]').remove();
				if (selectedInterfaceId) {
					selectedInterfaceOption.prop('selected', true);
				}
			}

			jQuery('#interfaceid').show();
			jQuery('#interface_not_defined').hide();
		}
	}
}
