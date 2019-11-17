


ZBX_Promise.PENDING = 0;
ZBX_Promise.RESOLVED = 1;
ZBX_Promise.REJECTED = 2;

/**
 * Promise API polyfill. This polyfill contains only things that we use.
 * Please update this once richer Promises API needs to be used.
 *
 * Warning: method chaining is not implemented.
 *
 * @param {callable} resolver
 */
function ZBX_Promise(resolver) {
	this.state = ZBX_Promise.PENDING;
	this.onResolve = function() {};
	this.onReject = function() {};

	resolver(this.resolve.bind(this), this.reject.bind(this));
}

/**
 * @param {mixed} result
 */
ZBX_Promise.prototype.reject = function(result) {
	this.state = ZBX_Promise.REJECTED;
	this.onReject(result);
};

/**
 * @param {mixed} result
 */
ZBX_Promise.prototype.resolve = function(result) {
	this.state = ZBX_Promise.RESOLVED;
	this.onResolve(result);
};

/**
 * @param {callable} closure
 */
ZBX_Promise.prototype.catch = function(closure) {
	this.onReject = closure;

	return this;
};

/**
 * @param {callable} closure
 */
ZBX_Promise.prototype.then = function(closure) {
	this.onResolve = closure;

	return this;
};

if (!window.Promise) {
	window.Promise = ZBX_Promise;
}
