<?php



/**
 * Class CActionButtonList
 *
 * Implements wrapper to handle output of mass action buttons as used in list views.
 */
class CActionButtonList extends CObject {

	/**
	 * CSubmit instances.
	 *
	 * @var CSubmit[]
	 */
	protected $buttons;

	/**
	 * Name of parameter which will hold values of checked checkboxes.
	 *
	 * @var string
	 */
	protected $checkboxes_name;

	/**
	 * Prefix for sessionStorage used for remembering which checkboxes have been checked when navigating between pages.
	 *
	 * @var string|null
	 */
	protected $name_prefix = null;

	/**
	 * Element that is used to show number of selected checkboxes.
	 *
	 * @var CObject
	 */
	protected $selected_count_element = null;

	/**
	 * @param string       $action_name                 Name of submit buttons.
	 * @param string       $checkboxes_name             Name of paramerer into which checked checkboxes will be put in.
	 * @param array        $buttons_data                Buttons data array.
	 * @param string       $buttons_data[]['name']      Button caption.
	 * @param string       $buttons_data[]['confirm']   Confirmation text (optional).
	 * @param string       $buttons_data[]['redirect']  Redirect URL (optional).
	 * @param string|null  $name_prefix                 Prefix for sessionStorage used for storing currently selected
	 *                                                  checkboxes.
	 */
	function __construct($action_name, $checkboxes_name, array $buttons_data, $name_prefix = null) {
		$this->checkboxes_name = $checkboxes_name;
		$this->name_prefix = $name_prefix ? $name_prefix : null;

		foreach ($buttons_data as $action => $button_data) {
			$button = (new CSubmit($action_name, $button_data['name']))
				->addClass(TRX_STYLE_BTN_ALT)
				->removeAttribute('id');

			if (array_key_exists('redirect', $button_data)) {
				$button
					// Removing parameters not to conflict with the redirecting URL.
					->removeAttribute('name')
					->removeAttribute('value')
					->onClick('var $_form = jQuery(this).closest("form");'.
						// Save the original form action.
						'if (!$_form.data("action")) {'.
							'$_form.data("action", $_form.attr("action"));'.
						'}'.
						'$_form.attr("action", '.CJs::encodeJson($button_data['redirect']).');'
					);
			}
			else {
				$button
					->setAttribute('value', $action)
					->onClick('var $_form = jQuery(this).closest("form");'.
						// Restore the original form action, if previously saved.
						'if ($_form.data("action")) {'.
							'$_form.attr("action", $_form.data("action"));'.
						'}'
					);
			}

			if (array_key_exists('confirm', $button_data)) {
				$button->setAttribute('confirm', $button_data['confirm']);
			}

			$this->buttons[$action] = $button;
		}
	}

	/**
	 * Returns current element for showing how many checkboxes are selected. If currently no
	 * element exists, constructs and returns default one.
	 *
	 * @return CObject
	 */
	public function getSelectedCountElement() {
		if (!$this->selected_count_element) {
			$this->selected_count_element = (new CSpan('0 '._('selected')))
				->setId('selected_count')
				->addClass(TRX_STYLE_SELECTED_ITEM_COUNT);
		}

		return $this->selected_count_element;
	}

	/**
	 * Gets string representation of action button list.
	 *
	 * @param bool $destroy
	 *
	 * @return string
	 */
	public function toString($destroy = true) {
		trx_add_post_js('chkbxRange.pageGoName = '.CJs::encodeJson($this->checkboxes_name).';');
		trx_add_post_js('chkbxRange.prefix = '.CJs::encodeJson($this->name_prefix).';');

		$this->items[] = (new CDiv([$this->getSelectedCountElement(), $this->buttons]))
			->setId('action_buttons')
			->addClass(TRX_STYLE_ACTION_BUTTONS);

		return parent::toString($destroy);
	}
}
