/*
	osB3_aegp.cpp - the AEGP half of spike B3. This is the gate.

	THE QUESTION

	The panel must change the effect's params on a layer the user has NOT
	selected, AE must re-render, and the user's selection must survive untouched.
	If the selection moves every time somebody nudges opacity, the panel is
	unusable while animating and Option B's premise is false in practice even
	though it is true on paper.

	WHAT IS MEASURED, PER WRITE

	  - the selected layer's ID BEFORE and AFTER. Not "is something selected" -
	    the actual AEGP_LayerIDVal, so a silent re-selection of a different layer
	    cannot pass as "still one layer selected".
	  - the param value read back after the write.
	  - wall-clock either side, so a repaint that costs seconds is visible.

	THE BROKEN CONTROL

	The NO-OP button writes the value the param already holds. If a no-op write
	still produces a visible repaint, then what we are watching is AE's idle
	redraw and not our write, and every other row is unreadable. There is also a
	DEAD button, carried from B2, wired to nothing.

	A CONSTRAINT FOUND IN THE HEADER, NOT AT RUNTIME

	AE_GeneralPlug.h says AEGP_SetStreamValue is "only legal to call when
	AEGP_GetStreamNumKFs==0". A KEYFRAMED param cannot be written this way. That
	is a real constraint on the Phase 2 panel - onion-skin opacity is exactly the
	sort of thing somebody might animate - so this spike detects the case and
	logs it rather than writing blindly. Finding it here is cheaper than finding
	it in Phase 2.
*/

#include "osB3.h"

static AEGP_PluginID		S_my_id		= 0L;
static SPBasicSuite			*sP			= NULL;
static AEGP_PanelSuite1		*S_panelP	= NULL;
static AEGP_Command			S_cmd_panel	= 0L;
static char					S_log_path[AEGP_MAX_PATH_SIZE] = {'\0'};

static const A_u_char *S_match_nameZ =
	reinterpret_cast<const A_u_char *>(OS_B3_MATCH_NAME);

/* ------------------------------------------------------------------ */
/*  Log - shared with the effect half                                  */
/* ------------------------------------------------------------------ */

void
OSB3_ResolveLogPath()
{
	if (S_log_path[0]) return;			// whichever half loads first wins

	const char *tmp = getenv("TEMP");
	if (!tmp) tmp = getenv("TMP");
	if (!tmp) tmp = ".";
	sprintf(S_log_path, "%s\\%s", tmp, OS_B3_LOG_LEAF);
}

void
OSB3_Log(const char *fmt, ...)
{
	if (!S_log_path[0]) OSB3_ResolveLogPath();

	FILE *f = fopen(S_log_path, "a");
	if (!f) return;

	time_t		now = time(NULL);
	struct tm	*lt = localtime(&now);
	char		stamp[32] = {'\0'};
	if (lt) strftime(stamp, sizeof(stamp), "%H:%M:%S", lt);

	//	Millisecond resolution matters here in a way it did not in B2 or B5:
	//	one of the measurements is how long AE takes to come back from a write.
	SYSTEMTIME st;
	GetLocalTime(&st);
	fprintf(f, "[%s.%03d]  ", stamp, (int)st.wMilliseconds);

	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);

	fprintf(f, "\n");
	fclose(f);
}

/* ------------------------------------------------------------------ */
/*  Panel visibility - called by BOTH halves                           */
/* ------------------------------------------------------------------ */

