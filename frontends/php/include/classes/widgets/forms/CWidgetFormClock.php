<?php



/**
 * Clock widget form.
 */
class CWidgetFormClock extends CWidgetForm {

	public function __construct($data) {
		parent::__construct($data, WIDGET_CLOCK);

		// Time type field.
		$field_time_type = (new CWidgetFieldComboBox('time_type', _('Time type'), [
			TIME_TYPE_LOCAL => _('Local time'),
			TIME_TYPE_SERVER => _('Server time'),
			TIME_TYPE_HOST => _('Host time')
		]))
			->setDefault(TIME_TYPE_LOCAL)
			->setAction('updateWidgetConfigDialogue()');

		if (array_key_exists('time_type', $this->data)) {
			$field_time_type->setValue($this->data['time_type']);
		}

		$this->fields[$field_time_type->getName()] = $field_time_type;

		// Item field.
		if ($field_time_type->getValue() === TIME_TYPE_HOST) {
			// Item multiselector with single value.
			$field_item = (new CWidgetFieldMsItem('itemid', _('Item')))
				->setFlags(CWidgetField::FLAG_NOT_EMPTY | CWidgetField::FLAG_LABEL_ASTERISK)
				->setMultiple(false);

			if (array_key_exists('itemid', $this->data)) {
				$field_item->setValue($this->data['itemid']);
			}

			$this->fields[$field_item->getName()] = $field_item;
		}
	}
}
