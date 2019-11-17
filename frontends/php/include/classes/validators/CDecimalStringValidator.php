<?php



/**
 * Class CDecimalStringValidator
 */
class CDecimalStringValidator extends CValidator {

	/**
	 * Error message for type and decimal format validation
	 *
	 * @var string
	 */
	public $messageInvalid;

	/**
	 * Returns true if the given $value is valid, or sets an error and returns false otherwise.
	 *
	 * @param $value
	 *
	 * @return bool
	 */
	public function validate($value) {
		$isValid = false;

		if (is_scalar($value)) {
			$isValid = ($this->isValidCommonNotation($value)
				|| $this->isValidDotNotation($value)
				|| $this->isValidScientificNotation($value));
		}

		if (!$isValid) {
			$this->error($this->messageInvalid, $this->stringify($value));
		}

		return $isValid;
	}

	/**
	 * Validates usual decimal syntax - "1.0", "0.11", "0".
	 *
	 * @param string $value
	 *
	 * @return boolean
	 */
	protected function isValidCommonNotation($value){
		return preg_match('/^-?\d+(\.\d+)?$/', $value);
	}

	/**
	 * Validates "dot notation" - ".11", "22."
	 *
	 * @param string $value
	 *
	 * @return boolean
	 */
	protected function isValidDotNotation($value) {
		return preg_match('/^-?(\.\d+|\d+\.)$/', $value);
	}

	/**
	 * Validate decimal string in scientific notation - "10e3", "1.0e-5".
	 *
	 * @param string $value
	 *
	 * @return boolean
	 */
	protected function isValidScientificNotation($value) {
		return preg_match('/^-?[0-9]+(\.[0-9]+)?e[+|-]?[0-9]+$/i', $value);
	}
}
