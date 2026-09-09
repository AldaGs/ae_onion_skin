/*
	osB2.h (Onion Skin) - Phase 0, spike B2.

	Throwaway AEGP host for one question:

	   B2 - can an AEGP_PanelSuite panel exist, dock, survive a restart, and
	        respond to a click while the user's layer selection is anything at
	        all (including nothing)?

	This is half the Option B gate. If it fails, the panel is dead and the
	product falls back to menu commands plus the ordinary Effect Controls UI.

	Nothing in the project is read or modified except the ACTIVE LAYER, which is
	read (never written) so the log can record what the selection was at the
	moment of each click. B3 is where writing starts.
*/

#pragma once

//	Same reasoning as osA3.h: the SDK template builds warnings-as-errors and the
//	CRT deprecation warnings for fopen/getenv are errors under it. Throwaway
//	spike, fixed-size buffers, one log in %TEMP%.
#define _CRT_SECURE_NO_WARNINGS

#include "AEConfig.h"

#ifdef AE_OS_WIN
	#define VC_EXTRALEAN
	#include <windows.h>
#endif

#include "entry.h"
#include "AE_GeneralPlug.h"
#include "AE_GeneralPlugPanels.h"
#include "AE_EffectSuites.h"
#include "AEGP_SuiteHandler.h"
#include "AE_Macros.h"

#include <stdio.h>
#include <time.h>

//	The match name is the panel's identity to AE - it is what AEGP_ToggleVisibility
//	and AEGP_IsShown take, and what AE stores in the user's workspace file. It is
//	NOT localized and it must never change once a workspace has seen it, or every
//	saved workspace forgets where the panel was docked.
#define OS_B2_MATCH_NAME	"OnionSkinB2Panel"

//	The user-visible name. Separate from the match name on purpose.
#define OS_B2_MENU_NAME		"Onion Skin B2 (Panel Spike)"

//	The log is opened in APPEND mode and never truncated. That is deliberate: the
//	whole point of B2 is what happens ACROSS AE restarts, and a log that starts
//	fresh each launch cannot show that.
#define OS_B2_LOG_LEAF		"onionskin_B2.txt"

//	Control IDs for the two buttons.
//
//	LIVE is wired to a handler. DEAD is created identically, sits next to it, and
//	is wired to NOTHING - its ID is deliberately absent from the WM_COMMAND
//	switch. It is the broken control: if the log shows a line when DEAD is
//	clicked, then the handler is firing on any click and the LIVE lines prove
//	nothing. Carried from pieFX S2, where a handler that fired on everything
//	looked exactly like a handler that worked.
#define OS_B2_BTN_LIVE		2001
#define OS_B2_BTN_DEAD		2002

// Exported through the PiPL (.r file)
extern "C" DllExport AEGP_PluginInitFuncPrototype EntryPointFunc;
