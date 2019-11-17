<?php



/**
 * Class to validate e-mails.
 */
class CEmailValidator extends CStringValidator {

	/**
	 * Function validates given string against the defined e-mail pattern.
	 *
	 * @param string $value  String to validate against defined e-mail pattern.
	 *
	 * @return bool
	 */
	public function validate($value) {
		if (!filter_var($value, FILTER_VALIDATE_EMAIL)) {
			preg_match('/.*<(?<email>.*[^>])>$/i', $value, $match);

			if (!array_key_exists('email', $match) || !filter_var($match['email'], FILTER_VALIDATE_EMAIL)) {
				$this->setError(_s('Invalid email address "%1$s".', $value));

				return false;
			}
		}

		return true;
	}
}
