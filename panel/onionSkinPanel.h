/*
	onionSkinPanel.h - Phase 2. The AEGP half: the managed layer and the panel.

	WHAT THIS IS FOR

	Onion skinning should be a thing you toggle, not a layer you maintain. This
	plug-in creates the adjustment layer, marks it, keeps it on top, applies the
	effect, and removes it again - so the user presses a key and gets ghosts.

	THE THREE RULES PHASE 0 PAID FOR

	1. THE PANEL HOLDS NO STATE. Every control reads from and writes to the
	   effect's param streams. If a value could be out of sync between the panel
	   and Effect Controls, it would be, and the user would trust the wrong one.
	   The snapshot below is a CACHE OF WHAT WAS READ, never a source of truth.

	2. NO AEGP CALLS FROM THE WNDPROC. B3 measured "internal verification
	   failure ... {no current context}" and then "AEGP magic error" for a write
	   made from a Win32 callback. Everything that touches the project is queued
	   and performed in the idle hook, which is a context AE accepts. That
	   includes READS - the panel paints from the snapshot and never asks AE
	   anything.

	3. BALANCE UNDO GROUPS WITH A FLAG. ERR(FUNC) is "if (!err) err = FUNC", so
	   ERR(StartUndoGroup) followed by a bare EndUndoGroup() is unbalanced on the
	   error path, and AE says "Group Mismatch" on the next Ctrl+Z. Track it.

	COMMANDS

	Registered into the Animation menu so AE lists them in its keyboard-shortcut
	editor. B5 established that this is the interaction that matters: an animator
	with a hand on the pen wants a key, not a button to travel to.
*/

#pragma once

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
#include <stdarg.h>
#include <time.h>

//	One file, both binaries. Never copy the enum.
#include "../shared/onionSkinIDs.h"

#define OSP_PANEL_MENU		"Onion Skin"
#define OSP_CMD_TOGGLE		"Onion Skin: Toggle"
//	These move PREVIOUS AND NEXT TOGETHER, which is what "more onion skin" means
//	to the hand on the pen. Renamed from "... Previous Frames" when the behaviour
//	changed rather than left describing something else - AE lists these strings
//	in its shortcut editor, so the name is the whole documentation.
//
//	NOTE the rename orphans any shortcut already bound to the old names; AE keys
//	its bindings on the command name. That is the cost of the names being honest.
#define OSP_CMD_MORE		"Onion Skin: More Frames"
#define OSP_CMD_FEWER		"Onion Skin: Fewer Frames"

#define OSP_LOG_LEAF		"onionskin_panel.txt"

//	Panel control ids.
#define OSP_BTN_TOGGLE		4001
#define OSP_BTN_PREV_DN		4002
#define OSP_BTN_PREV_UP		4003
#define OSP_BTN_NEXT_DN		4004
#define OSP_BTN_NEXT_UP		4005
#define OSP_BTN_STR_DN		4006
#define OSP_BTN_STR_UP		4007

extern "C" {
	DllExport AEGP_PluginInitFuncPrototype EntryPointFunc;
}
