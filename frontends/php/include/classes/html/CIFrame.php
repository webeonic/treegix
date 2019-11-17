<?php



class CIFrame extends CTag {

	public function __construct($src = null, $width = '100%', $height = '100%', $scrolling = 'no', $id = 'iframe') {
		parent::__construct('iframe', true);

		$this->setSrc($src);
		$this->setWidth($width);
		$this->setHeight($height);
		$this->setScrolling($scrolling);
		$this->setId($id);
	}

	public function setSrc($value = null) {
		if (is_null($value)) {
			$this->removeAttribute('src');
		}
		else {
			$this->setAttribute('src', $value);
		}
		return $this;
	}

	public function setWidth($value) {
		if (is_null($value)) {
			$this->removeAttribute('width');
		}
		else {
			$this->setAttribute('width', $value);
		}
		return $this;
	}

	public function setHeight($value) {
		if (is_null($value)) {
			$this->removeAttribute('height');
		}
		else {
			$this->setAttribute('height', $value);
		}
		return $this;
	}

	public function setScrolling($value) {
		if (is_null($value)) {
			$value = 'no';
		}

		$this->setAttribute('scrolling', $value);
		return $this;
	}
}
