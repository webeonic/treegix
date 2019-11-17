<?php



class CWidgetFieldReference extends CWidgetField {

	// This field name is reserved by Treegix for this particular use case. See comments below.
	const FIELD_NAME = 'reference';

	/**
	 * Reference widget field. If added to widget, will generate unique value across the dashboard
	 * and will be saved to database. This field should be used to save relations between widgets.
	 */
	public function __construct() {
		/*
		 * All reference fields for all widgets on dashboard should share the same name.
		 * It is needed to make possible search if value is not taken by some other widget in same dashboard.
		 */
		parent::__construct(self::FIELD_NAME, null);

		$this->setSaveType(ZBX_WIDGET_FIELD_TYPE_STR);
	}

	/**
	 * JS script, that will call reference generation, if reference is not yet created.
	 *
	 * @param string $form_selector  jQuery context selector for the configuration form (with # or . character)
	 *
	 * @return string
	 */
	public function getJavascript($form_selector) {
		return
			'var reference_field = jQuery("input[name=\"'.$this->getName().'\"]", "'.$form_selector.'");'.
			'if (!reference_field.val().length) {'.
				'var reference = jQuery(".dashbrd-grid-container").dashboardGrid("makeReference");'.
				'reference_field.val(reference);'.
			'}';
	}

	/**
	 * Set field value.
	 *
	 * @param string $value  Reference value. Only numeric characters allowed.
	 *
	 * @return CWidgetFieldReference
	 */
	public function setValue($value) {
		if ($value === '' || ctype_alnum($value)) {
			$this->value = $value;
		}

		return $this;
	}
}
