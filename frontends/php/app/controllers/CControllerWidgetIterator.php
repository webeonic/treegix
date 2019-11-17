<?php
 


/**
 * Class containing methods for operations with widget iterators.
 */
abstract class CControllerWidgetIterator extends CControllerWidget {

	/**
	 * @var array $iterator_validation_rules  Validation rules for input parameters of the iterator.
	 */
	private static $iterator_validation_rules = [
		'page' => 'required|ge 1'
	];

	public function __construct() {
		parent::__construct();

		$this->setValidationRules(self::$iterator_validation_rules);
	}

	/**
	 * Set validation rules for input parameters.
	 *
	 * @param array $validation_rules  Validation rules for input parameters.
	 *
	 * @return object
	 */
	protected function setValidationRules(array $validation_rules) {
		return parent::setValidationRules(array_merge(self::$iterator_validation_rules, $validation_rules));
	}

	/**
	 * Get realistic page number for given number of child widgets.
	 *
	 * @param int $num_widgets  Number of child widgets.
	 *
	 * @return int  Page number.
	 */
	protected function getIteratorPage($num_widgets) {
		return max(1, min((int) $this->getInput('page'), $this->getIteratorPageCount($num_widgets)));
	}

	/**
	 * Get number of child widgets to show on a single page.
	 *
	 * @return int  Number of child widgets.
	 */
	protected function getIteratorPageSize() {
		$fields_data = $this->getForm()->getFieldsData();

		return min($fields_data['rows'] * $fields_data['columns'],
			floor(DASHBOARD_MAX_COLUMNS * DASHBOARD_WIDGET_MAX_ROWS / DASHBOARD_WIDGET_MIN_ROWS)
		);
	}

	/**
	 * Get number of pages for given number of child widgets.
	 *
	 * @param int $num_widgets  Number of child widgets.
	 *
	 * @return int  Number of pages.
	 */
	protected function getIteratorPageCount($num_widgets) {
		return (floor(max(0, $num_widgets - 1) / $this->getIteratorPageSize()) + 1);
	}
}
