<?php


$table = new CTableInfo();

if ($data['error'] != null) {
	$table->setNoDataMessage($data['error']);
}
else {
	$table_header = [(new CColHeader(_('Timestamp')))->addClass(TRX_STYLE_CELL_WIDTH)];
	$names_at_top = ($data['style'] == STYLE_TOP && count($data['items']) > 1);

	if ($names_at_top) {
		$table->makeVerticalRotation();

		foreach ($data['items'] as $item) {
			$table_header[] = (new CColHeader(
				($data['same_host'] ? '' : $item['hosts'][0]['name'].NAME_DELIMITER).$item['name_expanded']
			))
				->addClass('vertical_rotation')
				->setTitle($item['name_expanded']);
		}
	}
	else {
		if ($data['style'] == STYLE_LEFT) {
			$table_header[] = _('Name');
		}
		$table_header[] = _('Value');
	}
	$table->setHeader($table_header);

	$clock = 0;
	$row_values = [];

	do {
		$history_item = array_shift($data['histories']);

		if ($history_item !== null && !$names_at_top) {
			$table_row = [
				(new CCol(zbx_date2str(DATE_TIME_FORMAT_SECONDS, $history_item['clock'])))->addClass(TRX_STYLE_NOWRAP)
			];
			if ($data['style'] == STYLE_LEFT) {
				$table->setHeadingColumn(1);
				$table_row[] = ($data['same_host']
					? ''
					: $data['items'][$history_item['itemid']]['hosts'][0]['name'].NAME_DELIMITER).
					$data['items'][$history_item['itemid']]['name_expanded'];
			}
			$table_row[] = $history_item['value'];
			$table->addRow($table_row);
		}
		else {
			if (($history_item === null && $row_values)
					|| ($clock != 0 && $history_item['clock'] != $clock)
					|| array_key_exists($history_item['itemid'], $row_values)) {
				$table_row = [
					(new CCol(zbx_date2str(DATE_TIME_FORMAT_SECONDS, $clock)))->addClass(TRX_STYLE_NOWRAP)
				];
				foreach ($data['items'] as $item) {
					$table_row[] = array_key_exists($item['itemid'], $row_values)
						? $row_values[$item['itemid']]
						: '';
				}
				$table->addRow($table_row);
				$row_values = [];
			}

			if ($history_item !== null) {
				$clock = $history_item['clock'];
				$row_values[$history_item['itemid']] = $history_item['value'];
			}
		}
	} while ($history_item !== null && $table->getNumRows() < $data['show_lines']);
}

$output = [
	'header' => $data['name'],
	'body' => $table->toString()
];

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
