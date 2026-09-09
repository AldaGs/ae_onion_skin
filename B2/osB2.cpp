/*
	osB2.cpp (Onion Skin) - Phase 0, spike B2.

	WHAT THIS DECIDES

	Option B's premise is about REACH: the user changes onion-skin settings
	without selecting the onion-skin layer. Only an AEGP panel can do that -
	Effect Controls shows the selected layer's effects and nothing else, and
	Custom Comp UI events arrive only while the effect is selected. So the panel
	is not a nicety here, it is the mechanism, and it is measured first.

	B2 asks four things, all of which are about EXISTENCE and REACH, none of
	which are about onion skinning:

	  1. Does a registered panel appear under Window and dock like a real panel?
	  2. Does it survive quitting and relaunching AE, and a workspace switch?
	  3. Does a click reach us while a DIFFERENT layer is selected?
	  4. Does a click reach us while NO layer is selected?

	3 and 4 are the ones that matter. A panel that only responds when the right
	layer is selected has not removed the friction it exists to remove.

	HOW IT IS CHECKED

	Every interesting event appends one timestamped line to %TEMP%\onionskin_B2.txt.
	The log is APPENDED, never truncated, because question 2 is entirely about
	what happens across launches - a log that resets each startup cannot answer it.

	Each LIVE click also records the active layer, read but never written. That
	is what turns "the button worked" into "the button worked with nothing
	selected", which is the actual claim.

	THE BROKEN CONTROL

	There are two buttons. LIVE is wired. DEAD is created the same way, sits
	beside it, and is deliberately absent from the WM_COMMAND switch. If DEAD
	produces a log line, the handler is firing on any click in the panel and
	every LIVE line is worthless. Judge the run by both.

	WHAT THIS DOES NOT DO

	No params are written (that is B3). Nothing is drawn but a flat rectangle and
	some text. There is no onion-skin logic here at all, by design: if the gate
	fails, the loss is this file.
*/

#include "osB2.h"

static AEGP_PluginID		S_my_id			= 0L;
static SPBasicSuite			*sP				= NULL;
static AEGP_Command			S_command		= 0L;
static AEGP_PanelSuite1		*S_panelP		= NULL;
static char					S_log_path[AEGP_MAX_PATH_SIZE] = {'\0'};

//	Modelled on Persisto, not Commando: Commando never assigns its plugin id and
//	so registers every hook under id 0. Everything below needs a real one.

static const A_u_char *S_match_nameZ =
	reinterpret_cast<const A_u_char *>(OS_B2_MATCH_NAME);

/* ------------------------------------------------------------------ */
/*  Log                                                                */
/* ------------------------------------------------------------------ */

//	One line per event, appended. Every line carries wall-clock time AND the
//	process's own tick count: wall clock is how a human correlates a line with
//	what they were doing, and the tick count is how we tell one AE session from
//	the next, because it restarts near zero on relaunch. Question 2 is read off
//	that reset.

static void
ResolveLogPath()
{
	const char *tmp = getenv("TEMP");
	if (!tmp) tmp = getenv("TMP");
	if (!tmp) tmp = ".";
	sprintf(S_log_path, "%s\\%s", tmp, OS_B2_LOG_LEAF);
}

static void
Log(const char *fmt, ...)
{
	if (!S_log_path[0]) return;

	FILE *f = fopen(S_log_path, "a");		// append: see the header comment
	if (!f) return;

	time_t		now = time(NULL);
	struct tm	*lt = localtime(&now);
	char		stamp[32] = {'\0'};

	if (lt) {
		strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", lt);
	}

	fprintf(f, "[%s  t=%8lu]  ", stamp, (unsigned long)GetTickCount());

	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);

	fprintf(f, "\n");
	fclose(f);
}

/* ------------------------------------------------------------------ */
/*  Selection, read only                                               */
/* ------------------------------------------------------------------ */

//	AEGP_GetActiveLayer returns non-NULL only when exactly ONE layer is selected.
//	That is precisely the distinction B2 needs, so no extra work is required to
//	tell "one layer" from "none or several" - but it does mean a multi-selection
//	logs as "none", so the description says so rather than claiming more than the
//	call returns.

