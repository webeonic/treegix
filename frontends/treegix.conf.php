<?php
// Treegix GUI configuration file.
global $DB;

$DB['TYPE']     = 'POSTGRESQL';
$DB['SERVER']   = 'postgres';
$DB['PORT']     = '5432';
$DB['DATABASE'] = 'treegix';
$DB['USER']     = 'treegix';
$DB['PASSWORD'] = 'treegix';

// Schema name. Used for IBM DB2 and PostgreSQL.
$DB['SCHEMA'] = '';

$TRX_SERVER      = 'treegix_server';
$TRX_SERVER_PORT = '10051';
$TRX_SERVER_NAME = '';

$IMAGE_FORMAT_DEFAULT = IMAGE_FORMAT_PNG;
