<?php



class CEventSourceObjectValidator extends CValidator {

	/**
	 * Supported source-object pairs.
	 *
	 * @var array
	 */
	public $pairs = [
		EVENT_SOURCE_TRIGGERS => [
			EVENT_OBJECT_TRIGGER => 1
		],
		EVENT_SOURCE_DISCOVERY => [
			EVENT_OBJECT_DHOST => 1,
			EVENT_OBJECT_DSERVICE => 1
		],
		EVENT_SOURCE_AUTO_REGISTRATION => [
			EVENT_OBJECT_AUTOREGHOST => 1
		],
		EVENT_SOURCE_INTERNAL => [
			EVENT_OBJECT_TRIGGER => 1,
			EVENT_OBJECT_ITEM => 1,
			EVENT_OBJECT_LLDRULE => 1
		]
	];

	/**
	 * Checks if the given source-object pair is valid.
	 *
	 * @param $value
	 *
	 * @return bool
	 */
	public function validate($value)
	{
		$pairs = $this->pairs;

		$objects = $pairs[$value['source']];
		if (!isset($objects[$value['object']])) {
			$supportedObjects = '';
			foreach ($objects as $object => $i) {
				$supportedObjects .= $object.' - '.eventObject($object).', ';
			}

			$this->setError(
				_s('Incorrect event object "%1$s" (%2$s) for event source "%3$s" (%4$s), only the following objects are supported: %5$s.',
					$value['object'],
					eventObject($value['object']),
					$value['source'],
					eventSource($value['source']),
					rtrim($supportedObjects, ', ')
				)
			);
			return false;
		}

		return true;
	}

}
