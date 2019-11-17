<?php



class CSetParser extends CParser {

	/**
	 * Array of string to search for with strings as keys.
	 *
	 * @var array
	 */
	private $needles = [];

	/**
	 * Array of chars that are used in the given strings with chars as keys.
	 *
	 * @var array
	 */
	private $chars = [];

	/**
	 * Array of strings to search for.
	 *
	 * @param array $needles
	 */
	public function __construct(array $needles) {
		$this->needles = array_flip($needles);
		$this->chars = array_flip(str_split(implode($needles)));
	}

	/**
	 * Find one of the given strings at the given position.
	 *
	 * The parser implements a greedy algorithm, i.e., looks for the longest match.
	 */
	public function parse($source, $pos = 0) {
		$this->length = 0;
		$this->match = '';

		$length = 0;
		$match = null;

		$token = '';
		for ($p = $pos; isset($source[$p]) && isset($this->chars[$source[$p]]); $p++) {
			$token .= $source[$p];

			// when we found a match, keep looking to see of there may be a longer match
			if (isset($this->needles[$token])) {
				$length = $p - $pos + 1;
				$match = $token;
			}
		}

		if ($match === null) {
			return self::PARSE_FAIL;
		}

		$this->length = $length;
		$this->match = $match;

		return (isset($source[$pos + $this->length]) ? self::PARSE_SUCCESS_CONT : self::PARSE_SUCCESS);
	}
}
