<?php



/**
 * URL widget form.
 */
class CWidgetFormUrl extends CWidgetForm {

	public function __construct($data) {
		parent::__construct($data, WIDGET_URL);

		// URL field.
		$field_url = (new CWidgetFieldUrl('url', _('URL')))
			->setFlags(CWidgetField::FLAG_NOT_EMPTY | CWidgetField::FLAG_LABEL_ASTERISK);

		if (array_key_exists('url', $this->data)) {
			$field_url->setValue($this->data['url']);
		}

		$this->fields[$field_url->getName()] = $field_url;

		// Dynamic item.
		$field_dynamic = (new CWidgetFieldCheckBox('dynamic', _('Dynamic item')))->setDefault(WIDGET_SIMPLE_ITEM);

		if (array_key_exists('dynamic', $this->data)) {
			$field_dynamic->setValue($this->data['dynamic']);
		}

		$this->fields[$field_dynamic->getName()] = $field_dynamic;
	}
}
