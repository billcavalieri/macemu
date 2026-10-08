#include "Processes.r"

/* Background-only, so it has no menu bar and no window, and it is told about Apple events (the quit at shutdown). */
resource 'SIZE' (-1) {
	reserved,
	ignoreSuspendResumeEvents,
	reserved,
	canBackground,
	doesActivateOnFGSwitch,
	onlyBackground,
	dontGetFrontClicks,
	ignoreChildDiedEvents,
	is32BitCompatible,
	isHighLevelEventAware,
	onlyLocalHLEvents,
	notStationeryAware,
	dontUseTextEditServices,
	reserved,
	reserved,
	reserved,
	128 * 1024,
	96 * 1024
};
