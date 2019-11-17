<?php


/**
 * A parser for time used in the time selector.
 */
class CRangeTimeParser extends CParser {

	const TRX_TIME_UNKNOWN = 0;
	const TRX_TIME_ABSOLUTE = 1;
	const TRX_TIME_RELATIVE = 2;

	/**
	 * @var int $time_type
	 */
	private $time_type;

	/**
	 * @var CRelativeTimeParser
	 */
	private $relative_time_parser;

	/**
	 * @var CAbsoluteTimeParser
	 */
	private $absolute_time_parser;

	public function __construct() {
		$this->relative_time_parser = new CRelativeTimeParser();
		$this->absolute_time_parser = new CAbsoluteTimeParser();
	}

	/**
	 * Parse the given period.
	 *
	 * @param string $source  Source string that needs to be parsed.
	 * @param int    $pos     Position offset.
	 */
	public function parse($source, $pos = 0) {
		$this->length = 0;
		$this->match = '';
		$this->time_type = self::TRX_TIME_UNKNOWN;

		$p = $pos;

		if ($this->relative_time_parser->parse($source, $p) != self::PARSE_FAIL) {
			$p += $this->relative_time_parser->getLength();
			$this->time_type = self::TRX_TIME_RELATIVE;
		}
		elseif ($this->absolute_time_parser->parse($source, $p) != self::PARSE_FAIL) {
			$p += $this->absolute_time_parser->getLength();
			$this->time_type = self::TRX_TIME_ABSOLUTE;
		}
		else {
			return self::PARSE_FAIL;
		}

		$this->length = $p - $pos;
		$this->match = substr($source, $pos, $this->length);

		return isset($source[$p]) ? self::PARSE_SUCCESS_CONT : self::PARSE_SUCCESS;
	}

	public function getTimeType() {
		return $this->time_type;
	}

	/**
	 * Timestamp is returned as initialized DateTime object. Returns null when timestamp is not valid.
	 *
	 * @param bool   $is_start  If set to true date will be modified to lowest value, example (now/w) will be returned
	 *                          as Monday of this week. When set to false precisiion will modify date to highest value,
	 *                          same example will return Sunday of this week.
	 *
	 * @return DateTime|null
	 */
	public function getDateTime($is_start) {
		switch ($this->time_type) {
			case self::TRX_TIME_ABSOLUTE:
				return $this->absolute_time_parser->getDateTime($is_start);

			case self::TRX_TIME_RELATIVE:
				return $this->relative_time_parser->getDateTime($is_start);

			default:
				return null;
		}
	}
}
