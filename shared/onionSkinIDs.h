/*
	onionSkinIDs.h - the contract between the two binaries.

	B1 established that one .aex is claimed by one kind, so Onion Skin ships as
	onionSkin.aex (the effect) and onionSkinPanel.aex (the AEGP). They find each
	other by match name and they address params by INDEX - which means a disk ID
	added to one binary and not the other is a silent cross-wiring bug: the panel
	would write Strength into Falloff and nothing would error.

	So the enum lives here, in one file, included by both. Never copy it.

	DISK IDS ARE APPEND-ONLY. Inserting one renumbers the rest and mis-maps every
	saved project ("effect control conversion required"). Add at the bottom, and
	bump PF_VERSION and the PiPL's AE_Effect_Version together when you do.
*/

#pragma once

#define OS_NAME				"Onion Skin"
#define OS_MATCH_NAME		"aldai OnionSkin"

//	The panel's identity to AE. AEGP_ToggleVisibility and AEGP_IsShown take it,
//	and AE stores it in the user's workspace, so it must never change once a
//	workspace has seen it.
#define OS_PANEL_MATCH_NAME	"OnionSkinPanel"

//	What the AEGP calls the layer it manages. Also how it finds that layer again,
//	so it is a contract too - but a soft one: the layer is really identified by
//	carrying OS_MATCH_NAME, and the name is for the user's benefit.
#define OS_LAYER_NAME		"ONION SKIN"

/*	Param indices. These double as PF_ParamIndex for
	AEGP_GetNewEffectStreamByIndex, where 0 is the effect's input layer - which
	is why OS_INPUT must stay at 0. */
enum {
	OS_INPUT = 0,
	OS_ENABLE,
	OS_PREV_FRAMES,
	OS_NEXT_FRAMES,
	OS_FRAME_STEP,
	OS_STRENGTH,
	OS_FALLOFF,
	OS_TINT_AMOUNT,
	OS_PAST_COLOR,
	OS_FUTURE_COLOR,

	//	RETIRED in v1.4: a checked-out layer param carries none of its comp
	//	transform, so it ghosted artwork but not animated position. Still created
	//	(invisible) and never read, because deleting them would renumber
	//	everything below.
	OS_RETIRED_SOURCE_1,
	OS_RETIRED_SOURCE_2,
	OS_RETIRED_SOURCE_3,

	OS_OPEN_PANEL,
	OS_DEBUG_LOG,
	OS_NUM_PARAMS
};

#define OS_MAX_SKINS		12		// per side
#define OS_MAX_SOURCES		3		// retired; kept so the loop that creates them still reads

#define OS_PREV_DFLT		2
#define OS_NEXT_DFLT		2
#define OS_STEP_DFLT		1
#define OS_STRENGTH_DFLT	55.0
#define OS_FALLOFF_DFLT		60.0
#define OS_TINT_DFLT		100.0