static void
DescribeSelection(char *outZ, size_t outN)
{
	AEGP_SuiteHandler	suites(sP);
	AEGP_LayerH			layerH = NULL;
	A_Err				err = A_Err_NONE;

	err = suites.LayerSuite9()->AEGP_GetActiveLayer(&layerH);

	if (err) {
		sprintf(outZ, "selection=<query failed, err %d>", (int)err);
		return;
	}
	if (!layerH) {
		//	No single active layer. Either nothing is selected, or several are.
		sprintf(outZ, "selection=none-or-multiple");
		return;
	}

	A_long index = -1;
	if (suites.LayerSuite9()->AEGP_GetLayerIndex(layerH, &index)) {
		sprintf(outZ, "selection=one layer, index unavailable");
	} else {
		sprintf(outZ, "selection=one layer, index %d", (int)index);
	}
}

/* ------------------------------------------------------------------ */
/*  Panel                                                              */
/* ------------------------------------------------------------------ */

//	AE hands over a bare platform window and stops. Everything below - the
//	subclassed wndproc, the two buttons, the painting - is ours. That is the
//	honest cost of an AE panel and the reason B4 exists to measure it.

static const char *S_prop_nameZ = "OnionSkinB2Panel";

class OSB2Panel
{
public:
	OSB2Panel(AEGP_PanelH panelH, AEGP_PlatformViewRef container,
				AEGP_PanelFunctions1 *outFunctionTable)
		: i_panelH(panelH), i_hwnd(container),
		  i_live_clicks(0), i_dead_clicks_seen(0)
	{
		//	Subclass AE's window so we see its messages, keeping the old proc to
		//	chain anything we do not handle. Dropping that chain is how a panel
		//	stops resizing and repainting correctly.
		i_prev_proc = (WNDPROC)SetWindowLongPtrA(i_hwnd, GWLP_WNDPROC,
													(LONG_PTR)OSB2Panel::S_WndProc);
		::SetPropA(i_hwnd, S_prop_nameZ, (HANDLE)this);

		CreateWindowA("BUTTON", "Live button",
						WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
						10, 60, 120, 26,
						i_hwnd, (HMENU)(INT_PTR)OS_B2_BTN_LIVE, NULL, NULL);

		//	The broken control. Identical construction, wired to nothing.
		CreateWindowA("BUTTON", "Dead button",
						WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
						140, 60, 120, 26,
						i_hwnd, (HMENU)(INT_PTR)OS_B2_BTN_DEAD, NULL, NULL);

		//	AE requires these three to be filled in even when they do nothing.
		outFunctionTable->DoFlyoutCommand	= S_DoFlyoutCommand;
		outFunctionTable->GetSnapSizes		= S_GetSnapSizes;
		outFunctionTable->PopulateFlyout	= S_PopulateFlyout;

		Log("PANEL   created. hwnd=%p", (void *)i_hwnd);
	}

	void Invalidate()
	{
		RECT r;
		GetClientRect(i_hwnd, &r);
		InvalidateRect(i_hwnd, &r, FALSE);
	}

private:
	AEGP_PanelH			i_panelH;
	HWND				i_hwnd;
	WNDPROC				i_prev_proc;
	A_long				i_live_clicks;
	A_long				i_dead_clicks_seen;	// stays 0; see OnCommand

