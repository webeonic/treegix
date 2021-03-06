<?php


/**
 * Class for regular expressions and Treegix global expressions.
 * Any string that begins with '@' is treated as Treegix expression.
 * Data from Treegix expressions is taken from DB, and cached in static variable.
 *
 * @throws Exception
 */
class CGlobalRegexp {

	const ERROR_REGEXP_NOT_EXISTS = 1;

	/**
	 * Determine if it's Treegix expression.
	 *
	 * @var bool
	 */
	protected $isTreegixRegexp;

	/**
	 * If we create simple regular expression this contains itself as a string,
	 * if we create Treegix expression this contains array of expressions taken from DB.
	 *
	 * @var array|string
	 */
	protected $expression;

	/**
	 * Cache for Treegix expressions.
	 *
	 * @var array
	 */
	private static $_cachedExpressions = [];

	/**
	 * Initialize expression, gets data from db for Treegix expressions.
	 *
	 * @param string $regExp
	 *
	 * @throws Exception
	 */
	public function __construct($regExp) {
		if ($regExp[0] == '@') {
			$this->isTreegixRegexp = true;
			$regExp = substr($regExp, 1);

			if (!isset(self::$_cachedExpressions[$regExp])) {
				self::$_cachedExpressions[$regExp] = [];

				$dbRegExps = DBselect(
					'SELECT e.regexpid,e.expression,e.expression_type,e.exp_delimiter,e.case_sensitive'.
					' FROM expressions e,regexps r'.
					' WHERE e.regexpid=r.regexpid'.
						' AND r.name='.trx_dbstr($regExp)
				);
				while ($expression = DBfetch($dbRegExps)) {
					self::$_cachedExpressions[$regExp][] = $expression;
				}

				if (empty(self::$_cachedExpressions[$regExp])) {
					unset(self::$_cachedExpressions[$regExp]);
					throw new Exception('Does not exist', self::ERROR_REGEXP_NOT_EXISTS);
				}
			}
			$this->expression = self::$_cachedExpressions[$regExp];
		}
		else {
			$this->isTreegixRegexp = false;
			$this->expression = $regExp;
		}
	}

	/**
	 * @param string $string
	 *
	 * @return bool
	 */
	public function match($string) {
		if ($this->isTreegixRegexp) {
			$result = true;

			foreach ($this->expression as $expression) {
				$result = self::matchExpression($expression, $string);

				if (!$result) {
					break;
				}
			}
		}
		else {
			$result = (bool) @preg_match('/'.str_replace('/', '\/', $this->expression).'/', $string);
		}

		return $result;
	}

	/**
	 * Matches expression against test string, respecting expression type.
	 *
	 * @static
	 *
	 * @param array $expression
	 * @param $string
	 *
	 * @return bool
	 */
	public static function matchExpression(array $expression, $string) {
		if ($expression['expression_type'] == EXPRESSION_TYPE_TRUE || $expression['expression_type'] == EXPRESSION_TYPE_FALSE) {
			$result = self::_matchRegular($expression, $string);
		}
		else {
			$result = self::_matchString($expression, $string);
		}

		return $result;
	}

	/**
	 * Matches expression as regular expression.
	 *
	 * @static
	 *
	 * @param array $expression
	 * @param string $string
	 *
	 * @return bool
	 */
	private static function _matchRegular(array $expression, $string) {
		$pattern = self::buildRegularExpression($expression);

		$expectedResult = ($expression['expression_type'] == EXPRESSION_TYPE_TRUE);

		return preg_match($pattern, $string) == $expectedResult;
	}

	/**
	 * Combines regular expression provided as definition array into a string.
	 *
	 * @static
	 *
	 * @param array $expression
	 *
	 * @return string
	 */
	private static function buildRegularExpression(array $expression) {
		$expression['expression'] = str_replace('/', '\/', $expression['expression']);

		$pattern = '/'.$expression['expression'].'/m';
		if (!$expression['case_sensitive']) {
			$pattern .= 'i';
		}

		return $pattern;
	}

	/**
	 * Matches expression as string.
	 *
	 * @static
	 *
	 * @param array $expression
	 * @param string $string
	 *
	 * @return bool
	 */
	private static function _matchString(array $expression, $string) {
		$result = true;

		if ($expression['expression_type'] == EXPRESSION_TYPE_ANY_INCLUDED) {
			$patterns = array_filter(explode($expression['exp_delimiter'], $expression['expression']), 'strlen');
		}
		else {
			$patterns = [$expression['expression']];
		}

		$expectedResult = ($expression['expression_type'] != EXPRESSION_TYPE_NOT_INCLUDED);

		if (!$expression['case_sensitive']) {
			$string = mb_strtolower($string);
		}

		foreach ($patterns as $pattern) {
			if (!$expression['case_sensitive']) {
				$pattern = mb_strtolower($pattern);
			}

			$pos = mb_strpos($string, $pattern);
			$tmp = (($pos !== false) == $expectedResult);

			if ($expression['expression_type'] == EXPRESSION_TYPE_ANY_INCLUDED && $tmp) {
				return true;
			}
			else {
				$result = ($result && $tmp);
			}
		}

		return $result;
	}
}
