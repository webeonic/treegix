<?php


abstract class CParser {

	const PARSE_FAIL = -1;
	const PARSE_SUCCESS = 0;
	const PARSE_SUCCESS_CONT = 1;

	protected $length = 0;
	protected $match = '';

	/**
	 * Try to parse the string starting from the given position.
	 *
	 * @param string	$source		string to parse
	 * @param int 		$pos		position to start from
	 *
	 * @return int
	 */
	abstract public function parse($source, $pos = 0);

	/**
	 * Returns length of the parsed element.
	 *
	 * @return int
	 */
	public function getLength() {
		return $this->length;
	}

	/**
	 * Returns parsed element.
	 *
	 * @return string
	 */
	public function getMatch() {
		return $this->match;
	}
}
