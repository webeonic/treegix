<?php



/**
 * A parser for host group names and host prototype group names.
 */
class CHostGroupNameParser extends CParser {

	private $options = [
		'lldmacros' => false
	];

	/**
	 * Array of macros found in string.
	 *
	 * @var array
	 */
	private $macros = [];

	/**
	 * LLD macro parser.
	 *
	 * @var CLLDMacroParser
	 */
	private $lld_macro_parser;

	/**
	 * LLD macro function parser.
	 *
	 * @var CLLDMacroFunctionParser
	 */
	private $lld_macro_function_parser;

	public function __construct($options = []) {
		if (array_key_exists('lldmacros', $options)) {
			$this->options['lldmacros'] = $options['lldmacros'];
		}

		if ($this->options['lldmacros']) {
			$this->lld_macro_parser = new CLLDMacroParser();
			$this->lld_macro_function_parser = new CLLDMacroFunctionParser();
		}
	}

	/**
	 * Parse the host group name or host prototype group name.
	 *
	 * @param string $source  Source string that needs to be parsed.
	 * @param int    $pos     Position offset.
	 *
	 * @return int
	 */
	public function parse($source, $pos = 0) {
		$this->length = 0;
		$this->match = '';
		$this->macros = [];

		$p = $pos;
		$is_slash = true;

		while (isset($source[$p])) {
			if ($this->options['lldmacros'] && $this->parseLLDMacro($source, $p)) {
				$is_slash = false;

				continue;
			}

			if ($is_slash && $source[$p] === '/') {
				break;
			}

			$is_slash = ($source[$p] === '/');
			$p++;
		}

		// Check last character. Cannot be /.
		if ($pos != $p && $is_slash) {
			$p--;
		}

		if ($pos == $p) {
			return self::PARSE_FAIL;
		}

		$this->length = $p - $pos;
		$this->match = substr($source, $pos, $this->length);

		return isset($source[$p]) ? self::PARSE_SUCCESS_CONT : self::PARSE_SUCCESS;
	}

	/**
	 * Retrieve macros found in source string.
	 *
	 * @return array
	 */
	public function getMacros() {
		return $this->macros;
	}

	/**
	 * Parse LLD macro or any character.
	 *
	 * @param string $source  Source string that needs to be parsed.
	 * @param int    $pos     Position offset.
	 *
	 * @return bool
	 */
	private function parseLLDMacro($source, &$pos) {
		if ($this->lld_macro_parser->parse($source, $pos) != self::PARSE_FAIL) {
			$pos += $this->lld_macro_parser->getLength();
			$this->macros[] = $this->lld_macro_parser->getMatch();

			return true;
		}

		if ($this->lld_macro_function_parser->parse($source, $pos) != self::PARSE_FAIL) {
			$pos += $this->lld_macro_function_parser->getLength();
			$this->macros[] = $this->lld_macro_function_parser->getMatch();

			return true;
		}

		return false;
	}
}
