<?php



require_once dirname(__FILE__).'/include/config.inc.php';
require_once dirname(__FILE__).'/include/images.inc.php';

$page['file'] = 'image.php';
$page['title'] = _('Image');
$page['type'] = PAGE_TYPE_IMAGE;

require_once dirname(__FILE__).'/include/page_header.php';

//	VAR		TYPE	OPTIONAL	FLAGS	VALIDATION	EXCEPTION
$fields = [
	'imageid' =>	[T_TRX_INT, O_MAND, P_SYS, DB_ID,				null],
	'width' =>		[T_TRX_INT, O_OPT, P_SYS,	BETWEEN(1, 2000),	null],
	'height' =>		[T_TRX_INT, O_OPT, P_SYS,	BETWEEN(1, 2000),	null],
];
check_fields($fields);

$resize = false;
if (isset($_REQUEST['width']) || isset($_REQUEST['height'])) {
	$resize = true;
	$width = getRequest('width', 0);
	$height = getRequest('height', 0);
}
if (!($row = get_image_by_imageid($_REQUEST['imageid']))) {
	error(_('Incorrect image index.'));
	require_once dirname(__FILE__).'/include/page_footer.php';
}
$source = imageFromString($row['image']);
unset($row);

if ($resize) {
	$source = imageThumb($source, $width, $height);
}
imageout($source);

require_once dirname(__FILE__).'/include/page_footer.php';
