<?php



require_once dirname(__FILE__).'/../../blocks.inc.php';

class CScreenServerInfo extends CScreenBase {

	/**
	 * Process screen.
	 *
	 * @return CDiv (screen inside container)
	 */
	public function get() {
		$footer = (new CList())
			->addItem(_s('Updated: %s', trx_date2str(TIME_FORMAT_SECONDS)))
			->addClass(TRX_STYLE_DASHBRD_WIDGET_FOOT);

		return $this->getOutput(
			(new CUiWidget(uniqid(), [make_status_of_trx(), $footer]))
				->setHeader(_('System information'))
		);
	}
}
