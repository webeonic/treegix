<?php



class CArea extends CTag {

	public function __construct($coords, $href, $alt, $shape) {
		parent::__construct('area');
		$this->setCoords($coords);
		$this->setShape($shape);
		$this->setHref($href);
		$this->setAlt($alt);
	}

	public function setCoords($value) {
		if (!is_array($value)) {
			return $this->error('Incorrect value for setCoords "'.$value.'".');
		}
		if (count($value) < 3) {
			return $this->error('Incorrect values count for setCoords "'.count($value).'".');
		}

		$str_val = '';
		foreach ($value as $val) {
			if (!is_numeric($val)) {
				return $this->error('Incorrect value for setCoords "'.$val.'".');
			}
			$str_val .= $val.',';
		}
		$this->setAttribute('coords', trim($str_val, ','));
		return $this;
	}

	public function setShape($value) {
		$this->setAttribute('shape', $value);
		return $this;
	}

	public function setHref($value) {
		$this->setAttribute('href', $value);
		return $this;
	}

	public function setAlt($value) {
		$this->setAttribute('alt', $value);
		return $this;
	}
}
