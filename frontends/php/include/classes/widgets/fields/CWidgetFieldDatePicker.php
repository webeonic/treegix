<?php



/**
 * Class makes datepicker widget field.
 */
class CWidgetFieldDatePicker extends CWidgetField {
	/**
	 * Date picker widget field.
	 *
	 * @param string $name   Field name in form.
	 * @param string $label  Label for the field in form.
	 */
	public function __construct($name, $label) {
		parent::__construct($name, $label);

		$this->setSaveType(TRX_WIDGET_FIELD_TYPE_STR);
		$this->setValidationRules(['type' => API_RANGE_TIME, 'length' => 255]);
		$this->setDefault('now');
	}

	/**
	 * Return javascript necessary to initialize field.
	 *
	 * @param string $form_name   Form name in which control is located.
	 * @param string $onselect    Callback script that is executed on date select.
	 *
	 * @return string
	 */
	public function getJavascript($form_name, $onselect = '') {
		return
			'var input = jQuery("[name=\"'.$this->getName().'\"]", jQuery("#'.$form_name.'")).get(0);'.

			'jQuery("#'.$this->getName().'_dp")'.
				'.data("clndr", create_calendar(input, null))'.
				'.data("input", input)'.
				'.click(function() {'.
					'var b = jQuery(this),'.
						'o = b.offset(),'.
						// h - calendar height.
						'h = jQuery(b.data("clndr").clndr.clndr_calendar).outerHeight(),'.
						// d - dialog offset.
						'd = jQuery("#overlay_dialogue").offset(),'.
						// t - calculated calendar top position.
						't = parseInt(o.top + b.outerHeight() - h - d.top, 10),'.
						// l - calculated calendar left position.
						'l = parseInt(o.left - d.left + b.outerWidth(), 10);'.

					'b.data("clndr").clndr.clndrshow(t, l, b.data("input"));'.
					($onselect !== '' ? 'b.data("clndr").clndr.onselect = function() {'.$onselect.'};' : '').

					'return false;'.
				'})';
	}
}