	static LRESULT CALLBACK S_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		OSB2Panel *selfP = reinterpret_cast<OSB2Panel *>(::GetPropA(hwnd, S_prop_nameZ));
		if (selfP) {
			return selfP->WndProc(hwnd, msg, wp, lp);
		}
		return DefWindowProc(hwnd, msg, wp, lp);
	}

	LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		bool handledB = false;

		switch (msg) {
			case WM_PAINT:
				OnPaint(hwnd);
				handledB = true;
				break;

			case WM_SIZE:
				Invalidate();
				break;

			case WM_COMMAND:
				if (HIWORD(wp) == BN_CLICKED) {
					handledB = OnCommand(LOWORD(wp));
				}
				break;
		}

		if (i_prev_proc && !handledB) {
			return CallWindowProc(i_prev_proc, hwnd, msg, wp, lp);
		}
		return handledB ? 0 : DefWindowProc(hwnd, msg, wp, lp);
	}

	bool OnCommand(int id)
	{
		//	OS_B2_BTN_DEAD is absent from this switch on purpose. If a DEAD click
		//	ever reaches the log, something upstream is dispatching every click
		//	to the live path and the whole run is void.
		if (id == OS_B2_BTN_LIVE) {
			char sel[256] = {'\0'};
			DescribeSelection(sel, sizeof(sel));

			i_live_clicks++;
			Log("CLICK   LIVE  #%d  %s", (int)i_live_clicks, sel);

			Invalidate();
			return true;
		}
		return false;
	}

	void OnPaint(HWND hwnd)
	{
		PAINTSTRUCT	ps;
		HDC			hdc = BeginPaint(hwnd, &ps);
		RECT		client;

		GetClientRect(hwnd, &client);

		//	Flat fill in AE's own panel colour, so a working panel looks docked
		//	rather than looking like a hole. PF_AppGetColor returns 16-bit
		//	channels; Win32 wants 8.
		AEGP_SuiteHandler	suites(sP);
		PF_App_Color		bg = {0};
		HBRUSH				brush = NULL;

		if (!suites.AppSuite4()->PF_AppGetColor(PF_App_Color_PANEL_BACKGROUND, &bg)) {
			brush = CreateSolidBrush(RGB(bg.red / 255, bg.green / 255, bg.blue / 255));
		} else {
			brush = CreateSolidBrush(RGB(48, 48, 48));
		}
		FillRect(hdc, &client, brush);
		DeleteObject(brush);

		//	The hardcoded rectangle the spike promised: proof that OUR drawing
		//	code, not AE's background fill, is reaching the screen.
		RECT mark = client;
		mark.left	+= 10;
		mark.top	+= 10;
		mark.right	 = mark.left + 100;
		mark.bottom	 = mark.top + 30;
		brush = CreateSolidBrush(RGB(220, 120, 40));
		FillRect(hdc, &mark, brush);
		DeleteObject(brush);

		char line[256] = {'\0'};
		sprintf(line, "Onion Skin B2 - live clicks: %d", (int)i_live_clicks);

		RECT text = client;
		text.left	+= 10;
		text.top	 = 100;
		text.bottom	 = text.top + 24;

		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, RGB(230, 230, 230));
		DrawTextA(hdc, line, (int)strlen(line), &text, DT_SINGLELINE | DT_LEFT);

		EndPaint(hwnd, &ps);
	}

	static A_Err S_GetSnapSizes(AEGP_PanelRefcon /*refcon*/, A_LPoint *snapSizes, A_long *numSizesP)
	{
		snapSizes[0].x = 300;	snapSizes[0].y = 150;
		snapSizes[1].x = 400;	snapSizes[1].y = 300;
		*numSizesP = 2;
		return A_Err_NONE;
	}

	static A_Err S_PopulateFlyout(AEGP_PanelRefcon /*refcon*/, AEGP_FlyoutMenuItem * /*itemsP*/, A_long *in_out_numItemsP)
	{
		*in_out_numItemsP = 0;
		return A_Err_NONE;
	}

	static A_Err S_DoFlyoutCommand(AEGP_PanelRefcon /*refcon*/, AEGP_FlyoutMenuCmdID /*commandID*/)
	{
		return A_Err_NONE;
	}
};

/* ------------------------------------------------------------------ */
/*  Hooks                                                              */
/* ------------------------------------------------------------------ */

static A_Err
CreatePanelHook(
	AEGP_GlobalRefcon		/*plugin_refconP*/,
	AEGP_CreatePanelRefcon	/*refconP*/,
	AEGP_PlatformViewRef	container,
	AEGP_PanelH				panelH,
	AEGP_PanelFunctions1	*outFunctionTable,
	AEGP_PanelRefcon		*outRefcon)
{
	*outRefcon = reinterpret_cast<AEGP_PanelRefcon>(
					new OSB2Panel(panelH, container, outFunctionTable));
	return A_Err_NONE;
}

