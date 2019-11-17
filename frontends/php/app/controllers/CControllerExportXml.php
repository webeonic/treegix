<?php



class CControllerExportXml extends CController {

	protected function checkInput() {
		$fields = [
			'action' => 'required|string',
			'backurl' => 'required|string'
		];

		switch (getRequest('action')) {
			case 'export.valuemaps.xml':
				$fields['valuemapids'] = 'required|array_db valuemaps.valuemapid';
				break;

			case 'export.hosts.xml':
				$fields['hosts'] = 'required|array_db hosts.hostid';
				break;

			case 'export.mediatypes.xml':
				$fields['mediatypeids'] = 'required|array_db media_type.mediatypeid';
				break;

			case 'export.screens.xml':
				$fields['screens'] = 'required|array_db screens.screenid';
				break;

			case 'export.sysmaps.xml':
				$fields['maps'] = 'required|array_db sysmaps.sysmapid';
				break;

			case 'export.templates.xml':
				$fields['templates'] = 'required|array_db hosts.hostid';
				break;

			default:
				$this->setResponse(new CControllerResponseFatal());

				return false;
		}

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		switch ($this->getInput('action')) {
			case 'export.mediatypes.xml':
			case 'export.valuemaps.xml':
				return (CWebUser::$data['type'] >= USER_TYPE_SUPER_ADMIN);

			case 'export.hosts.xml':
			case 'export.templates.xml':
				return (CWebUser::$data['type'] >= USER_TYPE_TREEGIX_ADMIN);

			case 'export.screens.xml':
			case 'export.sysmaps.xml':
				return (CWebUser::$data['type'] >= USER_TYPE_TREEGIX_USER);

			default:
				return false;
		}
	}

	protected function doAction() {
		$action = $this->getInput('action');

		switch ($action) {
			case 'export.valuemaps.xml':
				$export = new CConfigurationExport(['valueMaps' => $this->getInput('valuemapids', [])]);
				break;

			case 'export.hosts.xml':
				$export = new CConfigurationExport(['hosts' => $this->getInput('hosts', [])]);
				break;

			case 'export.mediatypes.xml':
				$export = new CConfigurationExport(['mediaTypes' => $this->getInput('mediatypeids', [])]);
				break;

			case 'export.screens.xml':
				$export = new CConfigurationExport(['screens' => $this->getInput('screens', [])]);
				break;

			case 'export.sysmaps.xml':
				$export = new CConfigurationExport(['maps' => $this->getInput('maps', [])]);
				break;

			case 'export.templates.xml':
				$export = new CConfigurationExport(['templates' => $this->getInput('templates', [])]);
				break;

			default:
				$this->setResponse(new CControllerResponseFatal());

				return;
		}

		$export->setBuilder(new CConfigurationExportBuilder());
		$export->setWriter(CExportWriterFactory::getWriter(CExportWriterFactory::XML));

		$export_data = $export->export();

		if ($export_data === false) {
			// Access denied.

			$response = new CControllerResponseRedirect(
				$this->getInput('backurl', 'treegix.php?action=dashboard.view'));

			$response->setMessageError(_('No permissions to referred object or it does not exist!'));
		}
		else {
			$response = new CControllerResponseData([
				'main_block' => $export_data,
				'page' => ['file' => 'zbx_export_' . substr($action, 7)]
			]);
		}

		$this->setResponse($response);
	}
}
