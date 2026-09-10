/*
	onionSkin.h - Phase 1, the effect.

	Ports the model settled in python-proto/onion_skin/os_step1_composite.py:

		result = C(t) OVER [ skins composited back to front ]
		skin_k : rgb = lerp(src_rgb, tint, tint_amount)
		         a   = src_a * strength * falloff^(|k|-1)

	C(t) lands last and untouched. An onion skin must never alter what is being
	drawn.

	WHERE THE FRAMES COME FROM

	The effect's own input, checked out at t + k*step. Nothing else.

	PLACEMENT, AND THE ONE CONSTRAINT

	Two supported placements, one code path:

	  - an ADJUSTMENT LAYER above the drawing. This sees comp space, so it
	    ghosts EVERY kind of animation including layer transforms, which is what
	    character work needs.

	  - the DRAWING LAYER itself, for art that animates in its own space.

	Either way the requirement is the same and it is the documented constraint:
	THE INPUT MUST CARRY ALPHA. An adjustment layer receives the composite below
	it, so an opaque background below makes the ghosts invisible - the proto
	measured max|result - input| = EXACTLY 0.000, not degraded, gone. Keep the
	background above the onion-skin layer, or outside the comp.

	Source Layer params were tried in v1.0-v1.3 to lift that constraint and were
	RETIRED in v1.4. A checked-out layer param arrives at the layer's own
	dimensions carrying none of its comp transform, so it ghosts artwork but not
	animated position - which for character animation is the thing you need. The
	disk IDs stay reserved; see the enum.

	STRAIGHT ALPHA

	The compositing below is straight-alpha source-over. Note the transparent
	pixel rule carried from the proto: where output alpha is ~0 we keep the
	SOURCE's rgb rather than writing black, so that Enable=off and Strength=0
	are bit-exact pass-throughs rather than approximately right.
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

#define OS_NAME				"Onion Skin"
#define OS_MATCH_NAME		"aldai OnionSkin"
#define OS_PANEL_MATCH_NAME	"OnionSkinPanel"

#define OS_MAJOR	1
#define OS_MINOR	4
#define OS_BUG		0
#define OS_STAGE	PF_Stage_DEVELOP
#define OS_BUILD	5

/*	Disk IDs are APPEND-ONLY. Inserting one renumbers the rest and silently
	mis-maps every saved project ("effect control conversion required"). Add at
	the bottom, and bump the PiPL version and PF_VERSION together when you do. */
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
	//	RETIRED in v1.4. Kept, and still added in ParamsSetup, so the disk IDs
	//	after them do not shift -- deleting a param in the middle renumbers the
	//	rest and silently mis-maps every saved project. They are added invisible
	//	and never read. See the placement note above for why they went.
	OS_RETIRED_SOURCE_1,
	OS_RETIRED_SOURCE_2,
	OS_RETIRED_SOURCE_3,
	OS_OPEN_PANEL,
	OS_DEBUG_LOG,		// appended for v1.1 - see the append-only note above
	OS_NUM_PARAMS
};

#define OS_MAX_SKINS		12		// per side
#define OS_MAX_SOURCES		3

#define OS_PREV_DFLT		2
#define OS_NEXT_DFLT		2
#define OS_STEP_DFLT		1
#define OS_STRENGTH_DFLT	55.0
#define OS_FALLOFF_DFLT		60.0
#define OS_TINT_DFLT		100.0

//	Diagnostic log, off by default. Writes one block per render describing what
//	the effect actually SAW - how many sources it resolved, what each checkout
//	returned, and the dimensions it got. Added because "selecting a source shows
//	no ghost" is a question about what arrives, and guessing at that is how a
//	morning disappears.
#define OS_LOG_LEAF			"onionskin_fx.txt"
void OS_Log(const char *fmt, ...);

extern "C" {
	DllExport PF_Err EffectMain(
		PF_Cmd			cmd,
		PF_InData		*in_data,
		PF_OutData		*out_data,
		PF_ParamDef		*params[],
		PF_LayerDef		*output,
		void			*extra);
}
