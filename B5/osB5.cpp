/*
	osB5.cpp (Onion Skin) - Phase 0, spike B5.

	WHAT THIS DECIDES

	B2 established that a panel is reachable with nothing selected. B5 asks the
	cheaper and, for an animator, better question: can the same state be driven
	by a KEYSTROKE, so the hand never leaves the pen?

	Three commands - Toggle, More Previous, Fewer Previous - registered into the
	Animation menu. AE lists registered menu commands in its own keyboard-shortcut
	editor, so if they appear there and fire, the user can bind them.

	WHAT COUNTS AS THE ANSWER

	  1. The three commands appear under Animation.
	  2. They fire, and change the state.
	  3. They fire with NO layer selected.
	  4. They appear in Edit > Keyboard Shortcuts and can be bound.
	  5. Once bound, the KEY changes the state - not just the menu item.

	5 is the real one. A command that works from the menu but not from its
	shortcut has not saved anybody a trip.

	HOW IT IS CHECKED

	Two ways at once, on purpose, because they fail differently:

	  - the log, which is the record; and
	  - the B2 panel, carried over, which paints the state large. The claim is
	    about what the user sees when they press a key, and the standing rule
	    says the check then has to be what the user sees. A log line saying
	    "toggled" while the panel still reads OFF is a finding, not a rounding
	    error - it would mean the command fired but the state it changed was not
	    the state anything else reads.

	THE BROKEN CONTROL

	The panel keeps B2's dead button. It is still absent from the WM_COMMAND
	switch, and it must still produce nothing. It costs one line to keep and it
	guards the same failure: a handler that fires on everything.

	Additionally, MORE and FEWER are separate commands rather than one command
	with a modifier. If both were bound and only one worked, a single command
	would hide it.

	WHAT THIS DOES NOT DO

	No params are written and no effect exists yet - the state below is a plain
	bool and a plain int. B5 asks whether the COMMAND PATH reaches us. Wiring
	that path to real params is B3 and Phase 2.
*/

#include "osB5.h"

static AEGP_PluginID		S_my_id			= 0L;
static SPBasicSuite			*sP				= NULL;
static AEGP_PanelSuite1		*S_panelP		= NULL;
static char					S_log_path[AEGP_MAX_PATH_SIZE] = {'\0'};

static AEGP_Command			S_cmd_panel		= 0L;
static AEGP_Command			S_cmd_toggle	= 0L;
static AEGP_Command			S_cmd_more		= 0L;
static AEGP_Command			S_cmd_fewer		= 0L;

//	The stand-in state. Phase 2 replaces this with reads and writes of the
//	effect's param streams; nothing else about the command path changes.
static A_Boolean			S_enabledB		= FALSE;
static A_long				S_prev_frames	= 1;

//	Modelled on Persisto, not Commando: Commando never assigns its plugin id and
//	so registers every hook under id 0. Everything below needs a real one.

static const A_u_char *S_match_nameZ =
	reinterpret_cast<const A_u_char *>(OS_B5_MATCH_NAME);

/* ------------------------------------------------------------------ */
/*  Log                                                                */
/* ------------------------------------------------------------------ */

//	Appended, never truncated. Every line carries wall-clock time; unlike B2
//	there is no tick count, because B2 proved that clock counts from system boot
//	and so cannot separate sessions. The STARTUP line does that job.

static void
ResolveLogPath()
{
	const char *tmp = getenv("TEMP");
	if (!tmp) tmp = getenv("TMP");
	if (!tmp) tmp = ".";
	sprintf(S_log_path, "%s\\%s", tmp, OS_B5_LOG_LEAF);
}