A_Err
OSB3_ShowPanel(SPBasicSuite *pica_basicP)
{
	A_Err				err			= A_Err_NONE;
	AEGP_PanelSuite1	*panelP		= NULL;
	A_Boolean			shownB = FALSE, frontB = FALSE;

	//	Acquire locally rather than reusing S_panelP: when this is called from
	//	the EFFECT half we may be on a different suite scope, and reusing a
	//	pointer cached by the AEGP half would be the kind of thing that works
	//	until it does not.
	ERR(pica_basicP->AcquireSuite(kAEGPPanelSuite, kAEGPPanelSuiteVersion1,
									(const void **)&panelP));
	if (err || !panelP) return err;

	ERR(panelP->AEGP_IsShown(S_match_nameZ, &shownB, &frontB));

	//	Only toggle when it is not already up. AEGP_ToggleVisibility would CLOSE
	//	an open panel, which is the wrong thing for a button labelled "Open".
	if (!err && !(shownB && frontB)) {
		ERR(panelP->AEGP_ToggleVisibility(S_match_nameZ));
	}

	pica_basicP->ReleaseSuite(kAEGPPanelSuite, kAEGPPanelSuiteVersion1);
	return err;
}

/* ------------------------------------------------------------------ */
/*  Selection, read only                                               */
/* ------------------------------------------------------------------ */

//	Returns the selected layer's ID, or -1 for none/multiple. The ID rather than
//	a yes/no, because the failure being hunted is a write that QUIETLY MOVES the
//	selection to the layer it wrote - which would still read as "one layer
//	selected" and pass a weaker check.

static AEGP_LayerIDVal
CurrentSelectionID()
{
	AEGP_SuiteHandler	suites(sP);
	AEGP_LayerH			layerH = NULL;
	AEGP_LayerIDVal		id = (AEGP_LayerIDVal)-1;

	if (suites.LayerSuite9()->AEGP_GetActiveLayer(&layerH) || !layerH) {
		return (AEGP_LayerIDVal)-1;
	}
	if (suites.LayerSuite9()->AEGP_GetLayerID(layerH, &id)) {
		return (AEGP_LayerIDVal)-1;
	}
	return id;
}

/* ------------------------------------------------------------------ */
/*  Finding the effect                                                 */
/* ------------------------------------------------------------------ */

//	Walks the active comp for the FIRST layer carrying our effect, by match name.
//	By match name and not by index or a stored handle, because layers move - and
//	Phase 2's managed layer will be found exactly this way.

static A_Err
FindOurEffect(AEGP_LayerH *layerPH, AEGP_EffectRefH *effectPH, A_long *layer_indexPL)
{
	A_Err				err = A_Err_NONE;
	AEGP_SuiteHandler	suites(sP);
	AEGP_ItemH			itemH	= NULL;
	AEGP_CompH			compH	= NULL;
	A_long				num_layers = 0;

	*layerPH	= NULL;
	*effectPH	= NULL;

	ERR(suites.ItemSuite6()->AEGP_GetActiveItem(&itemH));
	if (err || !itemH) return err;

	ERR(suites.CompSuite4()->AEGP_GetCompFromItem(itemH, &compH));
	if (err || !compH) return err;

	ERR(suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &num_layers));

	for (A_long i = 0; !err && i < num_layers; i++) {
		AEGP_LayerH	layerH = NULL;
		A_long		num_effects = 0;

		ERR(suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layerH));
		if (err || !layerH) continue;

		if (suites.EffectSuite4()->AEGP_GetLayerNumEffects(layerH, &num_effects)) {
			continue;
		}

		for (A_long e = 0; e < num_effects; e++) {
			AEGP_EffectRefH			effectH = NULL;
			AEGP_InstalledEffectKey	key;
			A_char					match[AEGP_MAX_EFFECT_MATCH_NAME_SIZE] = {'\0'};

			if (suites.EffectSuite4()->AEGP_GetLayerEffectByIndex(S_my_id, layerH, e, &effectH)) {
				continue;
			}
			if (!suites.EffectSuite4()->AEGP_GetInstalledKeyFromLayerEffect(effectH, &key) &&
				!suites.EffectSuite4()->AEGP_GetEffectMatchName(key, match) &&
				!strcmp(match, OS_B3_FX_MATCH_NAME))
			{
				*layerPH		= layerH;
				*effectPH		= effectH;		// caller disposes
				*layer_indexPL	= i;
				return A_Err_NONE;
			}
			suites.EffectSuite4()->AEGP_DisposeEffect(effectH);
		}
	}
	return err;
}

