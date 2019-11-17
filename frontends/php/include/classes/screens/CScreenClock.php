<?php



class CScreenClock extends CScreenBase {

	/**
	 * Process screen.
	 *
	 * @return CDiv (screen inside container)
	 */
	public function get() {
		$time = null;
		$title = null;
		$time_zone_string = null;
		$time_zone_offset = null;
		$error = null;

		switch ($this->screenitem['style']) {
			case TIME_TYPE_HOST:
				$itemid = $this->screenitem['resourceid'];

				if (!empty($this->hostid)) {
					$new_itemid = get_same_item_for_host($itemid, $this->hostid);
					$itemid = !empty($new_itemid) ? $new_itemid : '';
				}

				$items = API::Item()->get([
					'output' => ['itemid', 'value_type'],
					'selectHosts' => ['name'],
					'itemids' => [$itemid]
				]);

				if ($items) {
					$item = $items[0];
					$title = $item['hosts'][0]['name'];
					unset($items, $item['hosts']);

					$last_value = Manager::History()->getLastValues([$item]);

					if ($last_value) {
						$last_value = $last_value[$item['itemid']][0];

						try {
							$now = new DateTime($last_value['value']);

							$time_zone_string = _s('GMT%1$s', $now->format('P'));
							$time_zone_offset = $now->format('Z');

							$time = time() - ($last_value['clock'] - $now->getTimestamp());
						}
						catch (Exception $e) {
							$error = _('No data');
						}
					}
					else {
						$error = _('No data');
					}
				}
				else {
					$error = _('No data');
				}
				break;

			case TIME_TYPE_SERVER:
				$title = _('Server');

				$now = new DateTime();
				$time = $now->getTimestamp();
				$time_zone_string = _s('GMT%1$s', $now->format('P'));
				$time_zone_offset = $now->format('Z');
				break;

			default:
				$title = _('Local');
				break;
		}

		if ($this->screenitem['width'] > $this->screenitem['height']) {
			$this->screenitem['width'] = $this->screenitem['height'];
		}

		$clock = (new CClock())
			->setWidth($this->screenitem['width'])
			->setHeight($this->screenitem['height'])
			->setTimeZoneString($time_zone_string);

		if ($error !== null) {
			$clock->setError($error);
		}

		if ($time !== null) {
			$clock->setTime($time);
		}

		if ($time_zone_offset !== null) {
			$clock->setTimeZoneOffset($time_zone_offset);
		}

		if ($error === null) {
			if (!defined('TRX_CLOCK')) {
				define('TRX_CLOCK', 1);
				insert_js(file_get_contents($clock->getScriptFile()));
			}
			zbx_add_post_js($clock->getScriptRun());
		}

		$item = [];
		$item[] = $clock->getTimeDiv()
			->addClass(TRX_STYLE_TIME_ZONE);
		$item[] = $clock;
		$item[] = (new CDiv($title))
			->addClass(TRX_STYLE_LOCAL_CLOCK)
			->addClass(TRX_STYLE_GREY);

		return $this->getOutput($item);
	}
}
