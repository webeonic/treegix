<?php



class CControllerResponseRedirect extends CControllerResponse {

	private $location;
	private $messageOk = null;
	private $messageError = null;
	private $formData = null;

	public function __construct($location) {
		$this->location = $location;
	}

	public function getLocation() {
		return $this->location;
	}

	public function getFormData() {
		return $this->formData;
	}

	public function setFormData($formData) {
		$this->formData = $formData;
	}

	public function setMessageOk($messageOk) {
		$this->messageOk = $messageOk;
	}

	public function getMessageOk() {
		return $this->messageOk;
	}

	public function setMessageError($messageError) {
		$this->messageError = $messageError;
	}

	public function getMessageError() {
		return $this->messageError;
	}
}