static void
Log(const char *fmt, ...)
{
	if (!S_log_path[0]) return;

	FILE *f = fopen(S_log_path, "a");
	if (!f) return;

	time_t		now = time(NULL);
	struct tm	*lt = localtime(&now);
	char		stamp[32] = {'\0'};

	if (lt) {
		strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", lt);
	}
	fprintf(f, "[%s]  ", stamp);

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

//	AEGP_GetActiveLayer returns non-NULL only when exactly ONE layer is selected,
//	so a multi-selection reads as "none-or-multiple". That is the API's limit,
//	stated rather than papered over. B5 only needs to know the command arrived.

static void
DescribeSelection(char *outZ)
{
	AEGP_SuiteHandler	suites(sP);
	AEGP_LayerH			layerH = NULL;

	if (suites.LayerSuite9()->AEGP_GetActiveLayer(&layerH)) {
		sprintf(outZ, "selection=<query failed>");
		return;
	}
	if (!layerH) {
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
/*  Panel - carried over from B2                                       */
/* ------------------------------------------------------------------ */

static const char *S_prop_nameZ = "OnionSkinB5Panel";

class OSB5Panel;
static OSB5Panel *S_panel_instP = NULL;	// so a command can repaint it

class OSB5Panel
{
public:
	OSB5Panel(AEGP_PanelH panelH, AEGP_PlatformViewRef container,
				AEGP_PanelFunctions1 *outFunctionTable)
		: i_panelH(panelH), i_hwnd(container), i_live_clicks(0)
	{
		i_prev_proc = (WNDPROC)SetWindowLongPtrA(i_hwnd, GWLP_WNDPROC,
													(LONG_PTR)OSB5Panel::S_WndProc);
		::SetPropA(i_hwnd, S_prop_nameZ, (HANDLE)this);

		CreateWindowA("BUTTON", "Live button",
						WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
						10, 110, 120, 26,
						i_hwnd, (HMENU)(INT_PTR)OS_B5_BTN_LIVE, NULL, NULL);

		//	The broken control, kept from B2. Wired to nothing, on purpose.
		CreateWindowA("BUTTON", "Dead button",
						WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
						140, 110, 120, 26,
						i_hwnd, (HMENU)(INT_PTR)OS_B5_BTN_DEAD, NULL, NULL);

		outFunctionTable->DoFlyoutCommand	= S_DoFlyoutCommand;
		outFunctionTable->GetSnapSizes		= S_GetSnapSizes;
		outFunctionTable->PopulateFlyout	= S_PopulateFlyout;

		S_panel_instP = this;
		Log("PANEL   created. hwnd=%p", (void *)i_hwnd);
	}

	void Invalidate()
	{
		RECT r;
		GetClientRect(i_hwnd, &r);
		InvalidateRect(i_hwnd, &r, FALSE);
	}

private:
	AEGP_PanelH		i_panelH;
	HWND			i_hwnd;
	WNDPROC			i_prev_proc;
	A_long			i_live_clicks;

	static LRESULT CALLBACK S_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		OSB5Panel *selfP = reinterpret_cast<OSB5Panel *>(::GetPropA(hwnd, S_prop_nameZ));
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
				//	OS_B5_BTN_DEAD is absent on purpose.
				if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) == OS_B5_BTN_LIVE) {
					char sel[256] = {'\0'};
					DescribeSelection(sel);
					i_live_clicks++;
					Log("CLICK   LIVE  #%d  %s", (int)i_live_clicks, sel);
					Invalidate();
					handledB = true;
				}
				break;
		}

		if (i_prev_proc && !handledB) {
			return CallWindowProc(i_prev_proc, hwnd, msg, wp, lp);
		}
		return handledB ? 0 : DefWindowProc(hwnd, msg, wp, lp);
	}

	void OnPaint(HWND hwnd)
	{
		PAINTSTRUCT	ps;
		HDC			hdc = BeginPaint(hwnd, &ps);
		RECT		client;

		GetClientRect(hwnd, &client);

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

		//	The state, painted large and in colour. This is the whole point of
		//	keeping the panel in a spike about keyboard shortcuts: pressing a key
		//	has to visibly do something, or "it fired" is only a claim about a
		//	log file.
		RECT lamp = client;
		lamp.left	+= 10;
		lamp.top	+= 10;
		lamp.right	 = lamp.left + 40;
		lamp.bottom	 = lamp.top + 40;
		brush = CreateSolidBrush(S_enabledB ? RGB(80, 200, 100) : RGB(90, 90, 90));
		FillRect(hdc, &lamp, brush);
		DeleteObject(brush);

		char line[256] = {'\0'};
		sprintf(line, "Onion Skin: %s      prev frames: %d",
					S_enabledB ? "ON" : "OFF", (int)S_prev_frames);

		RECT text = client;
		text.left	+= 60;
		text.top	 = 18;
		text.bottom	 = text.top + 24;

		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, RGB(230, 230, 230));
		DrawTextA(hdc, line, (int)strlen(line), &text, DT_SINGLELINE | DT_LEFT);

		sprintf(line, "live clicks: %d", (int)i_live_clicks);
		text.left	= client.left + 10;
		text.top	= 70;
		text.bottom	= text.top + 24;
		SetTextColor(hdc, RGB(170, 170, 170));
		DrawTextA(hdc, line, (int)strlen(line), &text, DT_SINGLELINE | DT_LEFT);

		EndPaint(hwnd, &ps);
	}

	static A_Err S_GetSnapSizes(AEGP_PanelRefcon, A_LPoint *snapSizes, A_long *numSizesP)
	{
		snapSizes[0].x = 320;	snapSizes[0].y = 160;
		snapSizes[1].x = 420;	snapSizes[1].y = 300;
		*numSizesP = 2;
		return A_Err_NONE;
	}
	static A_Err S_PopulateFlyout(AEGP_PanelRefcon, AEGP_FlyoutMenuItem *, A_long *in_out_numItemsP)
	{
		*in_out_numItemsP = 0;
		return A_Err_NONE;
	}
	static A_Err S_DoFlyoutCommand(AEGP_PanelRefcon, AEGP_FlyoutMenuCmdID)
	{
		return A_Err_NONE;
	}
};

