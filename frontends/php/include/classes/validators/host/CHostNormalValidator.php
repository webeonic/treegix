<?php



class CHostNormalValidator extends CValidator {

	/**
	 * Error message
	 *
	 * @var string
	 */
	public $message;

	/**
	 * Checks is any of the given hosts are discovered.
	 *
	 * @param $hostIds
	 *
	 * @return bool
	 */
	public function validate($hostIds) {
		$hosts = API::Host()->get([
			'output' => ['host'],
			'hostids' => $hostIds,
			'filter' => ['flags' => TRX_FLAG_DISCOVERY_CREATED],
			'limit' => 1
		]);

		if ($hosts) {
			$host = reset($hosts);
			$this->error($this->message, $host['host']);
			return false;
		}

		return true;
	}
}
