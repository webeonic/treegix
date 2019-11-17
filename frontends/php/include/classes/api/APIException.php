<?php



class APIException extends Exception {

	/**
	 * @param int    $code
	 * @param string $message
	 */
	public function __construct($code = ZBX_API_ERROR_INTERNAL, $message = '') {
        parent::__construct($message, $code);
    }
}
