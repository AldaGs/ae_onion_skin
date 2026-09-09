/*
	osB5.h (Onion Skin) - Phase 0, spike B5.

	Throwaway AEGP host for one question:

	   B5 - can onion-skin state be driven by MENU COMMANDS that the user can
	        assign keyboard shortcuts to, with no layer selected and without
	        travelling to a panel?

	B5 is not part of the gate. It runs before B4 (widget cost) because it is
	cheap, needs no GUI code, and covers the highest-frequency friction on its
	own: toggling onion skinning on and off while drawing. If B4's number comes
	back ugly, B5 plus a minimal panel is the shipped product.

	The panel from B2 is carried over deliberately, and it is not decoration. The
	claim here is that a KEYSTROKE changes the state, and the standing rule says
	that when the claim is about what the user sees, the check has to be what the
	user sees. The panel is where the state becomes visible.
*/

#pragma once

//	Same reasoning as osA3.h / osB2.h: SDK template builds warnings-as-errors and
//	the CRT deprecation warnings are errors under it. Throwaway spike, fixed-size
//	buffers, one log in %TEMP%.
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

//	Panel identity. Distinct from B2's so both spikes can be installed at once
//	without fighting over a workspace slot.
#define OS_B5_MATCH_NAME	"OnionSkinB5Panel"
#define OS_B5_PANEL_MENU	"Onion Skin B5 (Panel Spike)"

//	The three action commands. These are the things a user should be able to bind
//	a key to. Their names are what appear in AE's keyboard-shortcut editor, so
//	they are prefixed to group together in that list.
#define OS_B5_TOGGLE_MENU	"Onion Skin: Toggle"
#define OS_B5_MORE_MENU		"Onion Skin: More Previous Frames"
#define OS_B5_FEWER_MENU	"Onion Skin: Fewer Previous Frames"

//	Appended, never truncated - same reasoning as B2.
#define OS_B5_LOG_LEAF		"onionskin_B5.txt"

//	Stand-in state. There is no onion-skin effect yet (that is Phase 1), so the
//	commands drive a plain bool and a plain count. That is the honest scope: B5
//	asks whether the COMMAND PATH reaches us, not whether onion skinning works.
#define OS_B5_MAX_PREV		10

#define OS_B5_BTN_LIVE		2001
#define OS_B5_BTN_DEAD		2002

// Exported through the PiPL (.r file)
extern "C" DllExport AEGP_PluginInitFuncPrototype EntryPointFunc;
