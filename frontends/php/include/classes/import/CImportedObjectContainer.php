<?php



/**
 * Class that holds processed (created and updated) host and template IDs during the current import.
 */
class CImportedObjectContainer {
	/**
	 * @var array with created and updated hosts.
	 */
	protected $hostIds = [];

	/**
	 * @var array with created and updated templates.
	 */
	protected $templateIds = [];

	/**
	 * Add host IDs that have been created and updated.
	 *
	 * @param array $hostIds
	 */
	public function addHostIds(array $hostIds) {
		foreach ($hostIds as $hostId) {
			$this->hostIds[$hostId] = $hostId;
		}
	}

	/**
	 * Add template IDs that have been created and updated.
	 *
	 * @param array $templateIds
	 */
	public function addTemplateIds(array $templateIds) {
		foreach ($templateIds as $templateId) {
			$this->templateIds[$templateId] = $templateId;
		}
	}

	/**
	 * Checks if host has been created and updated during the current import.
	 *
	 * @param string $hostId
	 *
	 * @return bool
	 */
	public function isHostProcessed($hostId) {
		return isset($this->hostIds[$hostId]);
	}

	/**
	 * Checks if template has been created and updated during the current import.
	 *
	 * @param string $templateId
	 *
	 * @return bool
	 */
	public function isTemplateProcessed($templateId) {
		return isset($this->templateIds[$templateId]);
	}

	/**
	 * Get array of created and updated hosts IDs.
	 *
	 * @return array
	 */
	public function getHostIds() {
		return array_values($this->hostIds);
	}

	/**
	 * Get array of created and updated template IDs.
	 *
	 * @return array
	 */
	public function getTemplateIds() {
		return array_values($this->templateIds);
	}
}
