<?php



require_once dirname(__FILE__).'/include/gettextwrapper.inc.php';
require_once dirname(__FILE__).'/include/func.inc.php';
require_once dirname(__FILE__).'/include/html.inc.php';
require_once dirname(__FILE__).'/include/defines.inc.php';
require_once dirname(__FILE__).'/include/classes/mvc/CView.php';
require_once dirname(__FILE__).'/include/classes/html/CObject.php';
require_once dirname(__FILE__).'/include/classes/html/CTag.php';
require_once dirname(__FILE__).'/include/classes/html/CLink.php';
require_once dirname(__FILE__).'/include/classes/helpers/CBrandHelper.php';
require_once dirname(__FILE__).'/include/html.inc.php';

(new CView('general.browserwarning'))->render();
