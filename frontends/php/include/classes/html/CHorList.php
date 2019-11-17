<?php
 


class CHorList extends CList {

	/**
	 * Creates a UL horizontal list with spaces between elements.
	 *
	 * @param array $values			an array of items to add to the list
	 */
	public function __construct(array $values = []) {
		parent::__construct($values);

		$this->addClass(TRX_STYLE_HOR_LIST);
	}

}
