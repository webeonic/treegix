<?php
 


/**
 * Convert PHP variable to string version of JavaScript style
 *
 * @deprecated use CJs::encodeJson() instead
 * @see CJs::encodeJson()
 *
 * @param mixed $value
 * @param bool  $as_object return string containing javascript object
 * @param bool  $addQuotes whether quotes should be added at the beginning and at the end of string
 *
 * @return string
 */
function trx_jsvalue($value, $as_object = false, $addQuotes = true) {
	if (!is_array($value)) {
		if (is_object($value)) {
			return unpack_object($value);
		}
		elseif (is_string($value)) {
			$escaped = str_replace("\r", '', $value); // removing caret returns
			$escaped = str_replace("\\", "\\\\", $escaped); // escaping slashes: \ => \\
			$escaped = str_replace('"', '\"', $escaped); // escaping quotes: " => \"
			$escaped = str_replace("\n", '\n', $escaped); // changing LF to '\n' string
			$escaped = str_replace('\'', '\\\'', $escaped); // escaping single quotes: ' => \'
			$escaped = str_replace('/', '\/', $escaped); // escaping forward slash: / => \/
			if ($addQuotes) {
				$escaped = "'".$escaped."'";
			}
			return $escaped;
		}
		elseif (is_null($value)) {
			return 'null';
		}
		elseif (is_bool($value)) {
			return ($value) ? 'true' : 'false';
		}
		else {
			return strval($value);
		}
	}
	elseif (count($value) == 0) {
		return $as_object ? '{}' : '[]';
	}

	$is_object = $as_object;

	foreach ($value as $key => &$v) {
		$is_object |= is_string($key);
		$escaped_key = $is_object ? '"'.trx_jsvalue($key, false, false).'":' : '';
		$v = $escaped_key.trx_jsvalue($v, $as_object, $addQuotes);
	}
	unset($v);

	return $is_object ? '{'.implode(',', $value).'}' : '['.implode(',', $value).']';
}

function encodeValues(&$value, $encodeTwice = true) {
	if (is_string($value)) {
		$value = htmlentities($value, ENT_COMPAT, 'UTF-8');
		if ($encodeTwice) {
			$value = htmlentities($value, ENT_COMPAT, 'UTF-8');
		}
	}
	elseif (is_array(($value))) {
		foreach ($value as $key => $elem) {
			encodeValues($value[$key]);
		}
	}
	elseif (is_object(($value))) {
		foreach ($value->items as $key => $item) {
			encodeValues($value->items[$key], false);
		}
	}
}

function insert_javascript_for_visibilitybox() {
	if (defined('CVISIBILITYBOX_JAVASCRIPT_INSERTED')) {
		return null;
	}
	define('CVISIBILITYBOX_JAVASCRIPT_INSERTED', 1);

	$js = '
		function visibility_status_changeds(value, obj_id, replace_to) {
			var obj = document.getElementById(obj_id);
			if (is_null(obj)) {
				throw "Cannot find objects with name [" + obj_id +"]";
			}

			if (replace_to && replace_to != "") {
				if (obj.originalObject) {
					var old_obj = obj.originalObject;
					old_obj.originalObject = obj;
					obj.parentNode.replaceChild(old_obj, obj);
				}
				else if (!value) {
					try {
						var new_obj = document.createElement("span");
						new_obj.setAttribute("name", obj.name);
						new_obj.setAttribute("id", obj.id);
					}
					catch(e) {
						throw "Cannot create new element";
					}
					new_obj.innerHTML = replace_to;
					new_obj.originalObject = obj;
					obj.parentNode.replaceChild(new_obj, obj);
				}
				else {
					throw "Missing originalObject for restoring";
				}
			}
			else {
				obj.style.visibility = value ? "visible" : "hidden";
			}
		}';
	insert_js($js);
}

function insert_js($script, $jQueryDocumentReady = false) {
	echo get_js($script, $jQueryDocumentReady);
}

function get_js($script, $jQueryDocumentReady = false) {
	return $jQueryDocumentReady
		? '<script type="text/javascript">'."\n".'jQuery(document).ready(function() { '.$script.' });'."\n".'</script>'
		: '<script type="text/javascript">'."\n".$script."\n".'</script>';
}

// add JavaScript for calling after page loading
function trx_add_post_js($script) {
	global $TRX_PAGE_POST_JS;

	if ($TRX_PAGE_POST_JS === null) {
		$TRX_PAGE_POST_JS = [];
	}

	if (!in_array($script, $TRX_PAGE_POST_JS)) {
		$TRX_PAGE_POST_JS[] = $script;
	}
}

function insertPagePostJs() {
	global $TRX_PAGE_POST_JS;

	if ($TRX_PAGE_POST_JS) {
		echo get_js(implode("\n", $TRX_PAGE_POST_JS), true);
	}
}
