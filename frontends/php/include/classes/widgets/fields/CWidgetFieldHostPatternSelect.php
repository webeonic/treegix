<?php
 


class CWidgetFieldHostPatternSelect extends CWidgetField {

	private $placeholder;

	/**
	 * Textarea widget field.
	 *
	 * @param string $name  field name in form
	 * @param string $label  label for the field in form
	 */
	public function __construct($name, $label) {
		parent::__construct($name, $label);

		$this->setDefault([]);
		$this->placeholder = null;

		/*
		 * Set validation rules bypassing a parent::setSaveType to skip validation of length.
		 * Save type is set in self::toApi method for each string field separately.
		 */
		$this->setValidationRules(['type' => API_STRINGS_UTF8]);
	}

	/**
	 * Prepares array entry for widget field, ready to be passed to CDashboard API functions.
	 * Reference is needed here to avoid array merging in CWidgetForm::fieldsToApi method. With large number of widget
	 * fields it causes significant performance decrease.
	 *
	 * @param array $widget_fields   reference to array of widget fields.
	 */
	public function toApi(array &$widget_fields = []) {
		$value = $this->getValue();

		if ($value !== $this->default) {
			foreach ($value as $num => $val) {
				$widget_fields[] = [
					'type' => ZBX_WIDGET_FIELD_TYPE_STR,
					'name' => $this->name.'.'.$num,
					'value' => $val
				];
			}
		}
	}

	public function setPlaceholder($placeholder) {
		$this->placeholder = $placeholder;

		return $this;
	}

	public function getPlaceholder() {
		return $this->placeholder;
	}

	public function getJavascript() {
		$fieldid = zbx_formatDomId($this->getName().'[]');

		return 'jQuery("#'.$fieldid.'").multiSelect(jQuery("#'.$fieldid.'").data("params"));';
	}
}
