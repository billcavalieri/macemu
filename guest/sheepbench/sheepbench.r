/* SheepBench resources: menus, the main window and the partition. */

#include "Processes.r"
#include "Dialogs.r"
#include "Menus.r"
#include "Types.r"

resource 'MBAR' (128) {
	{ 128, 129, 130 };
};

resource 'MENU' (128) {
	128, textMenuProc, 0x7FFFFFFD, enabled, apple,
	{
		"About SheepBench…", noIcon, noKey, noMark, plain;
		"-", noIcon, noKey, noMark, plain;
	}
};

resource 'MENU' (129) {
	129, textMenuProc, 0x7FFFFFFF, enabled, "File",
	{
		"Run", noIcon, "R", noMark, plain;
		"Show Results", noIcon, noKey, noMark, plain;
		"Save Report…", noIcon, "S", noMark, plain;
		"Compare Reports…", noIcon, noKey, noMark, plain;
		"-", noIcon, noKey, noMark, plain;
		"Close Window", noIcon, "W", noMark, plain;
		"Quit", noIcon, "Q", noMark, plain;
	}
};

resource 'MENU' (130) {
	130, textMenuProc, 0, enabled, "Edit",
	{
		"Undo", noIcon, "Z", noMark, plain;
		"-", noIcon, noKey, noMark, plain;
		"Cut", noIcon, "X", noMark, plain;
		"Copy", noIcon, "C", noMark, plain;
		"Paste", noIcon, "V", noMark, plain;
		"Clear", noIcon, noKey, noMark, plain;
	}
};

/* The main window. Item numbers are in main.c (enum MainItems). */
resource 'DLOG' (128) {
	{44, 12, 344, 452},
	noGrowDocProc,
	visible,
	goAway,
	0x0,
	128,
	"SheepBench",
	noAutoCenter
};

resource 'DITL' (128) {
	{
		/* 1 */ {262, 344, 282, 424}, Button { enabled, "Run" };
		/* 2 */ {262, 252, 282, 332}, Button { enabled, "Stop" };
		/* 3 */ {36, 20, 54, 200}, CheckBox { enabled, "PowerPC CPU" };
		/* 4 */ {56, 20, 74, 200}, CheckBox { enabled, "68k CPU" };
		/* 5 */ {76, 20, 94, 200}, CheckBox { enabled, "Memory" };
		/* 6 */ {96, 20, 114, 200}, CheckBox { enabled, "MMU" };
		/* 7 */ {36, 220, 54, 420}, CheckBox { enabled, "QuickDraw" };
		/* 8 */ {56, 220, 74, 420}, CheckBox { enabled, "3D (RAVE, QuickDraw 3D)" };
		/* 9 */ {76, 220, 94, 420}, CheckBox { enabled, "Disk" };
		/* 10 */ {142, 20, 160, 420}, CheckBox { enabled, "Also run with the JIT off (speedup column)" };
		/* 11 */ {162, 20, 180, 420}, CheckBox { enabled, "Also run with SheepForce QuickDraw off (GPU column)" };
		/* 12 */ {214, 20, 226, 424}, UserItem { disabled };
		/* 13 */ {232, 20, 250, 424}, StaticText { disabled, "" };
		/* 14 */ {12, 20, 30, 424}, StaticText { disabled, "Suites:" };
		/* 15 */ {122, 20, 140, 424}, StaticText { disabled, "Comparisons:" };
		/* 16 */ {182, 20, 200, 420}, CheckBox { enabled, "Quick run (fewer repetitions, less precise)" };
		/* 17 */ {262, 20, 282, 240}, StaticText { disabled, "" };
	}
};

resource 'SIZE' (-1) {
	reserved,
	acceptSuspendResumeEvents,
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
	24 * 1024 * 1024,
	8 * 1024 * 1024
};
