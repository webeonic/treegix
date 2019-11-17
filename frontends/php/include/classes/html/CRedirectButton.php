<?php



/**
 * A class for creating buttons that redirect you to a different page.
 */
class CRedirectButton extends CSimpleButton {

	/**
	 * @param string $caption
	 * @param string $url			URL to redirect to
	 * @param string $confirmation	confirmation message text
	 * @param string $class
	 */
	public function __construct($caption, $url, $confirmation = null) {
		parent::__construct($caption);

		$this->setUrl($url, $confirmation);
	}

	/**
	 * Set the URL and confirmation message.
	 *
	 * If the confirmation is set, a confirmation pop up will appear before redirecting to the URL.
	 *
	 * @param string $url
	 * @param string|null $confirmation
	 *
	 */
	public function setUrl($url, $confirmation = null) {
		$this->setAttribute('data-url', $url);

		if ($confirmation !== null) {
			$this->setAttribute('data-confirmation', $confirmation);
		}
		return $this;
	}
}
