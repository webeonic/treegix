<?php



function get_default_image() {
	$image = imagecreate(50, 50);
	$color = imagecolorallocate($image, 250, 50, 50);
	imagefill($image, 0, 0, $color);

	return $image;
}

/**
 * Get image data from db, cache is used
 * @param  $imageid
 * @return array image data from db
 */
function get_image_by_imageid($imageid) {
	static $images = [];

	if (!isset($images[$imageid])) {
		$row = DBfetch(DBselect('SELECT i.* FROM images i WHERE i.imageid='.zbx_dbstr($imageid)));
		$row['image'] = zbx_unescape_image($row['image']);
		$images[$imageid] = $row;
	}
	return $images[$imageid];
}

function zbx_unescape_image($image) {
	global $DB;

	$result = $image ? $image : 0;
	if ($DB['TYPE'] == TRX_DB_POSTGRESQL) {
		$result = pg_unescape_bytea($image);
	}
	return $result;
}

/**
 * Resizes the given image resource to the specified size keeping the original
 * proportions of the image.
 *
 * @param resource $source
 * @param int $thumbWidth
 * @param int $thumbHeight
 *
 * @return resource
 */
function imageThumb($source, $thumbWidth = 0, $thumbHeight = 0) {
	$srcWidth	= imagesx($source);
	$srcHeight	= imagesy($source);

	if ($srcWidth > $thumbWidth || $srcHeight > $thumbHeight) {
		if ($thumbWidth == 0) {
			$thumbWidth = $thumbHeight * $srcWidth / $srcHeight;
		}
		elseif ($thumbHeight == 0) {
			$thumbHeight = $thumbWidth * $srcHeight / $srcWidth;
		}
		else {
			$a = $thumbWidth / $thumbHeight;
			$b = $srcWidth / $srcHeight;

			if ($a > $b) {
				$thumbWidth = $b * $thumbHeight;
			}
			else {
				$thumbHeight = $thumbWidth / $b;
			}
		}

		if (function_exists('imagecreatetruecolor') && @imagecreatetruecolor(1, 1)) {
			$thumb = imagecreatetruecolor($thumbWidth, $thumbHeight);
		}
		else {
			$thumb = imagecreate($thumbWidth, $thumbHeight);
		}

		// preserve png transparency
		imagealphablending($thumb, false);
		imagesavealpha($thumb, true);

		imagecopyresampled(
			$thumb, $source,
			0, 0,
			0, 0,
			$thumbWidth, $thumbHeight,
			$srcWidth, $srcHeight);

		imagedestroy($source);
		$source = $thumb;
	}
	return $source;
}

/**
 * Creates an image from a string preserving PNG transparency.
 *
 * @param $imageString
 *
 * @return resource
 */
function imageFromString($imageString) {
	$image = imagecreatefromstring($imageString);

	// preserve PNG transparency
	imagealphablending($image, false);
	imagesavealpha($image, true);
	return $image;
}
