<?php



class CControllerPopupServices extends CController {

	protected function init() {
		$this->disableSIDvalidation();
	}

	protected function checkInput() {
		$fields = [
			'serviceid' =>	'db services.serviceid',
			'pservices' =>	'in 1',
			'cservices' =>	'in 1',
			'parentid' =>	'db services.serviceid'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$output = [];
			if (($messages = getMessages()) !== null) {
				$output['errors'] = $messages->toString();
			}

			$this->setResponse(
				(new CControllerResponseData(['main_block' => CJs::encodeJson($output)]))->disableView()
			);
		}

		return $ret;
	}

	protected function checkPermissions() {
		if ($this->getInput('serviceid', null)) {
			$service = API::Service()->get([
				'output' => [],
				'serviceids' => $this->getInput('serviceid')
			]);

			if (!$service) {
				return false;
			}
		}

		return true;
	}

	protected function doAction() {
		// Select service.
		if ($this->getInput('serviceid', null)) {
			$service = API::Service()->get([
				'output' => ['serviceid', 'name'],
				'serviceids' => $this->getInput('serviceid')
			]);

			$service = reset($service);
		}
		else {
			$service = null;
		}

		// Get data for parent services list.
		if ($this->getInput('pservices', null)) {
			$data = [
				'title' => _('Service parent'),
				'parentid' => $this->getInput('parentid', 0)
			];

			$parent_services = API::Service()->get([
				'output' => ['serviceid', 'name', 'algorithm'],
				'selectTrigger' => ['description'],
				'preservekeys' => true,
				'sortfield' => ['name']
			]);

			if ($service) {
				// Unset unavailable parents.
				$child_servicesids = get_service_children($service['serviceid']);
				$child_servicesids[] = $service['serviceid'];
				foreach ($child_servicesids as $child_servicesid) {
					unset($parent_services[$child_servicesid]);
				}

				$data += ['service' => $service];
			}

			foreach ($parent_services as &$parent_service) {
				$parent_service['trigger'] = $parent_service['trigger']
					? $parent_service['trigger']['description']
					: '';
			}
			unset($parent_service);

			$data['db_pservices'] = $parent_services;
		}
		// Get data for child services list.
		elseif ($this->getInput('cservices', null)) {
			$data = [
				'title' => _('Service dependencies'),
				'parentid' => $this->getInput('parentid', 0)
			];

			$child_services = API::Service()->get([
				'output' => ['serviceid', 'name', 'algorithm'],
				'selectTrigger' => ['description'],
				'preservekeys' => true,
				'sortfield' => ['name'],
			]);

			if ($service) {
				// Unset unavailable parents.
				$child_servicesids = get_service_children($service['serviceid']);
				$child_servicesids[] = $service['serviceid'];
				foreach ($child_servicesids as $child_serviceid) {
					unset($child_services[$child_serviceid]);
				}

				$data += ['service' => $service];
			}

			foreach ($child_services as &$child_service) {
				$child_service['trigger'] = $child_service['trigger'] ? $child_service['trigger']['description'] : '';
			}
			unset($child_service);

			$data['db_cservices'] = $child_services;
		}

		$data['user']['debug_mode'] = $this->getDebugMode();

		$this->setResponse(new CControllerResponseData($data));
	}
}