static void
RepaintPanel()
{
	if (S_panel_instP) {
		S_panel_instP->Invalidate();
	}
}

/* ------------------------------------------------------------------ */
/*  Hooks                                                              */
/* ------------------------------------------------------------------ */

static A_Err
CreatePanelHook(
	AEGP_GlobalRefcon, AEGP_CreatePanelRefcon,
	AEGP_PlatformViewRef	container,
	AEGP_PanelH				panelH,
	AEGP_PanelFunctions1	*outFunctionTable,
	AEGP_PanelRefcon		*outRefcon)
{
	*outRefcon = reinterpret_cast<AEGP_PanelRefcon>(
					new OSB5Panel(panelH, container, outFunctionTable));
	return A_Err_NONE;
}

static A_Err
CommandHook(
	AEGP_GlobalRefcon, AEGP_CommandRefcon,
	AEGP_Command		command,
	AEGP_HookPriority,
	A_Boolean,
	A_Boolean			*handledPB)
{
	char sel[256] = {'\0'};

	if (command == S_cmd_panel) {
		if (S_panelP) {
			Log("MENU    panel toggle requested");
			S_panelP->AEGP_ToggleVisibility(S_match_nameZ);
		}
		*handledPB = TRUE;

	} else if (command == S_cmd_toggle) {
		DescribeSelection(sel);
		S_enabledB = !S_enabledB;
		Log("CMD     TOGGLE  -> %s   %s", S_enabledB ? "ON" : "OFF", sel);
		RepaintPanel();
		*handledPB = TRUE;

	} else if (command == S_cmd_more) {
		DescribeSelection(sel);
		//	Clamped, and the clamp is logged when it bites. A command that
		//	silently does nothing at the limit looks identical to one that is
		//	broken.
		if (S_prev_frames < OS_B5_MAX_PREV) {
			S_prev_frames++;
			Log("CMD     MORE    -> prev=%d   %s", (int)S_prev_frames, sel);
		} else {
			Log("CMD     MORE    -> prev=%d (at max, unchanged)   %s", (int)S_prev_frames, sel);
		}
		RepaintPanel();
		*handledPB = TRUE;

	} else if (command == S_cmd_fewer) {
		DescribeSelection(sel);
		if (S_prev_frames > 0) {
			S_prev_frames--;
			Log("CMD     FEWER   -> prev=%d   %s", (int)S_prev_frames, sel);
		} else {
			Log("CMD     FEWER   -> prev=%d (at min, unchanged)   %s", (int)S_prev_frames, sel);
		}
		RepaintPanel();
		*handledPB = TRUE;
	}

	return A_Err_NONE;
}

