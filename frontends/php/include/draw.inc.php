<?php



/**
 * Calculate new color based on bg/fg colors and transparency index
 *
 * @param   resource  $image      image reference
 * @param   array     $bgColor    background color, array of RGB
 * @param   array     $fgColor    foreground color, array of RGB
 * @param   float     $alpha      transparency index in range of 0-1, 1 returns unchanged fgColor color
 *
 * @return  array                 new color
 */
function trx_colormix($image, $bgColor, $fgColor, $alpha) {
	$r = $bgColor[0] + ($fgColor[0] - $bgColor[0]) * $alpha;
	$g = $bgColor[1] + ($fgColor[1] - $bgColor[1]) * $alpha;
	$b = $bgColor[2] + ($fgColor[2] - $bgColor[2]) * $alpha;

	return imagecolorresolvealpha($image, $r, $g, $b, 0);
}

/**
 * Draw normal line.
 * PHP imageline() function is broken because it drops fraction instead of correct rounding of X/Y coordinates.
 * All calls to imageline() must be replaced by the wrapper function everywhere in the code.
 *
 * @param resource  $image  image reference
 * @param int       $x1     first x coordinate
 * @param int       $y1     first y coordinate
 * @param int       $x2     second x coordinate
 * @param int       $y2     second y coordinate
 * @param int       $color  line color
 */
function trx_imageline($image, $x1, $y1, $x2, $y2, $color) {
		imageline($image, round($x1), round($y1), round($x2), round($y2), $color);
}

/**
 * Draw antialiased line
 *
 * @param resource  $image  image reference
 * @param int       $x1     first x coordinate
 * @param int       $y1     first y coordinate
 * @param int       $x2     second x coordinate
 * @param int       $y2     second y coordinate
 * @param int       $color  line color
 * @param int       $style  line style, one of LINE_TYPE_NORMAL (default), LINE_TYPE_BOLD (bold line)
 */
function trx_imagealine($image, $x1, $y1, $x2, $y2, $color, $style = LINE_TYPE_NORMAL) {
	$x1 = round($x1);
	$y1 = round($y1);
	$x2 = round($x2);
	$y2 = round($y2);

	if ($x1 == $x2 && $y1 == $y2) {
		imagesetpixel($image, $x1, $y1, $color);
		return;
	}

	// Get foreground line color
	$lc = imagecolorsforindex($image, $color);
	$lc = [$lc['red'], $lc['green'], $lc['blue']];

	$dx = $x2 - $x1;
	$dy = $y2 - $y1;

	if (abs($dx) > abs($dy)) {
		if ($dx < 0) {
			trx_swap($x1, $x2);
			$y1 = $y2;
		}
		for ($x = $x1, $y = $y1; $x <= $x2; $x++, $y = $y1 + ($x - $x1) * $dy / $dx) {
			$yint = floor($y);
			$yfrac = $y - $yint;

			if (LINE_TYPE_BOLD == $style) {
				$bc = imagecolorsforindex($image, imagecolorat($image, $x, $yint - 1));
				$bc = [$bc['red'], $bc['green'], $bc['blue']];
				imagesetpixel($image, $x, $yint - 1, trx_colormix($image, $lc, $bc, $yfrac));

				$bc = imagecolorsforindex($image, imagecolorat($image, $x, $yint + 1));
				$bc = [$bc['red'], $bc['green'], $bc['blue']];
				imagesetpixel($image, $x, $yint + 1, trx_colormix($image, $lc, $bc, 1 - $yfrac));

				imagesetpixel($image, $x, $yint, $color);
			}
			else {
				$bc = imagecolorsforindex($image, imagecolorat($image, $x, $yint));
				$bc = [$bc['red'], $bc['green'], $bc['blue']];
				imagesetpixel($image, $x, $yint, trx_colormix($image, $lc, $bc, $yfrac));

				$bc = imagecolorsforindex($image, imagecolorat($image, $x, $yint + 1));
				$bc = [$bc['red'], $bc['green'], $bc['blue']];
				imagesetpixel($image, $x, $yint + 1, trx_colormix($image, $lc, $bc, 1 - $yfrac));
			}
		}
	}
	else {
		if ($dy < 0) {
			trx_swap($y1, $y2);
			$x1 = $x2;
		}
		for ($y = $y1, $x = $x1; $y <= $y2; $y++, $x = $x1 + ($y - $y1) * $dx / $dy)
		{
			$xint = floor($x);
			$xfrac = $x - $xint;

			if (LINE_TYPE_BOLD == $style) {
				$bc = imagecolorsforindex($image, imagecolorat($image, $xint - 1, $y));
				$bc = [$bc['red'], $bc['green'], $bc['blue']];
				imagesetpixel($image, $xint - 1, $y, trx_colormix($image, $lc, $bc, $xfrac));

				$bc = imagecolorsforindex($image, imagecolorat($image, $xint + 1, $y));
				$bc = [$bc['red'], $bc['green'], $bc['blue']];
				imagesetpixel($image, $xint + 1, $y, trx_colormix($image, $lc, $bc, 1 - $xfrac));

				imagesetpixel($image, $xint, $y, $color);
			}
			else {
				$bc = imagecolorsforindex($image, imagecolorat($image, $xint, $y));
				$bc = [$bc['red'], $bc['green'], $bc['blue']];
				imagesetpixel($image, $xint, $y, trx_colormix($image, $lc, $bc, $xfrac));

				$bc = imagecolorsforindex($image, imagecolorat($image, $xint + 1, $y));
				$bc = [$bc['red'], $bc['green'], $bc['blue']];
				imagesetpixel($image, $xint + 1, $y, trx_colormix($image, $lc, $bc, 1 - $xfrac));
			}
		}
	}
}
