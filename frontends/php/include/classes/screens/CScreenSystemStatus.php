<?php



require_once dirname(__FILE__).'/../../blocks.inc.php';

class CScreenSystemStatus extends CScreenBase {

	/**
	 * Process screen.
	 *
	 * @return CDiv (screen inside container)
	 */
	public function get() {
		global $page;

		// rewrite page file
		$page['file'] = $this->pageFile;

		$config = select_config();
		$severity_config = [
			'severity_name_0' => $config['severity_name_0'],
			'severity_name_1' => $config['severity_name_1'],
			'severity_name_2' => $config['severity_name_2'],
			'severity_name_3' => $config['severity_name_3'],
			'severity_name_4' => $config['severity_name_4'],
			'severity_name_5' => $config['severity_name_5']
		];
		$data = getSystemStatusData([]);
		$table = makeSystemStatus([], $data, $severity_config, $this->pageFile.'?screenid='.$this->screenid);

		$footer = (new CList())
			->addItem(_s('Updated: %s', zbx_date2str(TIME_FORMAT_SECONDS)))
			->addClass(ZBX_STYLE_DASHBRD_WIDGET_FOOT);

		return $this->getOutput(
			(new CUiWidget('hat_syssum', [$table, $footer]))->setHeader(_('Problems by severity'))
		);
	}
}
