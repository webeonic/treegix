<?php


$widget_view = include('include/classes/widgets/views/widget.'.$data['dialogue']['type'].'.form.view.php');

$form = $widget_view['form'];

// Submit button is needed to enable submit event on Enter on inputs.
$form->addItem((new CInput('submit', 'dashboard_widget_config_submit'))->addStyle('display: none;'));

$output = [
	'type' => $data['dialogue']['type'],
	'body' => $form->toString()
];

if (array_key_exists('jq_templates', $widget_view)) {
	foreach ($widget_view['jq_templates'] as $id => $jq_template) {
		$output['body'] .= '<script type="text/x-jquery-tmpl" id="'.$id.'">'.$jq_template.'</script>';
	}
}

if (array_key_exists('scripts', $widget_view)) {
	$output['body'] .= get_js(implode("\n", $widget_view['scripts']));
}

if (($messages = getMessages()) !== null) {
	$output['messages'] = $messages->toString();
}

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
