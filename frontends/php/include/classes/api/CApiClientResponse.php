<?php



/**
 * This class is used by the API client to return the results of an API call.
 */
class CApiClientResponse {

	/**
	 * Data returned by the service method.
	 *
	 * @var mixed
	 */
	public $data;

	/**
	 * Error code.
	 *
	 * @var	int
	 */
	public $errorCode;

	/**
	 * Error message.
	 *
	 * @var	string
	 */
	public $errorMessage;

	/**
	 * Debug information.
	 *
	 * @var	array
	 */
	public $debug;
}
