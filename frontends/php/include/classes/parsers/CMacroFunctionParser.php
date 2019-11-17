<?php


/**
 *  A parser for function macros like "{{ITEM.VALUE}.func()}".
 */
class CMacroFunctionParser extends CParser {

	/**
	 * Parser for generic macros.
	 *
	 * @var CMacroParser
	 */
	private $macro_parser;

	/**
	 * Parser for trigger functions.
	 *
	 * @var CFunctionParser
	 */
	private $function_parser;

	/**
	 * @param array $macros   The list of macros, for example ['{ITEM.VALUE}', '{ITEM.LASTVALUE}'].
	 * @param array $options
	 */
	public function __construct(array $macros, array $options = []) {
		$this->macro_parser = new CMacroParser($macros, $options);
		$this->function_parser = new CFunctionParser();
	}

	/**
	 * @param string $source
	 * @param int    $pos
	 *
	 * @return int
	 */
	public function parse($source, $pos = 0) {
		$this->length = 0;
		$this->match = '';

		$p = $pos;

		if (!isset($source[$p]) || $source[$p] !== '{') {
			return self::PARSE_FAIL;
		}
		$p++;

		if ($this->macro_parser->parse($source, $p) == CParser::PARSE_FAIL) {
			return self::PARSE_FAIL;
		}
		$p += $this->macro_parser->getLength();

		if (!isset($source[$p]) || $source[$p] !== '.') {
			return self::PARSE_FAIL;
		}
		$p++;

		if ($this->function_parser->parse($source, $p) == CParser::PARSE_FAIL) {
			return self::PARSE_FAIL;
		}
		$p += $this->function_parser->getLength();

		if (!isset($source[$p]) || $source[$p] !== '}') {
			return self::PARSE_FAIL;
		}
		$p++;

		$this->length = $p - $pos;
		$this->match = substr($source, $pos, $this->length);

		return (isset($source[$pos + $this->length]) ? self::PARSE_SUCCESS_CONT : self::PARSE_SUCCESS);
	}

	/**
	 * Returns macro parser.
	 *
	 * @return string
	 */
	public function getMacroParser() {
		return $this->macro_parser;
	}

	/**
	 * Returns function parser.
	 *
	 * @return string
	 */
	public function getFunctionParser() {
		return $this->function_parser;
	}
}
