<?php



class CControllerFavouriteCreate extends CController {

	protected function checkInput() {
		$fields = [
			'object' =>		'fatal|required|in graphid,itemid,screenid,slideshowid,sysmapid',
			'objectid' =>	'fatal|required|id'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseData(['main_block' => '']));
		}

		return $ret;
	}

	protected function checkPermissions() {
		return ($this->getUserType() >= USER_TYPE_TREEGIX_USER);
	}

	protected function doAction() {
		$profile = [
			'graphid' => 'web.favorite.graphids',
			'itemid' => 'web.favorite.graphids',
			'screenid' => 'web.favorite.screenids',
			'slideshowid' => 'web.favorite.screenids',
			'sysmapid' => 'web.favorite.sysmapids'
		];

		$object = $this->getInput('object');
		$objectid = $this->getInput('objectid');

		$data = [];

		DBstart();
		$result = CFavorite::add($profile[$object], $objectid, $object);
		$result = DBend($result);

		if ($result) {
			$data['main_block'] = '$("addrm_fav").title = "'._('Remove from favourites').'";'."\n".
				'$("addrm_fav").onclick = function() { rm4favorites("'.$object.'", "'.$objectid.'"); }'."\n".
				'switchElementClass("addrm_fav", "btn-add-fav", "btn-remove-fav");';
		}
		else {
			$data['main_block'] = '';
		}

		$this->setResponse(new CControllerResponseData($data));
	}
}
