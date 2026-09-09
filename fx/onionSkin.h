/*
	onionSkin.h - Phase 1, the effect.

	Ports the model settled in python-proto/onion_skin/os_step1_composite.py:

		result = C(t) OVER [ skins composited back to front ]
		skin_k : rgb = lerp(src_rgb, tint, tint_amount)
		         a   = src_a * strength * falloff^(|k|-1)

	C(t) lands last and untouched. An onion skin must never alter what is being
	drawn.

	WHERE THE FRAMES COME FROM

	Two placements, one code path, decided by whether any Source Layer is set:

	  - Source Layer(s) set  -> skins come from those layers, checked out at
	    t + k*step. Works with ANY background, because a layer's own alpha is
	    never contaminated by what sits under it.

	  - none set             -> skins come from the effect's own input. On an
	    adjustment layer that is the composite below, which is correct ONLY when
	    everything below carries alpha. With an opaque background the proto
	    measured max|result - input| = EXACTLY 0.000 - not degraded, invisible.
	    On a drawing layer it is always safe.

	The maths never asks which case it is in; it only needs input with real
	alpha.

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
#define OS_MINOR	2
#define OS_BUG		0
#define OS_STAGE	PF_Stage_DEVELOP
#define OS_BUILD	3

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
	OS_SOURCE_1,
	OS_SOURCE_2,
	OS_SOURCE_3,
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
