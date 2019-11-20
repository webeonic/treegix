<?php
 


function get_maintenance_by_maintenanceid($maintenanceid) {
	return DBfetch(DBselect(
		'SELECT m.* FROM maintenances m WHERE m.maintenanceid='.trx_dbstr($maintenanceid)
	));
}

function timeperiod_type2str($timeperiod_type) {
	switch ($timeperiod_type) {
		case TIMEPERIOD_TYPE_ONETIME:
			return _('One time only');
		case TIMEPERIOD_TYPE_DAILY:
			return _('Daily');
		case TIMEPERIOD_TYPE_WEEKLY:
			return _('Weekly');
		case TIMEPERIOD_TYPE_MONTHLY:
			return _('Monthly');
	}
	return _('Unknown');
}

function schedule2str($timeperiod) {
	$timeperiod['hour'] = floor($timeperiod['start_time'] / SEC_PER_HOUR);
	$timeperiod['minute'] = floor(($timeperiod['start_time'] - ($timeperiod['hour'] * SEC_PER_HOUR)) / SEC_PER_MIN);
	if ($timeperiod['hour'] < 10) {
		$timeperiod['hour'] = '0'.$timeperiod['hour'];
	}
	if ($timeperiod['minute'] < 10) {
		$timeperiod['minute'] = '0'.$timeperiod['minute'];
	}

	if ($timeperiod['timeperiod_type'] == TIMEPERIOD_TYPE_ONETIME) {
		$str = trx_date2str(DATE_TIME_FORMAT, $timeperiod['start_date']);
	}
	elseif ($timeperiod['timeperiod_type'] == TIMEPERIOD_TYPE_DAILY) {
		$str = _n('At %1$s:%2$s every day',
			'At %1$s:%2$s every %3$s days',
			$timeperiod['hour'],
			$timeperiod['minute'],
			$timeperiod['every']
		);
	}
	elseif ($timeperiod['timeperiod_type'] == TIMEPERIOD_TYPE_WEEKLY) {
		$days = '';
		$dayofweek = trx_num2bitstr($timeperiod['dayofweek'], true);
		$length = strlen($dayofweek);
		for ($i = 0; $i < $length; $i++) {
			if ($dayofweek[$i] == 1) {
				if (!trx_empty($days)) {
					$days .= ', ';
				}
				$days .= getDayOfWeekCaption($i + 1);
			}
		}

		$str = _n('At %1$s:%2$s %3$s of every week',
			'At %1$s:%2$s %3$s of every %4$s weeks',
			$timeperiod['hour'],
			$timeperiod['minute'],
			$days,
			$timeperiod['every']
		);
	}
	elseif ($timeperiod['timeperiod_type'] == TIMEPERIOD_TYPE_MONTHLY) {
		$months = '';
		$month = trx_num2bitstr($timeperiod['month'], true);
		$length = strlen($month);
		for ($i = 0; $i < $length; $i++) {
			if ($month[$i] == 1) {
				if (!trx_empty($months)) {
					$months .=', ';
				}
				$months .= getMonthCaption($i + 1);
			}
		}
		if ($timeperiod['dayofweek'] > 0) {
			$days = '';
			$dayofweek = trx_num2bitstr($timeperiod['dayofweek'], true);
			$length = strlen($dayofweek);
			for ($i = 0; $i < $length; $i++) {
				if ($dayofweek[$i] == 1) {
					if (!trx_empty($days)) {
						$days .= ', ';
					}
					$days .= getDayOfWeekCaption($i + 1);
				}
			}

			$every = '';
			switch ($timeperiod['every']) {
				case 1: $every = _('first'); break;
				case 2: $every = _x('second', 'adjective'); break;
				case 3: $every = _('third'); break;
				case 4: $every = _('fourth'); break;
				case 5: $every = _('last'); break;
			}

			$str = _s('At %1$s:%2$s on %3$s %4$s of every %5$s',
				$timeperiod['hour'],
				$timeperiod['minute'],
				$every,
				$days,
				$months
			);
		}
		else {
			$str = _s('At %1$s:%2$s on day %3$s of every %4$s',
				$timeperiod['hour'],
				$timeperiod['minute'],
				$timeperiod['day'],
				$months
			);
		}
	}
	return $str;
}
