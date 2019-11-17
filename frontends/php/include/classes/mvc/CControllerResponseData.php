<?php



class CControllerResponseData extends CControllerResponse {

	private $data;
	private $title = null;
	private $file_name = null;

	/**
	 * @var bool $view_enabled  true - send view and layout; false - send layout only.
	 */
	private $view_enabled = true;

	public function __construct($data) {
		$this->data = $data;
	}

	public function getData() {
		return $this->data;
	}

	public function setTitle($title) {
		$this->title = $title;
	}

	public function getTitle() {
		return $this->title;
	}

	public function setFileName($file_name) {
		$this->file_name = $file_name;
	}

	public function getFileName() {
		return $this->file_name;
	}

	/**
	 * Prohibits sending view.
	 */
	public function disableView() {
		$this->view_enabled = false;

		return $this;
	}

	/**
	 * Returns current value of view_enabled variable.
	 *
	 * @return bool
	 */
	public function isViewEnabled() {
		return $this->view_enabled;
	}
}
