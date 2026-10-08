#include "Processes.r"
#include "Dialogs.r"
#include "Types.r"

resource 'DLOG' (128) {
	{70, 60, 250, 440},
	movableDBoxProc,
	visible,
	noGoAway,
	0x0,
	128,
	"Install Sheep Shears",
	alertPositionMainScreen
};

resource 'DITL' (128) {
	{
		{146, 296, 166, 366}, Button { enabled, "Install" };
		{146, 214, 166, 284}, Button { enabled, "Quit" };
		{146, 16, 166, 106}, Button { enabled, "Remove" };
		{14, 16, 130, 366}, StaticText { disabled, "" };
	}
};

resource 'SIZE' (-1) {
	reserved,
	ignoreSuspendResumeEvents,
	reserved,
	canBackground,
	doesActivateOnFGSwitch,
	backgroundAndForeground,
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
	384 * 1024,
	256 * 1024
};
