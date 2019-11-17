<?php



class CWidgetFieldSelectResource extends CWidgetField {

	protected $srctbl;
	protected $srcfld1;
	protected $srcfld2;
	protected $dstfld1;
	protected $dstfld2;
	protected $resource_type;

	/**
	 * Select resource type widget field. Will create text box field with select button,
	 * that will allow to select specified resource.
	 *
	 * @param string $name           field name in form
	 * @param string $label          label for the field in form
	 * @param int    $resource_type  WIDGET_FIELD_SELECT_RES_ constant.
	 */
	public function __construct($name, $label, $resource_type) {
		parent::__construct($name, $label);

		$this->resource_type = $resource_type;

		switch ($resource_type) {
			case WIDGET_FIELD_SELECT_RES_SYSMAP:
				$this->setSaveType(ZBX_WIDGET_FIELD_TYPE_MAP);
				$this->srctbl = 'sysmaps';
				$this->srcfld1 = 'sysmapid';
				$this->srcfld2 = 'name';
				break;
		}

		$this->dstfld1 = $name;
		$this->dstfld2 = $this->name.'_caption';
		$this->setDefault(0);
	}

	/**
	 * Set additional flags, which can be used in configuration form.
	 *
	 * @param int $flags
	 *
	 * @return $this
	 */
	public function setFlags($flags) {
		parent::setFlags($flags);

		if ($flags & self::FLAG_NOT_EMPTY) {
			$strict_validation_rules = $this->getValidationRules();
			self::setValidationRuleFlag($strict_validation_rules, API_NOT_EMPTY);
			$this->setStrictValidationRules($strict_validation_rules);
		}
		else {
			$this->setStrictValidationRules(null);
		}

		return $this;
	}

	public function getResourceType() {
		return $this->resource_type;
	}

	public function getPopupOptions($dstfrm) {
		$popup_options = [
			'srctbl' => $this->srctbl,
			'srcfld1' => $this->srcfld1,
			'srcfld2' => $this->srcfld2,
			'dstfld1' => $this->dstfld1,
			'dstfld2' => $this->dstfld2,
			'dstfrm' => $dstfrm
		];

		return $popup_options;
	}
}
