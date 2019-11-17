<?php



class CTemplateScreenImporter extends CAbstractScreenImporter {

	/**
	 * Import template screens.
	 *
	 * @param array $allScreens
	 *
	 * @return null
	 */
	public function import(array $allScreens) {
		if ((!$this->options['templateScreens']['createMissing']
				&& !$this->options['templateScreens']['updateExisting']) || !$allScreens) {
			return;
		}

		$screensToCreate = [];
		$screensToUpdate = [];

		foreach ($allScreens as $template => $screens) {
			$templateId = $this->referencer->resolveTemplate($template);

			if (!$this->importedObjectContainer->isTemplateProcessed($templateId)) {
				continue;
			}

			foreach ($screens as $screenName => $screen) {
				$screen['screenid'] = $this->referencer->resolveTemplateScreen($templateId, $screenName);

				$screen = $this->resolveScreenReferences($screen);
				if ($screen['screenid']) {
					$screensToUpdate[] = $screen;
				}
				else {
					$screen['templateid'] = $this->referencer->resolveTemplate($template);
					$screensToCreate[] = $screen;
				}
			}
		}

		if ($this->options['templateScreens']['createMissing'] && $screensToCreate) {
			$newScreenIds = API::TemplateScreen()->create($screensToCreate);
			foreach ($screensToCreate as $num => $newScreen) {
				$screenId = $newScreenIds['screenids'][$num];
				$this->referencer->addTemplateScreenRef($newScreen['name'], $screenId);
			}
		}

		if ($this->options['templateScreens']['updateExisting'] && $screensToUpdate) {
			API::TemplateScreen()->update($screensToUpdate);
		}
	}

	/**
	 * Deletes missing template screens.
	 *
	 * @param array $allScreens
	 *
	 * @return null
	 */
	public function delete(array $allScreens) {
		if (!$this->options['templateScreens']['deleteMissing']) {
			return;
		}

		$templateIdsXML = $this->importedObjectContainer->getTemplateIds();

		// no templates have been processed
		if (!$templateIdsXML) {
			return;
		}

		$templateScreenIdsXML = [];

		if ($allScreens) {
			foreach ($allScreens as $template => $screens) {
				$templateId = $this->referencer->resolveTemplate($template);

				if ($templateId) {
					foreach ($screens as $screenName => $screen) {
						$templateScreenId = $this->referencer->resolveTemplateScreen($templateId, $screenName);

						if ($templateScreenId) {
							$templateScreenIdsXML[$templateScreenId] = $templateScreenId;
						}
					}
				}
			}
		}

		$dbTemplateScreenIds = API::TemplateScreen()->get([
			'output' => ['screenid'],
			'filter' => [
				'templateid' => $templateIdsXML
			],
			'nopermissions' => true,
			'preservekeys' => true
		]);

		$templateScreensToDelete = array_diff_key($dbTemplateScreenIds, $templateScreenIdsXML);

		if ($templateScreensToDelete) {
			API::TemplateScreen()->delete(array_keys($templateScreensToDelete));
		}
	}
}
