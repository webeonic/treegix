<?php


$pageHeader = (new CPageHeader(_('Warning').' ['._s('refreshed every %1$s sec.', 30).']'))
	->addCssFile('assets/styles/'.CHtml::encode($data['theme']).'.css')
	->display();

$buttons = array_key_exists('buttons', $data)
	? $data['buttons']
	: [(new CButton(null, _('Retry')))->onClick('document.location.reload();')];

echo '<body lang="'.CWebUser::getLang().'">';

(new CTag('main', true, new CWarning($data['header'], $data['messages'], $buttons)))->show();

echo get_js("setTimeout('document.location.reload();', 30000);");
echo '</body>';
echo '</html>';
