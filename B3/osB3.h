/*
	osB3.h (Onion Skin) - Phase 0, spike B3 (and B1, and the effect-side launcher).

	Three questions in one binary, because they all need the same stub effect:

	   B3  - can an AEGP panel write another plug-in's effect param such that AE
	         re-renders, WITHOUT stealing the user's layer selection, in one
	         undo step? This is the remaining gate.

	   B1  - can one .aex carry both an effect entry point and an AEGP entry
	         point? Answered for free: this file's PiPL declares both, so if AE
	         loads it and both halves work, B1 is "one binary".

	   launcher - can the effect open the panel from a button in Effect Controls?

	Bundling them is a deliberate exception to "one spike, one question". They
	share the stub effect, and building it three times would cost more than the
	coupling does. The risk is named: if the two-resource PiPL fails to load,
	B3 fails with it and has to be re-run from a split build. That is the first
	thing to check on run 1.
*/

#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include "AEConfig.h"

#ifdef AE_OS_WIN
	#define VC_EXTRALEAN
	#include <windows.h>
#endif

#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectSuites.h"
#include "AE_EffectCBSuites.h"
#include "AE_GeneralPlug.h"
#include "AE_GeneralPlugPanels.h"
#include "AEGP_SuiteHandler.h"
#include "AE_Macros.h"
#include "Param_Utils.h"

#include <stdio.h>
#include <time.h>

/* ---- identity ---------------------------------------------------- */

//	The effect's match name. The AEGP finds the effect by comparing against this
//	string, so it is the contract between the two halves of the binary and must
//	be identical in the PiPL.
#define OS_B3_FX_MATCH_NAME		"aldai OnionSkinB3"
#define OS_B3_FX_NAME			"Onion Skin B3"

#define OS_B3_MATCH_NAME		"OnionSkinB3Panel"
#define OS_B3_PANEL_MENU		"Onion Skin B3 (Write Spike)"

#define OS_B3_LOG_LEAF			"onionskin_B3.txt"

/* ---- effect params ----------------------------------------------- */

//	Disk IDs are a plain enum and are APPEND-ONLY: inserting one renumbers the
//	rest and silently mis-maps saved projects. This is a spike and nothing has
//	been saved against it yet, but the habit is the point.
enum {
	OS_B3_INPUT = 0,
	OS_B3_BRIGHTNESS,		// the param the panel writes
	OS_B3_OPEN_PANEL,		// the effect-side launcher
	OS_B3_NUM_PARAMS
};

//	Param index used by AEGP_GetNewEffectStreamByIndex. Index 0 is the effect's
//	input layer, so the stream indices line up with the enum above.
#define OS_B3_BRIGHTNESS_INDEX	1

#define OS_B3_BRIGHT_MIN		0
#define OS_B3_BRIGHT_MAX		200
#define OS_B3_BRIGHT_DFLT		100

/* ---- panel controls ---------------------------------------------- */

#define OS_B3_BTN_PLUS			3001	// write brightness + 10
#define OS_B3_BTN_MINUS			3002	// write brightness - 10
#define OS_B3_BTN_NOOP			3003	// write the value it already holds
#define OS_B3_BTN_DEAD			3004	// wired to nothing

/* ---- shared --------------------------------------------------- */

void	OSB3_Log(const char *fmt, ...);
void	OSB3_ResolveLogPath();

//	Opens the panel if it is not already shown. NOT a toggle: a button labelled
//	"Open Panel" that closes an open panel is a bug, and AEGP_ToggleVisibility
//	on its own would do exactly that.
A_Err	OSB3_ShowPanel(SPBasicSuite *pica_basicP);

// Both entry points, both exported through the PiPL.
extern "C" {
	DllExport PF_Err EffectMain(
		PF_Cmd			cmd,
		PF_InData		*in_data,
		PF_OutData		*out_data,
		PF_ParamDef		*params[],
		PF_LayerDef		*output,
		void			*extra);

	DllExport AEGP_PluginInitFuncPrototype EntryPointFunc;
}
