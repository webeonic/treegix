<?php



class CScreenDataOverview extends CScreenBase {

	/**
	 * Process screen.
	 *
	 * @return CDiv (screen inside container)
	 */
	public function get() {
		$groupid = $this->screenitem['resourceid'];

		$groups = API::HostGroup()->get([
			'output' => ['name'],
			'groupids' => $groupid
		]);

		$header = (new CDiv([
			new CTag('h4', true, _('Data overview')),
			(new CList())
				->addItem([_('Group'), ':', SPACE, $groups[0]['name']])
		]))->addClass(TRX_STYLE_DASHBRD_WIDGET_HEAD);

		$table = getItemsDataOverview((array) $groupid, $this->screenitem['application'], $this->screenitem['style'],
			TRX_PROBLEM_SUPPRESSED_FALSE
		);

		$footer = (new CList())
			->addItem(_s('Updated: %s', zbx_date2str(TIME_FORMAT_SECONDS)))
			->addClass(TRX_STYLE_DASHBRD_WIDGET_FOOT);

		return $this->getOutput(new CUiWidget(uniqid(), [$header, $table, $footer]));
	}
}
