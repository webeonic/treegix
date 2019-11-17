<?php



/**
 * Class containing methods for operations with API.
 */
class CAPIInfo extends CApiService {

	/**
	 * Get API version.
	 *
	 * @return string
	 */
	public function version(array $request) {
		$api_input_rules = ['type' => API_OBJECT, 'fields' =>[]];
		if (!CApiInputValidator::validate($api_input_rules, $request, '/', $error)) {
			self::exception(TRX_API_ERROR_PARAMETERS, $error);
		}

		return TREEGIX_API_VERSION;
	}
}
