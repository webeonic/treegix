<?php



class CControllerResponseFatal extends CControllerResponse {

	private $messages = [];

	public function getLocation() {
		return $this->location;
	}

	public function getMessages() {
		return $this->messages;
	}

	public function addMessage($msg) {
		$this->messages[] = $msg;
	}
}
