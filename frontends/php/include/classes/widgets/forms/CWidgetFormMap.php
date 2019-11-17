<?php



/**
 * Map widget form.
 */
class CWidgetFormMap extends CWidgetForm {

	public function __construct($data) {
		parent::__construct($data, WIDGET_MAP);

		// Widget reference field.
		$field_reference = (new CWidgetFieldReference())->setDefault('');

		if (array_key_exists($field_reference->getName(), $this->data)) {
			$field_reference->setValue($this->data[$field_reference->getName()]);
		}

		$this->fields[$field_reference->getName()] = $field_reference;

		// Select source type field.
		$field_source_type = (new CWidgetFieldRadioButtonList('source_type', _('Source type'), [
			WIDGET_SYSMAP_SOURCETYPE_MAP => _('Map'),
			WIDGET_SYSMAP_SOURCETYPE_FILTER => _('Map navigation tree'),
		]))
			->setDefault(WIDGET_SYSMAP_SOURCETYPE_MAP)
			->setAction('updateWidgetConfigDialogue()')
			->setModern(true);

		if (array_key_exists('source_type', $this->data)) {
			$field_source_type->setValue($this->data['source_type']);
		}

		$this->fields[$field_source_type->getName()] = $field_source_type;

		if ($field_source_type->getValue() === WIDGET_SYSMAP_SOURCETYPE_FILTER) {
			// Select filter widget field.
			$field_filter_widget = (new CWidgetFieldWidgetListComboBox('filter_widget_reference', _('Filter'),
				'type', 'navtree'
			))
				->setDefault('')
				->setFlags(CWidgetField::FLAG_NOT_EMPTY | CWidgetField::FLAG_LABEL_ASTERISK);

			if (array_key_exists('filter_widget_reference', $this->data)) {
				$field_filter_widget->setValue($this->data['filter_widget_reference']);
			}

			$this->fields[$field_filter_widget->getName()] = $field_filter_widget;
		}
		else {
			// Select sysmap field.
			$field_map = (new CWidgetFieldSelectResource('sysmapid', _('Map'), WIDGET_FIELD_SELECT_RES_SYSMAP))
				->setFlags(CWidgetField::FLAG_NOT_EMPTY | CWidgetField::FLAG_LABEL_ASTERISK);

			if (array_key_exists('sysmapid', $this->data)) {
				$field_map->setValue($this->data['sysmapid']);
			}

			$this->fields[$field_map->getName()] = $field_map;
		}
	}
}
