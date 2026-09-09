/*
	osA3.h (Onion Skin) - Phase 0, spike A3.

	Throwaway AEGP host for the two questions that decide A3:

	   A3a - does anything inside AE run during a modal drag, and at what
	         cadence? Logs timer ticks and AEGP idle ticks side by side, so the
	         two paths can be compared rather than assumed.

	   A3b - can the TRANSFORM be read from one of those ticks while the user is
	         dragging, and what does the read cost? This is the real gate: A1
	         made the transform knowable at rest, and A3b asks whether it is
	         still knowable in motion.
*/

#pragma once

//	The SDK's project template builds with warnings-as-errors, and the CRT's
//	deprecation warnings for fopen/sprintf/getenv are errors under it. This is a
//	throwaway spike that writes one log to %TEMP% from fixed-size buffers; the
//	_s variants would add noise without adding safety here.
#define _CRT_SECURE_NO_WARNINGS

#include "AEConfig.h"

#ifdef AE_OS_WIN
	#define VC_EXTRALEAN
	#include <windows.h>
#endif

#include "entry.h"
#include "AE_GeneralPlug.h"
#include "AEGP_SuiteHandler.h"
#include "AE_Macros.h"

#include <stdio.h>
#include <stdarg.h>

#define OS_A3A_MENU_NAME	"Onion Skin A3a (Tick Cadence: OFF/ON)"
#define OS_A3B_MENU_NAME	"Onion Skin A3b (Zoom-Read Latency: OFF/ON)"
#define OS_A3C_MENU_NAME	"Onion Skin A3c (Glued Overlay: OFF/ON)"

//	A3c tick period. A3a measured a 16ms request being delivered at ~30ms - that
//	is Windows' default 15.6ms timer granularity rounding up to two ticks, not
//	AE. A3c asks for 8ms AND raises the system timer resolution, so the cadence
//	it reports is the real ceiling rather than an artefact of the request.
#define OS_C_TICK_MS		8

//	Seconds to hover over the comp viewer before A3c latches onto it. A2 is not
//	solved - nothing can name AE's panels (A1a) - so for this spike the user
//	points at it.
#define OS_C_ARM_SECONDS	5

//	A3c stops itself after this long, for the same reason A3a does.
#define OS_C_MAX_SECONDS	120

#define OS_A3D_MENU_NAME	"Onion Skin A3d (Strip-Measured Overlay: OFF/ON)"

//	Where the sampling strips cross the panel, as offsets from its centre.
//	Deliberately NOT the exact centre: the comp centre crosshair is drawn there,
//	and a sample line that coincides with our own drawing would read the overlay
//	instead of AE. Odd offsets so the two lines cannot land on the same
//	symmetry as the comp edges at any round zoom.
#define OS_STRIP_ROW_OFF	37
#define OS_STRIP_COL_OFF	53

//	How different from the panel background a pixel must be to count as the comp
//	starting. Sum of absolute RGB difference, so 30 is a visible but modest step.
#define OS_STRIP_BG_TOL		30

//	The detected comp size must match s*comp_size to within this many pixels or
//	the detection is REJECTED. This is the control: without it a mis-detection
//	silently becomes a wrong transform, which is the exact failure mode A3c had.
#define OS_STRIP_SIZE_TOL	3.0

//	Tick period. 16ms is one frame at ~60Hz - the cadence an overlay has to hold
//	to look glued. Asking for it and measuring what we actually get is the whole
//	point; a timer that silently coalesces to 60ms is a fail, not a detail.
#define OS_TICK_MS			16

//	Hard cap on a logging session, so a forgotten ON never grows a log without
//	bound or leaves a timer running in the user's AE.
#define OS_MAX_SECONDS		60

//	Samples held in memory. Writing from inside a tick would measure the file
//	system, not AE.
#define OS_MAX_SAMPLES		40000

// Exported through the PiPL (.r file)
extern "C" DllExport AEGP_PluginInitFuncPrototype EntryPointFunc;
