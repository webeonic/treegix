<?php
 


abstract class CControllerPopupPreprocTest extends CController {
	/**
	 * Types of preprocessing tests, depending on type of item.
	 */
	const TRX_TEST_TYPE_ITEM = 0;
	const TRX_TEST_TYPE_ITEM_PROTOTYPE = 1;
	const TRX_TEST_TYPE_LLD = 2;

	/**
	 * Item value type used if user has not specified one.
	 */
	const TRX_DEFAULT_VALUE_TYPE = ITEM_VALUE_TYPE_TEXT;

	/**
	 * @var object
	 */
	protected $preproc_item;

	/**
	 * @var array
	 */
	protected static $preproc_steps_using_prev_value = [TRX_PREPROC_DELTA_VALUE, TRX_PREPROC_DELTA_SPEED,
		TRX_PREPROC_THROTTLE_VALUE, TRX_PREPROC_THROTTLE_TIMED_VALUE
	];

	protected function checkPermissions() {
		return ($this->getUserType() >= USER_TYPE_TREEGIX_ADMIN);
	}

	protected function getPreprocessingItemType($test_type) {
		switch ($test_type) {
			case self::TRX_TEST_TYPE_ITEM:
				return new CItem;

			case self::TRX_TEST_TYPE_ITEM_PROTOTYPE:
				return new CItemPrototype;

			case self::TRX_TEST_TYPE_LLD:
				return new CDiscoveryRule;
		}
	}
}