/* ------------------------------------------------------------------ */
/*  The write - this is the measurement                                */
/* ------------------------------------------------------------------ */

enum WriteKind { kPlus, kMinus, kNoOp };

static void
DoWrite(WriteKind kind)
{
	AEGP_SuiteHandler	suites(sP);
	A_Err				err			= A_Err_NONE;
	AEGP_LayerH			layerH		= NULL;
	AEGP_EffectRefH		effectH		= NULL;
	AEGP_StreamRefH		streamH		= NULL;
	A_long				layer_index	= -1;

	const char *kindZ = (kind == kPlus) ? "PLUS " : (kind == kMinus) ? "MINUS" : "NOOP ";

	//	Selection BEFORE. Captured before anything else touches AE.
	AEGP_LayerIDVal sel_before = CurrentSelectionID();

	err = FindOurEffect(&layerH, &effectH, &layer_index);
	if (err || !effectH) {
		OSB3_Log("WRITE   %s  FAILED: no layer in the active comp carries %s",
					kindZ, OS_B3_FX_MATCH_NAME);
		return;
	}

	ERR(suites.StreamSuite5()->AEGP_GetNewEffectStreamByIndex(S_my_id, effectH,
																OS_B3_BRIGHTNESS_INDEX, &streamH));

	//	The keyframe check, from the header's own warning. A keyframed stream
	//	cannot be written with AEGP_SetStreamValue at all, so detect and report
	//	rather than calling into undefined behaviour.
	if (!err && streamH) {
		A_long num_kfs = 0;
		if (!suites.KeyframeSuite3()->AEGP_GetStreamNumKFs(streamH, &num_kfs) && num_kfs > 0) {
			OSB3_Log("WRITE   %s  REFUSED: stream has %d keyframes; SetStreamValue is illegal here",
						kindZ, (int)num_kfs);
			suites.StreamSuite5()->AEGP_DisposeStream(streamH);
			suites.EffectSuite4()->AEGP_DisposeEffect(effectH);
			return;
		}
	}

	AEGP_StreamValue2	val;
	AEFX_CLR_STRUCT(val);
	A_Time				zero = {0, 100};

	ERR(suites.StreamSuite5()->AEGP_GetNewStreamValue(S_my_id, streamH,
														AEGP_LTimeMode_CompTime, &zero, FALSE, &val));

	double before = val.val.one_d;
	double after  = before;

	if (kind == kPlus)			after = before + 10.0;
	else if (kind == kMinus)	after = before - 10.0;
	// kNoOp: after == before, deliberately

	if (after < OS_B3_BRIGHT_MIN) after = OS_B3_BRIGHT_MIN;
	if (after > OS_B3_BRIGHT_MAX) after = OS_B3_BRIGHT_MAX;

	val.val.one_d = after;

	//	One undo group around one write, so Ctrl+Z is one step. Without this AE
	//	may or may not coalesce, and "undo took three presses" is a real defect
	//	for a control the user will nudge repeatedly.
	ERR(suites.UtilitySuite3()->AEGP_StartUndoGroup("Onion Skin B3 write"));

	DWORD t0 = GetTickCount();
	ERR(suites.StreamSuite5()->AEGP_SetStreamValue(S_my_id, streamH, &val));
	DWORD t1 = GetTickCount();

	suites.UtilitySuite3()->AEGP_EndUndoGroup();

	//	Read back rather than trusting the write. A SetStreamValue that returns
	//	no error but does not stick would otherwise look like a pass.
	AEGP_StreamValue2	check;
	AEFX_CLR_STRUCT(check);
	double readback = -999.0;
	if (!suites.StreamSuite5()->AEGP_GetNewStreamValue(S_my_id, streamH,
														AEGP_LTimeMode_CompTime, &zero, FALSE, &check)) {
		readback = check.val.one_d;
		suites.StreamSuite5()->AEGP_DisposeStreamValue(&check);
	}

	//	Selection AFTER.
	AEGP_LayerIDVal sel_after = CurrentSelectionID();

	OSB3_Log("WRITE   %s  layer_idx=%d  %.1f -> %.1f  readback=%.1f  set_ms=%lu  sel %d -> %d  %s",
				kindZ, (int)layer_index, before, after, readback,
				(unsigned long)(t1 - t0),
				(int)sel_before, (int)sel_after,
				(sel_before == sel_after) ? "SELECTION INTACT" : "*** SELECTION CHANGED ***");

	if (err) {
		OSB3_Log("WRITE   %s  err=%d", kindZ, (int)err);
	}

	suites.StreamSuite5()->AEGP_DisposeStreamValue(&val);
	suites.StreamSuite5()->AEGP_DisposeStream(streamH);
	suites.EffectSuite4()->AEGP_DisposeEffect(effectH);
}

