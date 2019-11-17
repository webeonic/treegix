<?php



/**
 * This class should be used for calling API services.
 */
abstract class CApiClient {

	/**
	 * Call the given API service method and return the response.
	 *
	 * @param string 	$api
	 * @param string 	$method
	 * @param mixed 	$params
	 * @param string	$auth
	 *
	 * @return CApiClientResponse
	 */
	abstract public function callMethod($api, $method, $params, $auth);
}
