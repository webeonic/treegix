<?php



class CIdValidator extends CValidator {

	/**
	 * Value to determine whether to allow empty values
	 *
	 * @var bool
	 */
	public $empty = false;

	/**
	 * Error message if the id has wrong type or id is out of range or invalid
	 *
	 * @var string
	 */
	public $messageInvalid;

	/**
	 * Error message if the id is empty
	 *
	 * @var string
	 */
	public $messageEmpty;

	/**
	 * Validates ID value
	 *
	 * @param string $value
	 *
	 * @return bool
	 */
	public function validate($value) {
		if (!is_string($value) && !is_int($value)) {
			$this->error($this->messageInvalid, $this->stringify($value));

			return false;
		}

		if (!$this->empty && (string) $value === '0') {
			$this->error($this->messageEmpty);

			return false;
		}

		$regex = $this->empty ? '/^(0|(?!0)[0-9]+)$/' : '/^(?!0)\d+$/';

		if (!preg_match($regex, $value) ||
			bccomp($value, 0)  == -1 ||
			bccomp($value, TRX_DB_MAX_ID) == 1
		) {
			$this->error($this->messageInvalid, $value);

			return false;
		}

		return true;
	}
}
