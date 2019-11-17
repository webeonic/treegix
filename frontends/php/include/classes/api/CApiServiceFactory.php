<?php



class CApiServiceFactory extends CRegistryFactory {

	public function __construct(array $objects = []) {
		parent::__construct(array_merge([
			// a generic API class
			'api' => 'CApiService',

			// specific API classes
			'action' => 'CAction',
			'alert' => 'CAlert',
			'apiinfo' => 'CAPIInfo',
			'application' => 'CApplication',
			'autoregistration' => 'CAutoregistration',
			'configuration' => 'CConfiguration',
			'correlation' => 'CCorrelation',
			'dashboard' => 'CDashboard',
			'dcheck' => 'CDCheck',
			'dhost' => 'CDHost',
			'discoveryrule' => 'CDiscoveryRule',
			'drule' => 'CDRule',
			'dservice' => 'CDService',
			'event' => 'CEvent',
			'graph' => 'CGraph',
			'graphitem' => 'CGraphItem',
			'graphprototype' => 'CGraphPrototype',
			'host' => 'CHost',
			'hostgroup' => 'CHostGroup',
			'hostprototype' => 'CHostPrototype',
			'history' => 'CHistory',
			'hostinterface' => 'CHostInterface',
			'httptest' => 'CHttpTest',
			'image' => 'CImage',
			'iconmap' => 'CIconMap',
			'item' => 'CItem',
			'itemprototype' => 'CItemPrototype',
			'maintenance' => 'CMaintenance',
			'map' => 'CMap',
			'mediatype' => 'CMediatype',
			'problem' => 'CProblem',
			'proxy' => 'CProxy',
			'service' => 'CService',
			'screen' => 'CScreen',
			'screenitem' => 'CScreenItem',
			'script' => 'CScript',
			'task' => 'CTask',
			'template' => 'CTemplate',
			'templatescreen' => 'CTemplateScreen',
			'templatescreenitem' => 'CTemplateScreenItem',
			'trend' => 'CTrend',
			'trigger' => 'CTrigger',
			'triggerprototype' => 'CTriggerPrototype',
			'user' => 'CUser',
			'usergroup' => 'CUserGroup',
			'usermacro' => 'CUserMacro',
			'valuemap' => 'CValueMap'
		], $objects));
	}
}
