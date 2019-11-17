<?php



/**
 * Class containing methods for operations with widgets.
 */
abstract class CControllerWidget extends CController {

	/**
	 * @var int $type  Widget type WIDGET_*.
	 */
	private $type;

	/**
	 * @var array $validation_rules  Validation rules for input parameters.
	 */
	private $validation_rules = [];

	/**
	 * @var object $form  CWidgetForm object.
	 */
	private $form;

	/**
	 * Initialization function.
	 */
	protected function init() {
		$this->disableSIDValidation();
	}

	/**
	 * Check user permissions.
	 *
	 * @return bool
	 */
	protected function checkPermissions() {
		return ($this->getUserType() >= USER_TYPE_TREEGIX_USER);
	}

	/**
	 * Set widget type.
	 *
	 * @param int $type  Widget type WIDGET_*.
	 *
	 * @return object
	 */
	protected function setType($type) {
		$this->type = $type;

		return $this;
	}

	/**
	 * Set validation rules for input parameters.
	 *
	 * @param array $validation_rules  Validation rules for input parameters.
	 *
	 * @return object
	 */
	protected function setValidationRules(array $validation_rules) {
		$this->validation_rules = $validation_rules;

		return $this;
	}

	/**
	 * Returns default widget header.
	 *
	 * @return string
	 */
	protected function getDefaultHeader() {
		return CWidgetConfig::getKnownWidgetTypes()[$this->type];
	}

	/**
	 * Validate input parameters.
	 *
	 * @return bool
	 */
	protected function checkInput() {
		$ret = $this->validateInput($this->validation_rules);

		if ($ret) {
			$this->form = CWidgetConfig::getForm($this->type, $this->getInput('fields', '{}'));

			if ($errors = $this->form->validate()) {
				foreach ($errors as $error) {
					info($error);
				}

				$ret = false;
			}
		}

		if (!$ret) {
			$output = [
				'header' => $this->getDefaultHeader()
			];

			if (($messages = getMessages()) !== null) {
				$output['messages'] = $messages->toString();
			}

			$this->setResponse(
				(new CControllerResponseData(['main_block' => (new CJson())->encode($output)]))->disableView()
			);
		}

		return $ret;
	}

	/**
	 * Returns form object.
	 *
	 * @return object
	 */
	protected function getForm() {
		return $this->form;
	}
}
