// $Id$

var varnish = {
	rebuild: function() {
	}
};

function varnish_register()
{
	window.getBrowser().addProgressListener(varnish_progress_listener);
}

function varnish_unregister()
{
	window.getBrowser().removeProgressListener(varnish_progress_listener);
}

window.addEventListener("load", varnish_register, false);
window.addEventListener("unload", varnish_unregister, false);

var varnish_progress_listener = {
	onLocationChange: function(webProgress, request, location) {
	},

	onProgressChange: function(webProgress, request, curSelfProgress,
	    maxSelfProgress, curTotalProgress, maxTotalProgress) {
	},

	onSecurityChange: function(webProgress, request, state) {
	},

	onStateChange: function(webProgress, request, stateFlags, status) {
	},

	onStatusChange: function(webProgress, request, status, message) {
	},
};
