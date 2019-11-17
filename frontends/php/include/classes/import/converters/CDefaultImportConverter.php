<?php



/**
 * Add tags with default values.
 */
class CDefaultImportConverter extends CConverter {

	protected $rules;

	public function __construct(array $schema) {
		$this->rules = $schema;
	}

	public function convert($data) {
		foreach ($this->rules['rules'] as $tag => $tag_rules) {
			if (!array_key_exists($tag, $data['treegix_export'])) {
				continue;
			}

			$data['treegix_export'][$tag] = $this->addDefaultValue($data['treegix_export'][$tag], $tag_rules);
		}

		return $data;
	}

	/**
	 * Add default values in place of missed tags.
	 *
	 * @param mixed $data   Import data.
	 * @param array $rules  XML rules.
	 *
	 * @return mixed
	 */
	protected function addDefaultValue($data, array $rules) {
		if ($rules['type'] & XML_ARRAY) {
			foreach ($rules['rules'] as $tag => $tag_rules) {
				if (array_key_exists($tag, $data)) {
					$data[$tag] = $this->addDefaultValue($data[$tag], $tag_rules);
				}
				else {
					$data[$tag] = array_key_exists('default', $tag_rules)
						? (string) $tag_rules['default']
						: (($tag_rules['type'] & XML_STRING) ? '' : []);
				}
			}
		}
		elseif ($rules['type'] & XML_INDEXED_ARRAY) {
			$prefix = $rules['prefix'];

			foreach ($data as $key => $value) {
				$data[$key] = $this->addDefaultValue($value, $rules['rules'][$prefix]);
			}
		}

		return $data;
	}
}
