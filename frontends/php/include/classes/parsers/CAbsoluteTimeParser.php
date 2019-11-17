<?php



/**
 * A parser for absolute time in "YYYY[-MM[-DD]][ hh[:mm[:ss]]]" format.
 */
class CAbsoluteTimeParser extends CParser {

	/**
	 * Full date in "YYYY-MM-DD hh:mm:ss" format.
	 *
	 * @var string $date
	 */
	private $date;

	/**
	 * @var array $tokens
	 */
	private $tokens;

	/**
	 * Parse the given period.
	 *
	 * @param string $source  Source string that needs to be parsed.
	 * @param int    $pos     Position offset.
	 */
	public function parse($source, $pos = 0) {
		$this->tokens = [];
		$this->length = 0;
		$this->match = '';
		$this->date = '';

		$p = $pos;

		if (!$this->parseAbsoluteTime($source, $p)) {
			return self::PARSE_FAIL;
		}

		$this->length = $p - $pos;
		$this->match = substr($source, $pos, $this->length);

		return isset($source[$p]) ? self::PARSE_SUCCESS_CONT : self::PARSE_SUCCESS;
	}

	/**
	 * Parse absolute time.
	 *
	 * @param string	$source
	 * @param int		$pos
	 *
	 * @return bool
	 */
	private function parseAbsoluteTime($source, &$pos) {
		$pattern_Y = '(?P<Y>[12][0-9]{3})';
		$pattern_m = '(?P<m>[0-9]{1,2})';
		$pattern_d = '(?P<d>[0-9]{1,2})';
		$pattern_H = '(?P<H>[0-9]{1,2})';
		$pattern_i = '(?P<i>[0-9]{1,2})';
		$pattern_s = '(?P<s>[0-9]{1,2})';
		$pattern = $pattern_Y.'(-'.$pattern_m.'(-'.$pattern_d.'( +'.$pattern_H.'(:'.$pattern_i.'(:'.$pattern_s.')?)?)?)?)?';

		if (!preg_match('/^'.$pattern.'/', substr($source, $pos), $matches)) {
			return false;
		}

		foreach (['Y', 'm', 'd', 'H', 'i', 's'] as $key) {
			if (array_key_exists($key, $matches)) {
				$this->tokens[$key] = $matches[$key];
			}
		}

		$matches += ['m' => 1, 'd' => 1, 'H' => 0, 'i' => 0, 's' => 0];

		$date = sprintf('%04d-%02d-%02d %02d:%02d:%02d', $matches['Y'], $matches['m'], $matches['d'], $matches['H'],
			$matches['i'], $matches['s']
		);

		$datetime = date_create($date);

		if ($datetime === false || $datetime->getLastErrors()['warning_count'] != 0) {
			return false;
		}

		$this->date = $date;
		$pos += strlen($matches[0]);

		return true;
	}

	/**
	 * Returns date in "YYYY-MM-DD hh:mm:ss" format.
	 *
	 * @param bool   $is_start  If set to true date will be modified to lowest value, example "2018" will be returned
	 *                          as "2018-01-01 00:00:00", otherwise "2018-12-31 23:59:59".
	 *
	 * @return DateTime|null
	 */
	public function getDateTime($is_start) {
		if ($this->date === '') {
			return null;
		}

		$date = new DateTime($this->date);

		if ($is_start) {
			return $date;
		}

		if (!array_key_exists('m', $this->tokens)) {
			return $date->modify('last day of December this year 23:59:59');
		}

		if (!array_key_exists('d', $this->tokens)) {
			return $date->modify('last day of this month 23:59:59');
		}

		if (!array_key_exists('H', $this->tokens)) {
			return $date->modify('23:59:59');
		}

		if (!array_key_exists('i', $this->tokens)) {
			return new DateTime($date->format('Y-m-d H:59:59'));
		}

		if (!array_key_exists('s', $this->tokens)) {
			return new DateTime($date->format('Y-m-d H:i:59'));
		}

		return $date;
	}
}
