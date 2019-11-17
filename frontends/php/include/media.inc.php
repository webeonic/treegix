<?php



function media_type2str($type = null) {
	$types = [
		MEDIA_TYPE_EMAIL => _('Email'),
		MEDIA_TYPE_EXEC => _('Script'),
		MEDIA_TYPE_SMS => _('SMS'),
		MEDIA_TYPE_WEBHOOK => _('Webhook')
	];

	if ($type === null) {
		natsort($types);

		return $types;
	}

	return $types[$type];
}
