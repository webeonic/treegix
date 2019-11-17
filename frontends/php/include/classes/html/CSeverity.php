<?php



class CSeverity extends CRadioButtonList {
	/**
	 * Options array.
	 *
	 * string $options['name']   HTML element name.
	 * int    $options['value']  Default selected value, default TRIGGER_SEVERITY_NOT_CLASSIFIED.
	 * bool   $options['all']    Is option 'All' available, default false.
	 * @property array
	 */
	private $options = [
		'all'	=> false,
		'value' => TRIGGER_SEVERITY_NOT_CLASSIFIED
	];

	/**
	 * @param array $options    Array of options.
	 * @param bool  $enabled    If set to false, radio buttons (severities) are marked as disabled.
	 */
	public function __construct(array $options = [], $enabled = true) {
		$this->options = $options + $this->options;

		parent::__construct($this->options['name'], $this->options['value']);

		$this->setModern(true);
		$this->setEnabled($enabled);
	}

	/**
	 * Add value.
	 *
	 * @param string $label      Input element label.
	 * @param string $value      Input element value.
	 * @param string $class      List item class name.
	 * @param string $on_change  Javascript handler for onchange event.
	 *
	 * @return CSeverity
	 */
	public function addValue($label, $value, $class = null, $on_change = null) {
		$this->values[] = [
			'name' => $label,
			'value' => $value,
			'id' => null,
			'class' => $class,
			'on_change' => $on_change
		];

		return $this;
	}

	public function toString($destroy = true) {
		$config = select_config();

		$severities = [
			TRIGGER_SEVERITY_NOT_CLASSIFIED, TRIGGER_SEVERITY_INFORMATION, TRIGGER_SEVERITY_WARNING,
			TRIGGER_SEVERITY_AVERAGE, TRIGGER_SEVERITY_HIGH, TRIGGER_SEVERITY_DISASTER
		];

		if ($this->options['all']) {
			$this->addValue(_('all'), -1);
		}

		foreach ($severities as $severity) {
			$this->addValue(getSeverityName($severity, $config), $severity, getSeverityStyle($severity));
		}

		return parent::toString($destroy);
	}
}
