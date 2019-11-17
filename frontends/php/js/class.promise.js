


TRX_Promise.PENDING = 0;
TRX_Promise.RESOLVED = 1;
TRX_Promise.REJECTED = 2;

/**
 * Promise API polyfill. This polyfill contains only things that we use.
 * Please update this once richer Promises API needs to be used.
 *
 * Warning: method chaining is not implemented.
 *
 * @param {callable} resolver
 */
function TRX_Promise(resolver) {
	this.state = TRX_Promise.PENDING;
	this.onResolve = function() {};
	this.onReject = function() {};

	resolver(this.resolve.bind(this), this.reject.bind(this));
}

/**
 * @param {mixed} result
 */
TRX_Promise.prototype.reject = function(result) {
	this.state = TRX_Promise.REJECTED;
	this.onReject(result);
};

/**
 * @param {mixed} result
 */
TRX_Promise.prototype.resolve = function(result) {
	this.state = TRX_Promise.RESOLVED;
	this.onResolve(result);
};

/**
 * @param {callable} closure
 */
TRX_Promise.prototype.catch = function(closure) {
	this.onReject = closure;

	return this;
};

/**
 * @param {callable} closure
 */
TRX_Promise.prototype.then = function(closure) {
	this.onResolve = closure;

	return this;
};

if (!window.Promise) {
	window.Promise = TRX_Promise;
}
