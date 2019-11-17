<?php


/**
 * A helper class for generating HTML snippets.
 */
class CViewHelper {

	/**
	 * Generates </a>&nbsp;<sup>num</sup>" to be used in tables. Null is returned if equal to zero.
	 *
	 * @static
	 *
	 * @param integer $num
	 *
	 * @return mixed
	 */
	public static function showNum($num) {
		if ($num == 0) {
			return null;
		}

		return [SPACE, new CSup($num)];
	}
}
