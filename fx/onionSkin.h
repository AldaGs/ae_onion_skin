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

//	Param indices, match names and defaults live in ONE place so the effect and
//	the panel cannot drift apart. See the header for why that matters.
#include "../shared/onionSkinIDs.h"

#define OS_MAJOR	1
#define OS_MINOR	5
#define OS_BUG		0
#define OS_STAGE	PF_Stage_DEVELOP
#define OS_BUILD	6

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