/* ------------------------------------------------------------------ */
/*  Panel                                                              */
/* ------------------------------------------------------------------ */

static const char *S_prop_nameZ = "OnionSkinB3Panel";

class OSB3Panel
{
public:
	OSB3Panel(AEGP_PanelH panelH, AEGP_PlatformViewRef container,
				AEGP_PanelFunctions1 *outFunctionTable)
		: i_panelH(panelH), i_hwnd(container)
	{
		i_prev_proc = (WNDPROC)SetWindowLongPtrA(i_hwnd, GWLP_WNDPROC,
													(LONG_PTR)OSB3Panel::S_WndProc);
		::SetPropA(i_hwnd, S_prop_nameZ, (HANDLE)this);

		MakeButton("Brightness +10",	OS_B3_BTN_PLUS,		10,  40);
		MakeButton("Brightness -10",	OS_B3_BTN_MINUS,	150, 40);
		MakeButton("No-op write",		OS_B3_BTN_NOOP,		10,  76);
		MakeButton("Dead button",		OS_B3_BTN_DEAD,		150, 76);

		outFunctionTable->DoFlyoutCommand	= S_DoFlyoutCommand;
		outFunctionTable->GetSnapSizes		= S_GetSnapSizes;
		outFunctionTable->PopulateFlyout	= S_PopulateFlyout;

		OSB3_Log("PANEL   created. hwnd=%p", (void *)i_hwnd);
	}

private:
	AEGP_PanelH		i_panelH;
	HWND			i_hwnd;
	WNDPROC			i_prev_proc;

	void MakeButton(const char *labelZ, int id, int x, int y)
	{
		CreateWindowA("BUTTON", labelZ, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
						x, y, 130, 26, i_hwnd, (HMENU)(INT_PTR)id, NULL, NULL);
	}

	static LRESULT CALLBACK S_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		OSB3Panel *selfP = reinterpret_cast<OSB3Panel *>(::GetPropA(hwnd, S_prop_nameZ));
		return selfP ? selfP->WndProc(hwnd, msg, wp, lp) : DefWindowProc(hwnd, msg, wp, lp);
	}

	LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		bool handledB = false;

		switch (msg) {
			case WM_PAINT:
				OnPaint(hwnd);
				handledB = true;
				break;

			case WM_COMMAND:
				if (HIWORD(wp) == BN_CLICKED) {
					//	OS_B3_BTN_DEAD is absent on purpose.
					switch (LOWORD(wp)) {
						case OS_B3_BTN_PLUS:	DoWrite(kPlus);		handledB = true; break;
						case OS_B3_BTN_MINUS:	DoWrite(kMinus);	handledB = true; break;
						case OS_B3_BTN_NOOP:	DoWrite(kNoOp);		handledB = true; break;
					}
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
		HBRUSH				brush;

		if (!suites.AppSuite4()->PF_AppGetColor(PF_App_Color_PANEL_BACKGROUND, &bg)) {
			brush = CreateSolidBrush(RGB(bg.red / 255, bg.green / 255, bg.blue / 255));
		} else {
			brush = CreateSolidBrush(RGB(48, 48, 48));
		}
		FillRect(hdc, &client, brush);
		DeleteObject(brush);

		const char *msgZ = "Writes Brightness on the first layer carrying Onion Skin B3.";
		RECT text = client;
		text.left += 10; text.top = 10; text.bottom = text.top + 24;
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, RGB(210, 210, 210));
		DrawTextA(hdc, msgZ, (int)strlen(msgZ), &text, DT_SINGLELINE | DT_LEFT);

		EndPaint(hwnd, &ps);
	}

	static A_Err S_GetSnapSizes(AEGP_PanelRefcon, A_LPoint *s, A_long *nP)
	{ s[0].x = 300; s[0].y = 130; s[1].x = 400; s[1].y = 200; *nP = 2; return A_Err_NONE; }
	static A_Err S_PopulateFlyout(AEGP_PanelRefcon, AEGP_FlyoutMenuItem *, A_long *nP)
	{ *nP = 0; return A_Err_NONE; }
	static A_Err S_DoFlyoutCommand(AEGP_PanelRefcon, AEGP_FlyoutMenuCmdID)
	{ return A_Err_NONE; }
};

