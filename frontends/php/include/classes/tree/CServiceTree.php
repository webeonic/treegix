<?php



/**
 * A class for rendering service trees.
 *
 * @see createServiceMonitoringTree() and createServiceConfigurationTree() for a way of creating trees from services
 */
class CServiceTree extends CTree {

	/**
	 * Returns a column object for the given row and field. Add additional service tree related formatting.
	 *
	 * @param $rowId
	 * @param $colName
	 *
	 * @return CCol
	 */
	protected function makeCol($rowId, $colName) {
		$config = select_config();

		switch ($colName) {
			case 'status':
				if (trx_is_int($this->tree[$rowId][$colName]) && $this->tree[$rowId]['id'] > 0) {
					$status = $this->tree[$rowId][$colName];

					// do not show the severity for information and unclassified triggers
					if (in_array($status, [TRIGGER_SEVERITY_INFORMATION, TRIGGER_SEVERITY_NOT_CLASSIFIED])) {
						return (new CCol(_('OK')))->addClass(TRX_STYLE_GREEN);
					}
					else {
						return (new CCol(getSeverityName($status, $config)))->addClass(getSeverityStyle($status));
					}
				}
				break;

			case 'sla':
				return parent::makeCol($rowId, $colName)->addClass(TRX_STYLE_CELL_WIDTH);
		}

		return parent::makeCol($rowId, $colName);
	}
}
