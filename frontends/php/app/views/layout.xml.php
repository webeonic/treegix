<?php



header('Content-Type: text/xml; charset=utf-8');
header('Content-Disposition: attachment; filename="'.$data['page']['file'].'"');

echo $data['main_block'];
