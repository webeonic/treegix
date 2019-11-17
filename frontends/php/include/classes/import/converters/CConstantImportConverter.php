<?php



/**
 * Convert constants to their values.
 */
class CConstantImportConverter extends CConverter {

	protected $rules;

	public function __construct(array $schema) {
		$this->rules = $schema;
	}

	public function convert($data) {
		$data['treegix_export'] = $this->replaceConstant($data['treegix_export'], $this->rules);

		return $data;
	}

	/**
	 * Convert human readable import constants to values Treegix API can work with.
	 *
	 * @param mixed  $data   Import data.
	 * @param array  $rules  XML rules.
	 * @param string $path   XML path (for error reporting).
	 *
	 * @throws Exception if the element is invalid.
	 *
	 * @return mixed
	 */
	protected function replaceConstant($data, array $rules, $path = '') {
		if ($rules['type'] & XML_STRING) {
			if (!array_key_exists('in', $rules)) {
				return $data;
			}

			$flip = array_flip($rules['in']);

			if (!array_key_exists($data, $flip)) {
				throw new Exception(
					_s('Invalid tag "%1$s": %2$s.', $path, _s('unexpected constant value "%1$s"', $data))
				);
			}

			$data = (string) $flip[$data];
		}
		elseif ($rules['type'] & XML_ARRAY) {
			foreach ($rules['rules'] as $tag => $tag_rules) {
				if (array_key_exists($tag, $data)) {
					if (array_key_exists('ex_rules', $tag_rules)) {
						$tag_rules = call_user_func($tag_rules['ex_rules'], $data);
					}

					$data[$tag] = $this->replaceConstant($data[$tag], $tag_rules, $tag);
				}
			}
		}
		elseif ($rules['type'] & XML_INDEXED_ARRAY) {
			$prefix = $rules['prefix'];

			foreach ($data as $tag => $value) {
				$data[$tag] = $this->replaceConstant($value, $rules['rules'][$prefix]);
			}
		}

		return $data;
	}
}
