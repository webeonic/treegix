<?php



/**
 * Convert array keys to numeric.
 */
class CArrayKeysImportConverter extends CConverter {

	protected $rules;

	public function __construct(array $schema) {
		$this->rules = $schema;
	}

	public function convert($data) {
		$data['treegix_export'] = $this->normalizeArrayKeys($data['treegix_export'], $this->rules);

		return $data;
	}

	/**
	 * Convert array keys to numeric.
	 *
	 * @param mixed $data   Import data.
	 * @param array $rules  XML rules.
	 *
	 * @return array
	 */
	protected function normalizeArrayKeys($data, array $rules) {
		if (!is_array($data)) {
			return $data;
		}

		if ($rules['type'] & XML_ARRAY) {
			foreach ($rules['rules'] as $tag => $tag_rules) {
				if (array_key_exists('ex_rules', $tag_rules)) {
					$tag_rules = call_user_func($tag_rules['ex_rules'], $data);
				}

				if (array_key_exists($tag, $data)) {
					$data[$tag] = $this->normalizeArrayKeys($data[$tag], $tag_rules);
				}
			}
		}
		elseif ($rules['type'] & XML_INDEXED_ARRAY) {
			$prefix = $rules['prefix'];

			foreach ($data as $tag => $value) {
				$data[$tag] = $this->normalizeArrayKeys($value, $rules['rules'][$prefix]);
			}

			$data = array_values($data);
		}

		return $data;
	}
}
