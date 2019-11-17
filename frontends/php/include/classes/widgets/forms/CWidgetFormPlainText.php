<?php



/**
 * Plain text widget form.
 */
class CWidgetFormPlainText extends CWidgetForm {

	public function __construct($data) {
		parent::__construct($data, WIDGET_PLAIN_TEXT);

		// Items selector.
		$field_items = (new CWidgetFieldMsItem('itemids', _('Items')))
			->setFlags(CWidgetField::FLAG_NOT_EMPTY | CWidgetField::FLAG_LABEL_ASTERISK);

		if (array_key_exists('itemids', $this->data)) {
			$field_items->setValue($this->data['itemids']);
		}

		$this->fields[$field_items->getName()] = $field_items;

		// Location of the item names.
		$field_style = (new CWidgetFieldRadioButtonList('style', _('Items location'), [
			STYLE_LEFT => _('Left'),
			STYLE_TOP => _('Top')
		]))
			->setDefault(STYLE_LEFT)
			->setModern(true);

		if (array_key_exists('style', $this->data)) {
			$field_style->setValue($this->data['style']);
		}

		$this->fields[$field_style->getName()] = $field_style;

		// Number of records to display.
		$field_lines = (new CWidgetFieldIntegerBox('show_lines', _('Show lines'), ZBX_MIN_WIDGET_LINES,
			ZBX_MAX_WIDGET_LINES
		))
			->setFlags(CWidgetField::FLAG_LABEL_ASTERISK)
			->setDefault(ZBX_DEFAULT_WIDGET_LINES);

		if (array_key_exists('show_lines', $this->data)) {
			$field_lines->setValue($this->data['show_lines']);
		}

		$this->fields[$field_lines->getName()] = $field_lines;

		// Show text as HTML.
		$field_show_as_html = (new CWidgetFieldCheckBox('show_as_html', _('Show text as HTML')))->setDefault(0);

		if (array_key_exists('show_as_html', $this->data)) {
			$field_show_as_html->setValue($this->data['show_as_html']);
		}

		$this->fields[$field_show_as_html->getName()] = $field_show_as_html;

		// Use dynamic items.
		$dynamic_item = (new CWidgetFieldCheckBox('dynamic', _('Dynamic items')))->setDefault(WIDGET_SIMPLE_ITEM);

		if (array_key_exists('dynamic', $this->data)) {
			$dynamic_item->setValue($this->data['dynamic']);
		}

		$this->fields[$dynamic_item->getName()] = $dynamic_item;
	}
}
