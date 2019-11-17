<?php
 


class CPatternSelect extends CMultiSelect {
	/**
	 * Search method used for auto-suggestions.
	 */
	const SEARCH_METHOD = 'patternselect.get';

	public function __construct(array $options = []) {
		parent::__construct($options);

		// Reset numeric IDs and use names as unique identifiers.
		if (array_key_exists('data', $this->params) && $this->params['data']) {
			foreach ($this->params['data'] as &$item) {
				$item = [
					'name' => $item,
					'id' => $item
				];
			}
			unset($item);
		}

		// Add popup patternselect flag.
		$this->params['popup']['parameters']['patternselect'] = 1;
	}

	public function setEnabled($enabled) {
		$this->params['disabled'] = !$enabled;
		$this->setAttribute('aria-disabled', $enabled ? null : 'true');

		return $this;
	}

	public function toString($destroy = true) {
		$this->setAttribute('data-params', $this->params);

		return parent::toString($destroy);
	}
}
