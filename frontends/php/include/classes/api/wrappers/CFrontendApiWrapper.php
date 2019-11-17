<?php



/**
 * This class can be used for any pre-processing required for API calls made from the frontend.
 */
class CFrontendApiWrapper extends CApiWrapper {

	/**
	 * Whether to enable debug mode.
	 *
	 * @var bool
	 */
	public $debug = false;

	/**
	 * The profiler class used for profiling API calls.
	 *
	 * @var CProfiler
	 */
	protected $profiler;

	/**
	 * Set the profiler class.
	 *
	 * @param CProfiler $profiler
	 */
	public function setProfiler(CProfiler $profiler) {
		$this->profiler = $profiler;
	}

	/**
	 * Call the API method with profiling.
	 *
	 * If the API call has been unsuccessful - return only the result value.
	 * If the API call has been unsuccessful - add an error message and return false, instead of an array.
	 *
	 * @param string 	$method
	 * @param mixed 	$params
	 *
	 * @return mixed
	 */
	protected function callMethod($method, $params) {
		API::setWrapper();
		$response = parent::callMethod($method, $params);
		API::setWrapper($this);

		// call profiling
		if ($this->debug) {
			$this->profiler->profileApiCall($this->api, $method, $params, $response->data);
		}

		if (!$response->errorCode) {
			return $response->data;
		}
		else {
			// add an error message
			$trace = $response->errorMessage;
			if ($response->debug) {
				$trace .= ' ['.$this->profiler->formatCallStack($response->debug).']';
			}
			error($trace);

			return false;
		}
	}

	/**
	 * Call the client method. Pass the "auth" parameter only to the methods that require it.
	 *
	 * @param string $method		API method
	 * @param array  $params		API parameters
	 *
	 * @return CApiClientResponse
	 */
	protected function callClientMethod($method, $params) {
		$auth = ($this->requiresAuthentication($this->api, $method)) ? $this->auth : null;

		return $this->client->callMethod($this->api, $method, $params, $auth);
	}

	/**
	 * Returns true if calling the given method requires an authentication token.
	 *
	 * @param $api
	 * @param $method
	 *
	 * @return bool
	 */
	protected function requiresAuthentication($api, $method) {
		return !(($api === 'user' && $method === 'login')
			|| ($api === 'user' && $method === 'checkAuthentication')
			|| ($api === 'apiinfo' && $method === 'version'));
	}
}
