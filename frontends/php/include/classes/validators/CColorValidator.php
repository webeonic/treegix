<?php



class CColorValidator extends CStringValidator {

	/**
	 * Hex color code regex.
	 *
	 * @var string
	 */
	public $regex = '/^[0-9a-f]{6}$/i';

	public function __construct(array $options = []) {
		$this->messageRegex = _('Colour "%1$s" is not correct: expecting hexadecimal colour code (6 symbols).');
		$this->messageEmpty = _('Empty colour.');

		parent::__construct($options);
	}
}
