<?php



class CUrl {

	private $url;
	protected $reference;
	protected $query;
	protected $arguments = [];

	/**
	 * WARNING: the class doesn't support parsing query strings with multi-dimentional arrays.
	 *
	 * @param string|null $url
	 */
	public function __construct($url = null) {
		if (empty($url)) {
			$this->formatGetArguments();

			$this->url = basename($_SERVER['SCRIPT_NAME']);
		}
		else {
			$this->url = $url;

			// parse reference
			$pos = strpos($url, '#');
			if ($pos !== false) {
				$this->reference = substr($url, $pos + 1);
				$this->url = substr($url, 0, $pos);
			}

			// parse query
			$pos = strpos($url, '?');
			if ($pos !== false) {
				$this->query = substr($url, $pos + 1);
				$this->url = substr($url, 0, $pos);
			}

			$this->formatArguments();
		}
	}

	/**
	 * Creates a HTTP query string from the arguments set in self::$arguments and saves it in self::$query.
	 */
	public function formatQuery() {
		$this->query = http_build_query($this->arguments);
	}

	public function formatGetArguments() {
		$this->arguments = $_GET;

		$this->formatQuery();

		return $this;
	}

	public function formatArguments($query = null) {
		if ($query === null) {
			$query = $this->query;
		}
		if ($query !== null) {
			$args = explode('&', $query);
			foreach ($args as $id => $arg) {
				if (empty($arg)) {
					continue;
				}

				if (strpos($arg, '=') !== false) {
					list($name, $value) = explode('=', $arg);
					$this->arguments[urldecode($name)] = urldecode($value);
				}
				else {
					$this->arguments[$arg] = '';
				}
			}
		}
		$this->formatQuery();
	}

	/**
	 * Return relative url.
	 *
	 * @return string
	 */
	public function getUrl() {
		$this->formatQuery();

		$url = $this->url;
		$url .= $this->query ? '?'.$this->query : '';
		$url .= $this->reference ? '#'.urlencode($this->reference) : '';

		return $url;
	}

	public function removeArgument($key) {
		unset($this->arguments[$key]);

		return $this;
	}

	public function setArgument($key, $value = '') {
		$this->arguments[$key] = $value;

		return $this;
	}

	public function setArgumentSID() {
		$this->arguments['sid'] = substr($_COOKIE[TRX_SESSION_NAME], 16, 16);

		return $this;
	}

	public function toString() {
		return $this->getUrl();
	}
}
