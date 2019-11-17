<?php


/**
 * A parser for ranges separated by a comma.
 */
class CRangesParser extends CParser {

	/**
	 * Status code range parser.
	 *
	 * @var CRangeParser
	 */
	private $range_parser;

	/**
	 * Array of status code ranges.
	 *
	 * @var array
	 */
	private $ranges = [];

	/**
	 * Options to initialize other parsers.
	 *
	 * @var array
	 */
	private $options = [
		'usermacros' => false,
		'lldmacros' => false
	];

	/**
	 * @param array $options   An array of options to initialize other parsers.
	 */
	public function __construct($options = []) {
		if (array_key_exists('usermacros', $options)) {
			$this->options['usermacros'] = $options['usermacros'];
		}
		if (array_key_exists('lldmacros', $options)) {
			$this->options['lldmacros'] = $options['lldmacros'];
		}

		$this->range_parser = new CRangeParser([
			'usermacros' => $this->options['usermacros'],
			'lldmacros' => $this->options['lldmacros']
		]);
	}

	/**
	 * Parse the given status codes separated by a comma.
	 *
	 * @param string $source  Source string that needs to be parsed.
	 * @param int    $pos     Position offset.
	 */
	public function parse($source, $pos = 0) {
		$this->length = 0;
		$this->match = '';
		$this->ranges = [];

		$p = $pos;

		while (true) {
			if ($this->range_parser->parse($source, $p) == self::PARSE_FAIL) {
				break;
			}

			$p += $this->range_parser->getLength();
			$this->ranges[] = $this->range_parser->getRange();

			if (!isset($source[$p]) || $source[$p] !== ',') {
				break;
			}

			$p++;
		}

		if ($p == $pos) {
			return self::PARSE_FAIL;
		}

		if ($source[$p - 1] === ',') {
			$p--;
		}

		$this->length = $p - $pos;
		$this->match = substr($source, $pos, $this->length);

		return isset($source[$p]) ? self::PARSE_SUCCESS_CONT : self::PARSE_SUCCESS;
	}

	/**
	 * Retrieve the status code ranges.
	 *
	 * @return array
	 */
	public function getRanges() {
		return $this->ranges;
	}
}