static A_Err
UpdateMenuHook(AEGP_GlobalRefcon, AEGP_UpdateMenuRefcon, AEGP_WindowType)
{
	AEGP_SuiteHandler	suites(sP);

	//	All four enabled unconditionally. Enabling these only when a layer is
	//	selected would defeat the entire point of the spike - and, worse, AE
	//	will not deliver a keyboard shortcut for a DISABLED command, so a
	//	conditional enable here would silently make the shortcuts dead exactly
	//	when they are wanted.
	suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_panel);
	suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_toggle);
	suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_more);
	suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_fewer);

	//	Checkmark mirrors the state, so the Animation menu is a second, independent
	//	readout of the same bool the panel paints. If the two ever disagree, the
	//	state is not single-sourced.
	suites.CommandSuite1()->AEGP_CheckMarkMenuCommand(S_cmd_toggle, S_enabledB);

	if (S_panelP) {
		A_Boolean shownB = FALSE, frontB = FALSE;
		if (!S_panelP->AEGP_IsShown(S_match_nameZ, &shownB, &frontB)) {
			suites.CommandSuite1()->AEGP_CheckMarkMenuCommand(S_cmd_panel, shownB && frontB);
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
	Log("STARTUP AE %d.%d, plugin id %d", (int)major_versionL, (int)minor_versionL, (int)S_my_id);

	AEGP_SuiteHandler suites(sP);

	err = sP->AcquireSuite(kAEGPPanelSuite, kAEGPPanelSuiteVersion1,
							(const void **)&S_panelP);
	if (err || !S_panelP) {
		Log("WARN    no panel suite (err %d) - commands only", (int)err);
		S_panelP = NULL;
		err = A_Err_NONE;	// B5 does not need the panel to answer its question
	}

	//	Four commands. The panel toggle goes in Window, where AE users look for
	//	panels. The three actions go in Animation, because that is what they are.
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_panel));
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_toggle));
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_more));
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_fewer));

	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_cmd_panel, OS_B5_PANEL_MENU,
														AEGP_Menu_WINDOW, AEGP_MENU_INSERT_SORTED));
	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_cmd_toggle, OS_B5_TOGGLE_MENU,
														AEGP_Menu_ANIMATION, AEGP_MENU_INSERT_AT_BOTTOM));
	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_cmd_more, OS_B5_MORE_MENU,
														AEGP_Menu_ANIMATION, AEGP_MENU_INSERT_AT_BOTTOM));
	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_cmd_fewer, OS_B5_FEWER_MENU,
														AEGP_Menu_ANIMATION, AEGP_MENU_INSERT_AT_BOTTOM));

	//	One hook for all four; it dispatches on the command id. Registering four
	//	hooks would work too, but then a mis-registration would look like a
	//	dead command rather than a dispatch bug.
	ERR(suites.RegisterSuite5()->AEGP_RegisterCommandHook(S_my_id, AEGP_HP_BeforeAE,
															S_cmd_panel, CommandHook, NULL));
	ERR(suites.RegisterSuite5()->AEGP_RegisterCommandHook(S_my_id, AEGP_HP_BeforeAE,
															S_cmd_toggle, CommandHook, NULL));
	ERR(suites.RegisterSuite5()->AEGP_RegisterCommandHook(S_my_id, AEGP_HP_BeforeAE,
															S_cmd_more, CommandHook, NULL));
	ERR(suites.RegisterSuite5()->AEGP_RegisterCommandHook(S_my_id, AEGP_HP_BeforeAE,
															S_cmd_fewer, CommandHook, NULL));

	ERR(suites.RegisterSuite5()->AEGP_RegisterUpdateMenuHook(S_my_id, UpdateMenuHook, NULL));

	if (S_panelP) {
		ERR(S_panelP->AEGP_RegisterCreatePanelHook(S_my_id, S_match_nameZ,
													CreatePanelHook, NULL, true));
	}

	if (err) {
		Log("FAIL    registration failed, err %d", (int)err);
	} else {
		Log("READY   4 commands registered (panel, toggle, more, fewer)");
	}

	*global_refconP = NULL;
	return err;
}
