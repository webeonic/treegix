<?php


$pageHeader = (new CPageHeader(_('Fatal error, please report to the Treegix team')))
	->addCssFile('assets/styles/'.CHtml::encode($data['theme']).'.css')
	->display();

$buttons = [
	(new CButton('back', _('Go to dashboard')))
		->onClick('javascript: document.location = "treegix.php?action=dashboard.view"'
)];

echo '<body lang="'.CWebUser::getLang().'">';

(new CTag('main', true,
	new CWarning(_('Fatal error, please report to the Treegix team'), $data['messages'], $buttons)
))->show();

echo '</body>';
echo '</html>';
