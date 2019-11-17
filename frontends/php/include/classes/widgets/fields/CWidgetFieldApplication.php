<?php



/**
 * Class to build UI control to select application in widget configuration window.
 */
class CWidgetFieldApplication extends CWidgetField {

	private $filter_parameters = [
		'srctbl' => 'applications',
		'srcfld1' => 'name',
		'with_applications' => '1',
		'real_hosts' => '1'
	];

	/**
	 * Create widget field for Application selection.
	 *
	 * @param string $name   Field name in form.
	 * @param string $label  Label for the field in form.
	 */
	public function __construct($name, $label) {
		parent::__construct($name, $label);

		$this->filter_parameters['dstfld1'] = $name;
		$this->setSaveType(ZBX_WIDGET_FIELD_TYPE_STR);
		$this->setDefault('');
	}

	/**
	 * Returns parameters specified for popup opened when user clicks on Select button.
	 *
	 * @return array
	 */
	public function getFilterParameters() {
		return $this->filter_parameters;
	}
}
