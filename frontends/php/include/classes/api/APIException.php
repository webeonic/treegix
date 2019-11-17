<?php



class APIException extends Exception {

	/**
	 * @param int    $code
	 * @param string $message
	 */
	public function __construct($code = TRX_API_ERROR_INTERNAL, $message = '') {
        parent::__construct($message, $code);
    }
}