static A_Err
CommandHook(
	AEGP_GlobalRefcon	/*plugin_refconP*/,
	AEGP_CommandRefcon	/*refconP*/,
	AEGP_Command		command,
	AEGP_HookPriority	/*hook_priority*/,
	A_Boolean			/*already_handledB*/,
	A_Boolean			*handledPB)
{
	if (command == S_command && S_panelP) {
		Log("MENU    toggle requested");
		S_panelP->AEGP_ToggleVisibility(S_match_nameZ);
		*handledPB = TRUE;
	}
	return A_Err_NONE;
}

static A_Err
UpdateMenuHook(
	AEGP_GlobalRefcon		/*plugin_refconP*/,
	AEGP_UpdateMenuRefcon	/*refconP*/,
	AEGP_WindowType			/*active_window*/)
{
	AEGP_SuiteHandler	suites(sP);

	//	Enabled unconditionally. If this were conditional on a selection, the
	//	spike would be answering a question it had itself constrained.
	suites.CommandSuite1()->AEGP_EnableCommand(S_command);

	if (S_panelP) {
		A_Boolean shownB = FALSE, frontB = FALSE;
		if (!S_panelP->AEGP_IsShown(S_match_nameZ, &shownB, &frontB)) {
			suites.CommandSuite1()->AEGP_CheckMarkMenuCommand(S_command, shownB && frontB);
		}
	}
	return A_Err_NONE;
}

/* ------------------------------------------------------------------ */
/*  Entry                                                              */
/* ------------------------------------------------------------------ */

A_Err
EntryPointFunc(
	struct SPBasicSuite		*pica_basicP,
	A_long					major_versionL,
	A_long					minor_versionL,
	AEGP_PluginID			aegp_plugin_id,
	AEGP_GlobalRefcon		*global_refconP)
{
	A_Err err = A_Err_NONE;

	sP		= pica_basicP;
	S_my_id	= aegp_plugin_id;

	ResolveLogPath();

	//	This line is the restart evidence. Its tick count starts near zero on a
	//	fresh launch, so a second STARTUP with a small t is a new AE session and
	//	everything after it is post-restart behaviour.
	Log("STARTUP AE %d.%d, plugin id %d", (int)major_versionL, (int)minor_versionL, (int)S_my_id);

	AEGP_SuiteHandler suites(sP);

	err = sP->AcquireSuite(kAEGPPanelSuite, kAEGPPanelSuiteVersion1,
							(const void **)&S_panelP);
	if (err || !S_panelP) {
		//	A hard, early answer to B2: no panel suite, no panel, no Option B
		//	panel product. Say so loudly rather than failing silently at toggle
		//	time, where it would look like a menu bug.
		Log("FAIL    could not acquire AEGP_PanelSuite1 (err %d)", (int)err);
		suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id,
			"Onion Skin B2: AEGP_PanelSuite1 unavailable. B2 fails.");
		return err ? err : A_Err_GENERIC;
	}

	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_command));
	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_command, OS_B2_MENU_NAME,
														AEGP_Menu_WINDOW,
														AEGP_MENU_INSERT_SORTED));

	ERR(suites.RegisterSuite5()->AEGP_RegisterCommandHook(S_my_id, AEGP_HP_BeforeAE,
															S_command, CommandHook, NULL));
	ERR(suites.RegisterSuite5()->AEGP_RegisterUpdateMenuHook(S_my_id, UpdateMenuHook, NULL));

	//	in_paint_backgroundB = true: AE paints the panel background before our
	//	WM_PAINT. We paint over it anyway, so this only affects the first frame.
	ERR(S_panelP->AEGP_RegisterCreatePanelHook(S_my_id, S_match_nameZ,
												CreatePanelHook, NULL, true));

	if (err) {
		Log("FAIL    registration failed, err %d", (int)err);
	} else {
		Log("READY   panel registered as \"%s\"", OS_B2_MATCH_NAME);
	}

	*global_refconP = NULL;
	return err;
}
