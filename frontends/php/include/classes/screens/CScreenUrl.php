<?php
 


class CScreenUrl extends CScreenBase {

	/**
	 * Process screen.
	 *
	 * @return CDiv (screen inside container)
	 */
	public function get() {
		// prevent from resolving macros in configuration page
		if ($this->mode != SCREEN_MODE_PREVIEW && $this->mode != SCREEN_MODE_SLIDESHOW) {
			return $this->getOutput(
				CHtmlUrlValidator::validate($this->screenitem['url'])
					? new CIFrame($this->screenitem['url'], $this->screenitem['width'], $this->screenitem['height'],
							'auto')
					: makeMessageBox(false, [[
								'type' => 'error',
								'message' => _s('Provided URL "%1$s" is invalid.', $this->screenitem['url'])
							]]
						)
			);
		}
		elseif ($this->screenitem['dynamic'] == SCREEN_DYNAMIC_ITEM && $this->hostid == 0) {
			return $this->getOutput((new CTableInfo())->setNoDataMessage(_('No host selected.')));
		}

		$resolveHostMacros = ($this->screenitem['dynamic'] == SCREEN_DYNAMIC_ITEM || $this->isTemplatedScreen);

		$url = CMacrosResolverHelper::resolveWidgetURL([
			'config' => $resolveHostMacros ? 'widgetURL' : 'widgetURLUser',
			'url' => $this->screenitem['url'],
			'hostid' => $resolveHostMacros ? $this->hostid : 0
		]);

		$this->screenitem['url'] = $url ? $url : $this->screenitem['url'];

		return $this->getOutput(
			CHtmlUrlValidator::validate($this->screenitem['url'])
				? new CIFrame($this->screenitem['url'], $this->screenitem['width'], $this->screenitem['height'], 'auto')
				: makeMessageBox(false, [[
							'type' => 'error',
							'message' => _s('Provided URL "%1$s" is invalid.', $this->screenitem['url'])
						]]
					)
		);
	}
}
