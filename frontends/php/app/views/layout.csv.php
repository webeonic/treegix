<?php



header('Content-Type: text/csv; charset=UTF-8');
header('Content-Disposition: attachment; filename="'.$data['page']['file'].'"');

echo $data['main_block'];
