<?php



/**
 * A class for rendering a widget that can be collapsed or expanded.
 */
class CCollapsibleUiWidget extends CUiWidget {

	/**
	 * Expand/collapse widget.
	 *
	 * Supported values:
	 * - true - expanded;
	 * - false - collapsed.
	 *
	 * @var bool
	 */
	private $expanded = true;

	/**
	 * Sets the header and adds a default expand-collapse icon.
	 *
	 * @param string $caption   Header caption.
	 * @param array  $controls  (optional)
	 * @param string $idx       (optional)
	 *
	 * @return $this
	 */
	public function setHeader($caption, array $controls = [], $idx = '') {
		$icon = (new CRedirectButton(null, null))
			->setId($this->id.'_icon')
			->onClick('changeWidgetState(this, "'.$this->id.'", "'.$idx.'");');

		if ($this->expanded) {
			$icon
				->addClass(TRX_STYLE_BTN_WIDGET_COLLAPSE)
				->setTitle(_('Collapse'));
		}
		else {
			$icon
				->addClass(TRX_STYLE_BTN_WIDGET_EXPAND)
				->setTitle(_('Expand'));
		}

		$controls[] = $icon;

		parent::setHeader($caption, $controls);

		return $this;
	}

	/**
	 * Display the widget in expanded or collapsed state.
	 */
	protected function build() {
		$body = (new CDiv($this->body))
			->addClass('body')
			->setId($this->id);

		if (!$this->expanded) {
			$body->setAttribute('style', 'display: none;');

			if ($this->footer) {
				$this->footer->setAttribute('style', 'display: none;');
			}
		}

		$this->cleanItems();
		$this->addItem($this->header);
		$this->addItem($body);
		$this->addItem($this->footer);

		return $this;
	}

	/**
	 * Sets expanded or collapsed state of the widget.
	 *
	 * @param bool
	 */
	public function setExpanded($expanded) {
		$this->expanded = $expanded;
		return $this;
	}
}
