


/**
 * Amount of seconds for keep-alive interval.
 */
TRX_BrowserTab.keep_alive_interval = 30;

/**
 * This object is representing a browser tab. Implements singleton pattern. It ensures there are only non-crashed tabs
 * in store.
 *
 * @param {TRX_LocalStorage} store  A localStorage wrapper.
 */
function TRX_BrowserTab(store) {
	if (TRX_BrowserTab.instance) {
		return TRX_BrowserTab.instance;
	}

	if (!(store instanceof TRX_LocalStorage)) {
		throw 'Unmatched signature!';
	}

	TRX_BrowserTab.instance = this;

	this.uid = (Math.random() % 9e6).toString(36).substr(2);
	this.store = store;

	var ctx_lastseen = this.store.readKey('tabs.lastseen', {});
	ctx_lastseen[this.uid] = Math.floor(+new Date / 1000);

	this.lastseen = ctx_lastseen;

	this.on_focus_cbs = [];
	this.on_blur_cbs = [];
	this.on_unload_cbs = [];
	this.on_crashed_cbs = [];

	this.bindEventHandlers();
	this.pushLastseen();
}

/**
 * @param {object} lastseen
 */
TRX_BrowserTab.prototype.handlePushedLastseen = function(lastseen) {
	this.lastseen = lastseen;
};

/**
 * Checks and updates current lastseen object.
 */
TRX_BrowserTab.prototype.handleKeepAliveTick = function() {
	this.lastseen[this.uid] = Math.floor(+new Date / 1000);

	this.findCrashedTabs().forEach(function(tabid) {
		delete this.lastseen[tabid];
		this.handleCrashed(tabid);
	}.bind(this));

	this.pushLastseen();
};

/**
 * Writes own ID in `store.tabs` object. Registers unload event to remove own ID from `store.tabs.lastseen`.
 * Registers focus event. Begins a loop to see if any tab of tabs has crashed.
 */
TRX_BrowserTab.prototype.bindEventHandlers = function() {
	this.handleKeepAliveTick();
	setInterval(this.handleKeepAliveTick.bind(this), TRX_BrowserTab.keep_alive_interval * 1000);

	window.addEventListener('beforeunload', this.handleBeforeUnload.bind(this));
	window.addEventListener('focus', this.handleFocus.bind(this));
	this.store.onKeyUpdate('tabs.lastseen', this.handlePushedLastseen.bind(this));
};

/**
 * @return {array} List of IDs of crashed tabs.
 */
TRX_BrowserTab.prototype.findCrashedTabs = function() {
	var since = this.lastseen[this.uid] - (TRX_BrowserTab.keep_alive_interval - 1) * 2,
		crashed_tabids = [];

	for (var tabid in this.lastseen) {
		if (this.lastseen[tabid] < since) {
			crashed_tabids.push(tabid);
		}
	}

	return crashed_tabids;
};

/**
 * Gives all tab ids.
 *
 * @return {array}
 */
TRX_BrowserTab.prototype.getAllTabIds = function() {
	return Object.keys(this.lastseen);
};

/**
 * @param {string} tabid  The crashed tab ID.
 */
TRX_BrowserTab.prototype.handleCrashed = function(tabid) {
	this.on_crashed_cbs.forEach(function(c) {c(this);}.bind(this));
};

/**
 * @param {callable} callback
 */
TRX_BrowserTab.prototype.onFocus = function(callback) {
	this.on_focus_cbs.push(callback);
};

/**
 * @param {callable} callback
 */
TRX_BrowserTab.prototype.onCrashed = function(callback) {
	this.on_crashed_cbs.push(callback);
};

/**
 * @param {callable} callback
 */
TRX_BrowserTab.prototype.onBeforeUnload = function(callback) {
	this.on_unload_cbs.push(callback);
};

/**
 * @param {FocusEvent} e
 */
TRX_BrowserTab.prototype.handleFocus = function(e) {
	this.on_focus_cbs.forEach(function(c) {c(this);}.bind(this));
};

/**
 * @param {UnloadEvent} e
 */
TRX_BrowserTab.prototype.handleBeforeUnload = function(e) {
	delete this.lastseen[this.uid];

	this.on_unload_cbs.forEach(function(c) {c(this, this.getAllTabIds());}.bind(this));
	this.pushLastseen();

	window.removeEventListener('beforeunload', this.handleBeforeUnload.bind(this));
	window.removeEventListener('focus', this.handleFocus.bind(this));
};

/**
 * Writes to store.
 */
TRX_BrowserTab.prototype.pushLastseen = function() {
	this.store.writeKey('tabs.lastseen', this.lastseen);
};

TREEGIX.namespace('instances.browserTab', new TRX_BrowserTab(
	TREEGIX.namespace('instances.localStorage')
));
