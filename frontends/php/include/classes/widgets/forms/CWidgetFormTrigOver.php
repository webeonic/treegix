<?php



/**
 * Trigger overview widget form.
 */
class CWidgetFormTrigOver extends CWidgetForm {

	public function __construct($data) {
		parent::__construct($data, WIDGET_TRIG_OVER);

		// Output information option.
		$field_show = (new CWidgetFieldRadioButtonList('show', _('Show'), [
			TRIGGERS_OPTION_RECENT_PROBLEM => _('Recent problems'),
			TRIGGERS_OPTION_IN_PROBLEM => _('Problems'),
			TRIGGERS_OPTION_ALL => _('Any')
		]))
			->setDefault(TRIGGERS_OPTION_RECENT_PROBLEM)
			->setModern(true);

		if (array_key_exists('show', $this->data)) {
			$field_show->setValue($this->data['show']);
		}

		$this->fields[$field_show->getName()] = $field_show;

		// Host groups.
		$field_groups = new CWidgetFieldMsGroup('groupids', _('Host groups'));

		if (array_key_exists('groupids', $this->data)) {
			$field_groups->setValue($this->data['groupids']);
		}

		$this->fields[$field_groups->getName()] = $field_groups;

		// Application field.
		$field_application = new CWidgetFieldApplication('application', _('Application'));

		if (array_key_exists('application', $this->data)) {
			$field_application->setValue($this->data['application']);
		}

		$this->fields[$field_application->getName()] = $field_application;

		// Show suppressed problems.
		$field_show_suppressed = (new CWidgetFieldCheckBox('show_suppressed', _('Show suppressed problems')))
			->setDefault(ZBX_PROBLEM_SUPPRESSED_FALSE);

		if (array_key_exists('show_suppressed', $this->data)) {
			$field_show_suppressed->setValue($this->data['show_suppressed']);
		}

		$this->fields[$field_show_suppressed->getName()] = $field_show_suppressed;

		// Hosts names location.
		$field_style = (new CWidgetFieldRadioButtonList('style', _('Hosts location'), [
			STYLE_LEFT => _('Left'),
			STYLE_TOP => _('Top')
		]))
			->setDefault(STYLE_LEFT)
			->setModern(true);

		if (array_key_exists('style', $this->data)) {
			$field_style->setValue($this->data['style']);
		}

		$this->fields[$field_style->getName()] = $field_style;
	}
}
