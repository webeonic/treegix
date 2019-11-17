<?php



/**
 * Class for standard ajax response generation.
 */
class CAjaxResponse {

	private $_result = true;
	private $_data = [];
	private $_errors = [];

	public function __construct($data = null) {
		if ($data !== null) {
			$this->success($data);
		}
	}

	/**
	 * Add error to ajax response. All errors are returned as array in 'errors' part of response.
	 *
	 * @param string $error error text
	 */
	public function error($error) {
		$this->_result = false;
		$this->_errors[] = ['error' => $error];
	}

	/**
	 * Assigns data that is returned in 'data' part of ajax response.
	 * If any error was added previously, this method does nothing.
	 *
	 * @param array $data
	 */
	public function success(array $data) {
		if ($this->_result) {
			$this->_data = $data;
		}
	}

	/**
	 * Output ajax response. If any error was added, 'result' is false, otherwise true.
	 */
	public function send() {
		$json = new CJson();

		if ($this->_result) {
			echo $json->encode(['result' => true, 'data' => $this->_data]);
		}
		else {
			echo $json->encode(['result' => false, 'errors' => $this->_errors]);
		}
	}
}
