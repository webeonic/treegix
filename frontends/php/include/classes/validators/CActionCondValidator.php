<?php


class CActionCondValidator extends CValidator {

	/**
	 * Returns true if the given $value is valid, or set's an error and returns false otherwise.
	 *
	 *
	 * @param $condition
	 *
	 * @return bool
	 */
	public function validate($condition) {
		// build validators
		$discoveryCheckTypeValidator = new CLimitedSetValidator([
			'values' => array_keys(discovery_check_type2str())
		]);
		$discoveryObjectStatusValidator = new CLimitedSetValidator([
			'values' => array_keys(discovery_object_status2str())
		]);
		$triggerSeverityValidator = new CLimitedSetValidator([
			'values' => [
				TRIGGER_SEVERITY_NOT_CLASSIFIED,
				TRIGGER_SEVERITY_INFORMATION,
				TRIGGER_SEVERITY_WARNING,
				TRIGGER_SEVERITY_AVERAGE,
				TRIGGER_SEVERITY_HIGH,
				TRIGGER_SEVERITY_DISASTER
			]
		]);
		$discoveryObjectValidator = new CLimitedSetValidator([
			'values' => array_keys(discovery_object2str())
		]);
		$triggerValueValidator = new CLimitedSetValidator([
			'values' => array_keys(trigger_value2str())
		]);
		$eventTypeValidator = new CLimitedSetValidator([
			'values' => array_keys(eventType())
		]);

		$conditionValue = $condition['value'];
		// validate condition values depending on condition type
		switch ($condition['conditiontype']) {
			case CONDITION_TYPE_HOST_GROUP:
				if (!$conditionValue) {
					$this->setError(_('Empty action condition.'));
				}
				break;

			case CONDITION_TYPE_TEMPLATE:
				if (!$conditionValue) {
					$this->setError(_('Empty action condition.'));
				}
				break;

			case CONDITION_TYPE_TRIGGER:
				if (!$conditionValue) {
					$this->setError(_('Empty action condition.'));
				}
				break;

			case CONDITION_TYPE_HOST:
				if (!$conditionValue) {
					$this->setError(_('Empty action condition.'));
				}
				break;

			case CONDITION_TYPE_DRULE:
				if (!$conditionValue) {
					$this->setError(_('Empty action condition.'));
				}
				break;

			case CONDITION_TYPE_DCHECK:
				if (!$conditionValue) {
					$this->setError(_('Empty action condition.'));
				}
				break;

			case CONDITION_TYPE_PROXY:
				if (!$conditionValue) {
					$this->setError(_('Empty action condition.'));
				}
				break;

			case CONDITION_TYPE_DOBJECT:
				if (zbx_empty($conditionValue)) {
					$this->setError(_('Empty action condition.'));
				}
				elseif (!$discoveryObjectValidator->validate($conditionValue)) {
					$this->setError(_('Incorrect action condition discovery object.'));
				}
				break;

			case CONDITION_TYPE_TIME_PERIOD:
				$time_period_parser = new CTimePeriodsParser(['usermacros' => true]);

				if ($time_period_parser->parse($conditionValue) != CParser::PARSE_SUCCESS) {
					$this->setError(_('Invalid time period.'));
				}
				break;

			case CONDITION_TYPE_DHOST_IP:
				$ip_range_parser = new CIPRangeParser(['v6' => TRX_HAVE_IPV6, 'dns' => false, 'max_ipv4_cidr' => 30]);
				if (zbx_empty($conditionValue)) {
					$this->setError(_('Empty action condition.'));
				}
				elseif (!$ip_range_parser->parse($conditionValue)) {
					$this->setError(_s('Invalid action condition: %1$s.', $ip_range_parser->getError()));
				}
				break;

			case CONDITION_TYPE_DSERVICE_TYPE:
				if (zbx_empty($conditionValue)) {
					$this->setError(_('Empty action condition.'));
				}
				elseif (!$discoveryCheckTypeValidator->validate($conditionValue)) {
					$this->setError(_('Incorrect action condition discovery check.'));
				}
				break;

			case CONDITION_TYPE_DSERVICE_PORT:
				if (zbx_empty($conditionValue)) {
					$this->setError(_('Empty action condition.'));
				}
				elseif (!validate_port_list($conditionValue)) {
					$this->setError(_s('Incorrect action condition port "%1$s".', $conditionValue));
				}
				break;

			case CONDITION_TYPE_DSTATUS:
				if (zbx_empty($conditionValue)) {
					$this->setError(_('Empty action condition.'));
				}
				elseif (!$discoveryObjectStatusValidator->validate($conditionValue)) {
					$this->setError(_('Incorrect action condition discovery status.'));
				}
				break;

			case CONDITION_TYPE_SUPPRESSED:
				if (!zbx_empty($conditionValue)) {
					$this->setError(_('Action condition value must be empty.'));
				}
				break;

			case CONDITION_TYPE_TRIGGER_SEVERITY:
				if (zbx_empty($conditionValue)) {
					$this->setError(_('Empty action condition.'));
				}
				elseif (!$triggerSeverityValidator->validate($conditionValue)) {
					$this->setError(_('Incorrect action condition trigger severity.'));
				}
				break;

			case CONDITION_TYPE_EVENT_TYPE:
				if (zbx_empty($conditionValue)) {
					$this->setError(_('Empty action condition.'));
				}
				elseif (!$eventTypeValidator->validate($conditionValue)) {
					$this->setError(_('Incorrect action condition event type.'));
				}
				break;

			case CONDITION_TYPE_TRIGGER_NAME:
			case CONDITION_TYPE_DUPTIME:
			case CONDITION_TYPE_DVALUE:
			case CONDITION_TYPE_APPLICATION:
			case CONDITION_TYPE_HOST_NAME:
			case CONDITION_TYPE_HOST_METADATA:
			case CONDITION_TYPE_EVENT_TAG:
				if (zbx_empty($conditionValue)) {
					$this->setError(_('Empty action condition.'));
				}
				break;

			case CONDITION_TYPE_EVENT_TAG_VALUE:
				if (!is_string($condition['value']) || $condition['value'] === '' ||
						!is_string($condition['value2']) || $condition['value2'] === '') {
					$this->setError(_('Empty action condition.'));
				}
				break;

			default:
				$this->setError(_('Incorrect action condition type.'));
		}

		// If no error is not set, return true.
		return !(bool) $this->getError();
	}
}
