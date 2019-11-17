<?php



class CWidgetFieldTags extends CWidgetField {

	/**
	 * Create widget field for Tags selection.
	 *
	 * @param string $name   Field name in form.
	 * @param string $label  Label for the field in form.
	 */
	public function __construct($name, $label) {
		parent::__construct($name, $label);

		$this->setSaveType(TRX_WIDGET_FIELD_TYPE_STR);
		$this->setValidationRules(['type' => API_OBJECTS, 'fields' => [
			'tag'		=> ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED, 'length' => 255],
			'operator'	=> ['type' => API_INT32, 'flags' => API_REQUIRED, 'in' => implode(',', [TAG_OPERATOR_LIKE, TAG_OPERATOR_EQUAL])],
			'value'		=> ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED, 'length' => 255]
		]]);
		$this->setDefault([]);
	}

	public function setValue($value) {
		$this->value = (array) $value;

		return $this;
	}

	/**
	 * Get field value. If no value is set, will return default value.
	 *
	 * @return mixed
	 */
	public function getValue() {
		$value = parent::getValue();

		foreach ($value as $index => $val) {
			if ($val['tag'] === '' && $val['value'] === '') {
				unset($value[$index]);
			}
		}

		return $value;
	}

	/**
	 * Add dynamic row script and fix the distance between AND/OR buttons and tag inputs below them.
	 *
	 * @return string
	 */
	public function getJavascript() {
		return 'var tags_table = jQuery("#tags_table_'.$this->getName().'");'.
			'tags_table.dynamicRows({template: "#tag-row-tmpl"});'.
			'tags_table.parent().addClass("has-before");';
	}

	/**
	 * Prepares array entry for widget field, ready to be passed to CDashboard API functions.
	 * Reference is needed here to avoid array merging in CWidgetForm::fieldsToApi method. With large number of widget
	 * fields it causes significant performance decrease.
	 *
	 * @param array $widget_fields   reference to Array of widget fields.
	 */
	public function toApi(array &$widget_fields = []) {
		$value = $this->getValue();

		foreach ($value as $index => $val) {
			$widget_fields[] = [
				'type' => $this->save_type,
				'name' => $this->name.'.tag.'.$index,
				'value' => $val['tag']
			];
			$widget_fields[] = [
				'type' => TRX_WIDGET_FIELD_TYPE_INT32,
				'name' => $this->name.'.operator.'.$index,
				'value' => $val['operator']
			];
			$widget_fields[] = [
				'type' => $this->save_type,
				'name' => $this->name.'.value.'.$index,
				'value' => $val['value']
			];
		}
	}
}
