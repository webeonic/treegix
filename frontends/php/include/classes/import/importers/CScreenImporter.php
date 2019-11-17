<?php



class CScreenImporter extends CAbstractScreenImporter {

	/**
	 * Import screens.
	 *
	 * @param array $screens
	 */
	public function import(array $screens) {
		$screens_to_create = [];
		$screens_to_update = [];

		foreach ($screens as $screen) {
			$screen = $this->resolveScreenReferences($screen);

			if ($screenid = $this->referencer->resolveScreen($screen['name'])) {
				$screen['screenid'] = $screenid;
				$screens_to_update[] = $screen;
			}
			else {
				$screens_to_create[] = $screen;
			}
		}

		if ($this->options['screens']['createMissing'] && $screens_to_create) {
			API::Screen()->create($screens_to_create);
		}

		if ($this->options['screens']['updateExisting'] && $screens_to_update) {
			API::Screen()->update($screens_to_update);
		}
	}
}
