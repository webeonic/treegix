<?php



class CScreenTriggersInfo extends CScreenBase {

	/**
	 * Process screen.
	 *
	 * @return CDiv (screen inside container)
	 */
	public function get() {
		$header = (new CDiv([
			new CTag('h4', true, _('Trigger info'))
		]))->addClass(ZBX_STYLE_DASHBRD_WIDGET_HEAD);

		if ($this->screenitem['resourceid'] != 0) {
			$groups = API::HostGroup()->get([
				'output' => ['name'],
				'groupids' => [$this->screenitem['resourceid']]
			]);

			$header->addItem((new CList())->addItem([_('Host'), ':', SPACE, $groups[0]['name']]));
		}

		$table = (new CTriggersInfo($this->screenitem['resourceid']))->setOrientation($this->screenitem['style']);

		$footer = (new CList())
			->addItem(_s('Updated: %s', zbx_date2str(TIME_FORMAT_SECONDS)))
			->addClass(ZBX_STYLE_DASHBRD_WIDGET_FOOT);

		return $this->getOutput(new CUiWidget(uniqid(), [$header, $table, $footer]));
	}
}
