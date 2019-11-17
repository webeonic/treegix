<?php


/**
 * A parser for IPv4 and IPv6 addresses
 */
class CIPParser extends CParser {

	/**
	 * @var CIPv4Parser
	 */
	private $ipv4_parser;

	/**
	 * @var CIPv6Parser
	 */
	private $ipv6_parser;

	/**
	 * Supported options:
	 *   'v6' => true		enabled support of IPv6 addresses
	 *
	 * @var array
	 */
	private $options = ['v6' => true];

	/**
	 * @param array $options
	 */
	public function __construct(array $options) {
		if (array_key_exists('v6', $options)) {
			$this->options['v6'] = $options['v6'];
		}

		$this->ipv4_parser = new CIPv4Parser();
		$this->ipv6_parser = new CIPv6Parser();
	}

	/**
	 * @param string $source
	 * @param int    $pos
	 *
	 * @return bool
	 */
	public function parse($source, $pos = 0) {
		$this->length = 0;
		$this->match = '';

		if ($this->ipv4_parser->parse($source, $pos) != self::PARSE_FAIL) {
			$this->length = $this->ipv4_parser->getLength();
			$this->match = $this->ipv4_parser->getMatch();
		}
		elseif ($this->options['v6'] && $this->ipv6_parser->parse($source, $pos) != self::PARSE_FAIL) {
			$this->length = $this->ipv6_parser->getLength();
			$this->match = $this->ipv6_parser->getMatch();
		}
		else {
			return self::PARSE_FAIL;
		}

		return (isset($source[$pos + $this->length]) ? self::PARSE_SUCCESS_CONT : self::PARSE_SUCCESS);
	}
}
