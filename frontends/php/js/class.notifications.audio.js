


/**
 * Timeout controlled player.
 *
 * It plays, meanwhile decrementing timeout. Pausing and playing is done by control of 'volume' and 'muted' properties.
 * It holds infinite loop, so it allows us easily adjust timeout during playback.
 */
function TRX_NotificationsAudio() {
	this.audio = new Audio();

	this.audio.volume = 0;
	this.audio.muted = true;
	this.audio.autoplay = true;
	this.audio.loop = true;

	this.audio.onloadeddata = this.handleOnloadeddata.bind(this);

	this.audio.load();

	this.wave = '';
	this.ms_timeout = 0;
	this.is_playing = false;
	this.message_timeout = 0;
	this.callback = null;

	this.resetPromise();
	this.listen();
}

/**
 * Starts main loop.
 *
 * @return int  Interval ID.
 */
TRX_NotificationsAudio.prototype.listen = function() {
	var ms_step = 10;

	function resolveAudioState() {
		if (this.play_once_on_ready) {
			return this.once();
		}

		this.ms_timeout -= ms_step;
		this.is_playing = (this.ms_timeout > 0.0001);
		this.audio.volume = this.is_playing ? 1 : 0;

		if (this.ms_timeout < 0.0001) {
			this._resolve_timeout(this);
			this.ms_timeout = 0;
			this.seek(0);

			if (this.callback !== null) {
				this.callback();
				this.callback = null;
			}
		}
	}

	resolveAudioState.call(this);

	return setInterval(resolveAudioState.bind(this), ms_step);
};

/**
 * File is applied only if it is different than on instate, so this method may be called repeatedly, and will not
 * interrupt playback.
 *
 * @param {string} file  Audio file path relative to DOCUMENT_ROOT/audio/ directory.
 *
 * @return {TRX_NotificationsAudio}
 */
TRX_NotificationsAudio.prototype.file = function(file) {
	if (this.wave == file) {
		return this;
	}

	this.wave = file;
	this.seek(0);

	if (!this.wave) {
		this.audio.removeAttribute('src');
	}
	else {
		this.audio.src = 'audio/' + this.wave;
	}

	return this;
};

/**
 * Sets player seek position. There are no safety checks, if one decides to seek out of audio file bounds - no audio.
 *
 * @param {number} seconds
 *
 * @return {TRX_NotificationsAudio}
 */
TRX_NotificationsAudio.prototype.seek = function(seconds) {
	if (this.audio.readyState > 0) {
		this.audio.currentTime = seconds;
	}

	return this;
};

/**
 * Once file duration is known, this method seeks player to the beginning and sets timeout equal to file duration.
 *
 * @return {Promise}
 */
TRX_NotificationsAudio.prototype.once = function() {
	if (this.play_once_on_ready && this.audio.readyState >= 3) {
		this.play_once_on_ready = false;

		var timeout = (this.message_timeout == 0)
			? this.audio.duration
			: Math.min(this.message_timeout, this.audio.duration);

		return this.timeout(timeout);
	}

	this.play_once_on_ready = true;

	return this.resetPromise();
};

/**
 * An alias method. Player is stopped by exhausting timeout.
 *
 * @return {TRX_NotificationsAudio}
 */
TRX_NotificationsAudio.prototype.stop = function() {
	this.ms_timeout = 0;
	this.is_playing = false;

	return this;
};

/**
 * Mute player.
 *
 * @return {TRX_NotificationsAudio}
 */
TRX_NotificationsAudio.prototype.mute = function() {
	this.audio.muted = true;

	return this;
};

/**
 * Unmute player.
 *
 * @return {TRX_NotificationsAudio}
 */
TRX_NotificationsAudio.prototype.unmute = function() {
	this.audio.muted = false;

	return this;
};

/**
 * Tune player.
 *
 * @argument {array}   options
 * @argument {bool}    options[playOnce]        Player will not play in the loop if set to true.
 * @argument {number}  options[messageTimeout]  Message display timeout. Used to avoid playing when message box is gone.
 *
 * @return {TRX_NotificationsAudio}
 */
TRX_NotificationsAudio.prototype.tune = function(options) {
	if (typeof options.playOnce === 'boolean') {
		this.audio.loop = !options.playOnce;
	}

	if (typeof options.messageTimeout === 'number') {
		this.message_timeout = options.messageTimeout;
	}

	if (typeof options.callback !== 'undefined') {
		this.callback = options.callback;
	}

	return this;
};

/**
 * Assigns new promise property in place, any pending promise will not be resolved.
 *
 * @return {Promise}
 */
TRX_NotificationsAudio.prototype.resetPromise = function() {
	this.timeout_promise = new Promise(function(resolve, reject) {
		this._resolve_timeout = resolve;
	}.bind(this));

	return this.timeout_promise;
};

/**
 * Will play in loop for seconds given, since this call. If "0" given - will just not play. If "-1" is given - file will
 * be played once.
 *
 * @param {number} seconds
 *
 * @return {Promise}
 */
TRX_NotificationsAudio.prototype.timeout = function(seconds) {
	if (this.message_timeout == 0) {
		this.stop();
		return this.resetPromise();
	}

	if (!this.audio.loop) {
		if (seconds == TRX_Notifications.ALARM_ONCE_PLAYER) {
			return this.once();
		}
		else if (this.is_playing) {
			return this.timeout_promise;
		}
		else {
			this.audio.load();
		}
	}

	this.ms_timeout = seconds * 1000;

	return this.resetPromise();
};

/**
 * Get current player seek position.
 *
 * @return {float}  Amount of seconds.
 */
TRX_NotificationsAudio.prototype.getSeek = function() {
	return this.audio.currentTime;
};

/**
 * Get the time player will play for.
 *
 * @return {float}  Amount of seconds.
 */
TRX_NotificationsAudio.prototype.getTimeout = function() {
	return this.ms_timeout / 1000;
};

/**
 * This handler will be invoked once audio file has successfully pre-loaded. Attempt to auto play and see, if auto play
 * policy error occurs.
 */
TRX_NotificationsAudio.prototype.handleOnloadeddata = function() {
	var promise = this.audio.play();

	// Internet explorer does not return promise.
	if (typeof promise === 'undefined') {
		return;
	}

	promise.catch(function (error) {
		if (error.name === 'NotAllowedError' && this.audio.paused) {
			console.warn(error.message);
			console.warn(
				'Treegix was not able to play audio due to "Autoplay policy". Please see manual for more information.'
			);
		}
	}.bind(this));
};
