<?php
 


class CScreenTriggersOverview extends CScreenBase {

	/**
	 * Process screen.
	 *
	 * @return CDiv (screen inside container)
	 */
	public function get() {
		$groups = API::HostGroup()->get([
			'output' => ['name'],
			'groupids' => $this->screenitem['resourceid']
		]);

		$header = (new CDiv([
			new CTag('h4', true, _('Trigger overview')),
			(new CList())->addItem([_('Group'), ':', SPACE, $groups[0]['name']])
		]))->addClass(ZBX_STYLE_DASHBRD_WIDGET_HEAD);

		list($hosts, $triggers) = getTriggersOverviewData((array) $this->screenitem['resourceid'],
			$this->screenitem['application']
		);

		$table = getTriggersOverview($hosts, $triggers, $this->pageFile, $this->screenitem['style'], $this->screenid);

		$footer = (new CList())
			->addItem(_s('Updated: %s', zbx_date2str(TIME_FORMAT_SECONDS)))
			->addClass(ZBX_STYLE_DASHBRD_WIDGET_FOOT);

		return $this->getOutput(new CUiWidget(uniqid(), [$header, $table, $footer]));
	}
}
