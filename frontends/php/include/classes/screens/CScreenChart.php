<?php



class CScreenChart extends CScreenBase {

	/**
	 * Graph id
	 *
	 * @var int
	 */
	public $graphid;

	/**
	 * Init screen data.
	 *
	 * @param array		$options
	 * @param int		$options['graphid']
	 */
	public function __construct(array $options = []) {
		parent::__construct($options);

		$this->graphid = isset($options['graphid']) ? $options['graphid'] : null;
		$this->profileIdx2 = $this->graphid;
	}

	/**
	 * Process screen.
	 *
	 * @return CDiv (screen inside container)
	 */
	public function get() {
		$this->dataId = 'graph_full';
		$containerId = 'graph_container';

		// time control
		$graphDims = getGraphDims($this->graphid);
		if ($graphDims['graphtype'] == GRAPH_TYPE_PIE || $graphDims['graphtype'] == GRAPH_TYPE_EXPLODED) {
			$loadSBox = 0;
			$src = new CUrl('chart6.php');
		}
		else {
			$loadSBox = 1;
			$src = new CUrl('chart2.php');
		}
		$src
			->setArgument('graphid', $this->graphid)
			->setArgument('from', $this->timeline['from'])
			->setArgument('to', $this->timeline['to'])
			->setArgument('profileIdx', $this->profileIdx)
			->setArgument('profileIdx2', $this->profileIdx2);

		$timeControlData = [
			'id' => $this->getDataId(),
			'containerid' => $containerId,
			'src' => $src->getUrl(),
			'objDims' => $graphDims,
			'loadSBox' => $loadSBox,
			'loadImage' => 1,
			'dynamic' => 1
		];

		// output
		if ($this->mode == SCREEN_MODE_JS) {
			$timeControlData['dynamic'] = 0;
			$timeControlData['loadSBox'] = 0;

			return 'timeControl.addObject("'.$this->getDataId().'", '.zbx_jsvalue($this->timeline).', '.zbx_jsvalue($timeControlData).')';
		}
		else {
			if ($this->mode == SCREEN_MODE_SLIDESHOW) {
				insert_js('timeControl.addObject("'.$this->getDataId().'", '.zbx_jsvalue($this->timeline).', '.zbx_jsvalue($timeControlData).');');
			}
			else {
				zbx_add_post_js('timeControl.addObject("'.$this->getDataId().'", '.zbx_jsvalue($this->timeline).', '.zbx_jsvalue($timeControlData).');');
			}

			return $this->getOutput(
				(new CDiv())
					->addClass('center')
					->setId($containerId),
				true,
				['graphid' => $this->graphid]
			);
		}
	}
}
