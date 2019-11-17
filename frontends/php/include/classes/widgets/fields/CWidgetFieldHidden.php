<?php



class CWidgetFieldHidden extends CWidgetField {

	/**
	 * Hidden widget field. Will not be displayed for user. Can contain string, int or id type value.
	 *
	 * @param string $name       field name in form
	 * @param int    $save_type  ZBX_WIDGET_FIELD_TYPE_ constant. Defines how field will be saved in database.
	 */
	public function __construct($name, $save_type) {
		parent::__construct($name, null);

		$this->setSaveType($save_type);
	}
}
