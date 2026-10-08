#include "Processes.r"
#include "Dialogs.r"
#include "Types.r"

resource 'DLOG' (128) {
	{70, 60, 250, 420},
	movableDBoxProc,
	visible,
	noGoAway,
	0x0,
	128,
	"Sheep Shears",
	alertPositionMainScreen
};

resource 'DITL' (128) {
	{
		{144, 280, 164, 350}, Button { enabled, "Done" };
		{14, 16, 32, 350}, CheckBox { enabled, "Release the mouse at the edge of the screen" };
		{40, 16, 58, 350}, CheckBox { enabled, "Share the clipboard with the Mac" };
		{70, 16, 130, 350}, StaticText { disabled, "" };
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
	256 * 1024,
	192 * 1024
};