/* ------------------------------------------------------------------ */
/*  Hooks                                                              */
/* ------------------------------------------------------------------ */

static A_Err
CreatePanelHook(AEGP_GlobalRefcon, AEGP_CreatePanelRefcon,
				AEGP_PlatformViewRef container, AEGP_PanelH panelH,
				AEGP_PanelFunctions1 *outFunctionTable, AEGP_PanelRefcon *outRefcon)
{
	*outRefcon = reinterpret_cast<AEGP_PanelRefcon>(
					new OSB3Panel(panelH, container, outFunctionTable));
	return A_Err_NONE;
}

static A_Err
CommandHook(AEGP_GlobalRefcon, AEGP_CommandRefcon, AEGP_Command command,
			AEGP_HookPriority, A_Boolean, A_Boolean *handledPB)
{
	if (command == S_cmd_panel && S_panelP) {
		S_panelP->AEGP_ToggleVisibility(S_match_nameZ);
		*handledPB = TRUE;
	}
	return A_Err_NONE;
}

static A_Err
UpdateMenuHook(AEGP_GlobalRefcon, AEGP_UpdateMenuRefcon, AEGP_WindowType)
{
	AEGP_SuiteHandler suites(sP);
	suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_panel);
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

	OSB3_ResolveLogPath();

	//	B1's answer is this line appearing in the same log as the effect half's
	//	GLOBAL_SETUP line. Two entry points, one binary, both alive.
	OSB3_Log("STARTUP AEGP half loaded. AE %d.%d, plugin id %d",
				(int)major_versionL, (int)minor_versionL, (int)S_my_id);

	AEGP_SuiteHandler suites(sP);

	err = sP->AcquireSuite(kAEGPPanelSuite, kAEGPPanelSuiteVersion1,
							(const void **)&S_panelP);
	if (err || !S_panelP) {
		OSB3_Log("FAIL    no panel suite (err %d)", (int)err);
		return err ? err : A_Err_GENERIC;
	}

	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_panel));
	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_cmd_panel, OS_B3_PANEL_MENU,
														AEGP_Menu_WINDOW, AEGP_MENU_INSERT_SORTED));
	ERR(suites.RegisterSuite5()->AEGP_RegisterCommandHook(S_my_id, AEGP_HP_BeforeAE,
															S_cmd_panel, CommandHook, NULL));
	ERR(suites.RegisterSuite5()->AEGP_RegisterUpdateMenuHook(S_my_id, UpdateMenuHook, NULL));
	ERR(S_panelP->AEGP_RegisterCreatePanelHook(S_my_id, S_match_nameZ,
												CreatePanelHook, NULL, true));

	OSB3_Log(err ? "FAIL    registration err %d" : "READY   AEGP half registered", (int)err);

	*global_refconP = NULL;
	return err;
}
