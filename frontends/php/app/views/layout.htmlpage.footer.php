<?php


show_messages();

if ($data['web_layout_mode'] === ZBX_LAYOUT_NORMAL) {
	makePageFooter()->show();
}

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	CProfiler::getInstance()->show();
	makeDebugButton()->show();
}

insertPagePostJs();
require_once 'include/views/js/common.init.js.php';
