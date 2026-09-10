/*
	onionSkinPanel.cpp - Phase 2. See onionSkinPanel.h for the three rules.
*/

#include "onionSkinPanel.h"

static AEGP_PluginID	S_my_id		= 0L;
static SPBasicSuite		*sP			= NULL;
static AEGP_PanelSuite1	*S_panelP	= NULL;
static char				S_log_path[AEGP_MAX_PATH_SIZE] = {'\0'};

static AEGP_Command		S_cmd_panel	= 0L;
static AEGP_Command		S_cmd_toggle = 0L;
static AEGP_Command		S_cmd_more	= 0L;
static AEGP_Command		S_cmd_fewer	= 0L;

static const A_u_char *S_panel_nameZ =
	reinterpret_cast<const A_u_char *>(OS_PANEL_MATCH_NAME);

/* ------------------------------------------------------------------ */
/*  Log                                                                */
/* ------------------------------------------------------------------ */

static void
Log(const char *fmt, ...)
{
	if (!S_log_path[0]) {
		const char *tmp = getenv("TEMP");
		if (!tmp) tmp = getenv("TMP");
		if (!tmp) tmp = ".";
		sprintf(S_log_path, "%s\\%s", tmp, OSP_LOG_LEAF);
	}

	FILE *f = fopen(S_log_path, "a");
	if (!f) return;

	SYSTEMTIME st;
	GetLocalTime(&st);
	fprintf(f, "[%02d:%02d:%02d.%03d]  ",
			(int)st.wHour, (int)st.wMinute, (int)st.wSecond, (int)st.wMilliseconds);

	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fprintf(f, "\n");
	fclose(f);
}

/* ------------------------------------------------------------------ */
/*  Snapshot - a CACHE of what was read, never a source of truth       */
/* ------------------------------------------------------------------ */

//	Rule 1. The panel paints from this and nothing else; the idle hook refills it
//	from the param streams every tick. If the user changes Strength in Effect
//	Controls, the panel follows within a tick, because the streams are the only
//	thing either of them believes.

typedef struct {
	A_Boolean	has_compB;			// is there an active comp at all
	A_Boolean	has_layerB;			// is onion skinning ON (a managed layer exists)
	A_long		n_instances;		// every layer carrying the effect, managed or not
	A_Boolean	enabledB;
	A_long		prev, next;
	double		strength;
	A_Boolean	opaque_belowB;		// a full-frame opaque layer under the onion layer
	double		past_r, past_g, past_b;
	double		next_r, next_g, next_b;
	char		note[160];
} Snapshot;

static Snapshot S_snap;

/* ------------------------------------------------------------------ */
/*  Finding things                                                     */
/* ------------------------------------------------------------------ */

static A_Err
ActiveComp(AEGP_CompH *compPH)
{
	A_Err				err = A_Err_NONE;
	AEGP_SuiteHandler	suites(sP);
	AEGP_ItemH			itemH = NULL;

	*compPH = NULL;
	ERR(suites.ItemSuite6()->AEGP_GetActiveItem(&itemH));
	if (err || !itemH) return err;

	return suites.CompSuite4()->AEGP_GetCompFromItem(itemH, compPH);
}

//	Does this layer carry our effect? Returns the effect ref (caller disposes) or
//	NULL. By MATCH NAME, never by index - layers and effects both move.
static AEGP_EffectRefH
OurEffectOn(AEGP_LayerH layerH)
{
	AEGP_SuiteHandler	suites(sP);
	A_long				n = 0;

	if (suites.EffectSuite4()->AEGP_GetLayerNumEffects(layerH, &n)) return NULL;

	for (A_long e = 0; e < n; e++) {
		AEGP_EffectRefH			fxH = NULL;
		AEGP_InstalledEffectKey	key;
		A_char					match[AEGP_MAX_EFFECT_MATCH_NAME_SIZE] = {'\0'};

		if (suites.EffectSuite4()->AEGP_GetLayerEffectByIndex(S_my_id, layerH, e, &fxH)) {
			continue;
		}
		if (!suites.EffectSuite4()->AEGP_GetInstalledKeyFromLayerEffect(fxH, &key) &&
			!suites.EffectSuite4()->AEGP_GetEffectMatchName(key, match) &&
			!strcmp(match, OS_MATCH_NAME))
		{
			return fxH;			// caller disposes
		}
		suites.EffectSuite4()->AEGP_DisposeEffect(fxH);
	}
	return NULL;
}

static AEGP_InstalledEffectKey
OurInstalledKey()
{
	AEGP_SuiteHandler		suites(sP);
	AEGP_InstalledEffectKey	key = AEGP_InstalledEffectKey_NONE;

	while (!suites.EffectSuite4()->AEGP_GetNextInstalledEffect(key, &key) &&
			key != AEGP_InstalledEffectKey_NONE)
	{
		A_char match[AEGP_MAX_EFFECT_MATCH_NAME_SIZE] = {'\0'};
		if (!suites.EffectSuite4()->AEGP_GetEffectMatchName(key, match) &&
			!strcmp(match, OS_MATCH_NAME))
		{
			return key;
		}
	}
	return AEGP_InstalledEffectKey_NONE;
}

/* ------------------------------------------------------------------ */
/*  The opaque-background check                                        */
/* ------------------------------------------------------------------ */

//	The one confusing failure mode this product has: an adjustment layer receives
//	the composite below it, so a full-frame opaque layer underneath makes every
//	ghost invisible. The effect cannot detect it - by the time it runs, the
//	pixels are already flattened and an opaque pixel carries no evidence of what
//	made it. The AEGP can, because it can see the layer stack.
//
//	Deliberately conservative: it reports only what it is SURE of - a visible,
//	non-adjustment, full-frame layer below ours with opacity 100. A false alarm
//	here would be worse than silence, because it would train the user to ignore
//	the warning that matters.

static A_Boolean
OpaqueLayerBelow(AEGP_CompH compH, A_long onion_index)
{
	AEGP_SuiteHandler	suites(sP);
	A_long				n = 0, cw = 0, ch = 0;
	AEGP_ItemH			comp_itemH = NULL;

	//	Same rule as below: a success code is not a promise of a non-NULL handle.
	if (suites.CompSuite4()->AEGP_GetItemFromComp(compH, &comp_itemH)) return FALSE;
	if (!comp_itemH) return FALSE;
	if (suites.ItemSuite6()->AEGP_GetItemDimensions(comp_itemH, &cw, &ch)) return FALSE;
	if (suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &n)) return FALSE;

	for (A_long i = onion_index + 1; i < n; i++) {
		AEGP_LayerH	layerH = NULL;
		AEGP_LayerFlags flags = 0;

		if (suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layerH)) continue;
		if (suites.LayerSuite9()->AEGP_GetLayerFlags(layerH, &flags)) continue;

		//	Invisible, adjustment and guide layers cannot be the culprit.
		if (!(flags & AEGP_LayerFlag_VIDEO_ACTIVE))		continue;
		if (flags & AEGP_LayerFlag_ADJUSTMENT_LAYER)	continue;
		if (flags & AEGP_LayerFlag_GUIDE_LAYER)			continue;

		//	Only AV layers can be a background. Cameras, lights and text are not
		//	candidates and asking them for a source item is meaningless.
		AEGP_ObjectType	otype = AEGP_ObjectType_NONE;
		if (suites.LayerSuite9()->AEGP_GetLayerObjectType(layerH, &otype)) continue;
		if (otype != AEGP_ObjectType_AV) continue;

		AEGP_ItemH	srcH = NULL;
		A_long		lw = 0, lh = 0;

		//	AEGP_GetLayerSourceItem returns A_Err_NONE with a NULL item for any
		//	layer that has no source - shape layers, text, nulls, solids created
		//	certain ways. Passing that NULL on raises AE's "internal verification
		//	failure {itemH cannot be NULL}", which is what run 1 hit: the check
		//	written to help comps with artwork in them fell over on the shape
		//	layers that artwork is made of.
		//
		//	A success code is not a promise of a non-NULL handle. Check both.
		if (suites.LayerSuite9()->AEGP_GetLayerSourceItem(layerH, &srcH)) continue;
		if (!srcH) continue;
		if (suites.ItemSuite6()->AEGP_GetItemDimensions(srcH, &lw, &lh)) continue;

		//	Smaller than the comp: it cannot be covering everything. A shape
		//	layer big enough to cover the frame is therefore NOT reported - it
		//	has no source item to measure. That is the conservative side to err
		//	on: a missed warning costs a puzzled minute, a false one costs the
		//	user's trust in every warning after it.
		if (lw < cw || lh < ch) continue;

		//	NOTE this is AEGP_StreamVal2, the bare union, not AEGP_StreamValue2 -
		//	AEGP_GetLayerStreamValue hands back the value itself with nothing to
		//	dispose. The two type names differ by one character and the compiler
		//	is the only thing that will tell you.
		AEGP_StreamVal2	sv;
		A_Time			now = {0, 100};
		AEFX_CLR_STRUCT(sv);
		if (suites.StreamSuite5()->AEGP_GetLayerStreamValue(layerH,
					AEGP_LayerStream_OPACITY, AEGP_LTimeMode_CompTime, &now,
					FALSE, &sv, NULL)) {
			continue;
		}
		if (sv.one_d > 99.5) return TRUE;
	}
	return FALSE;
}

/* ------------------------------------------------------------------ */
/*  Reading and writing the effect's params                            */
/* ------------------------------------------------------------------ */

static A_Boolean
ReadStream(AEGP_EffectRefH fxH, A_long idx, double *outP)
{
	AEGP_SuiteHandler	suites(sP);
	AEGP_StreamRefH		streamH = NULL;
	AEGP_StreamValue2	val;
	A_Time				zero = {0, 100};

	if (suites.StreamSuite5()->AEGP_GetNewEffectStreamByIndex(S_my_id, fxH, idx, &streamH)) {
		return FALSE;
	}
	AEFX_CLR_STRUCT(val);
	A_Boolean okB = FALSE;
	if (!suites.StreamSuite5()->AEGP_GetNewStreamValue(S_my_id, streamH,
				AEGP_LTimeMode_CompTime, &zero, FALSE, &val)) {
		*outP = val.val.one_d;
		okB = TRUE;
		suites.StreamSuite5()->AEGP_DisposeStreamValue(&val);
	}
	suites.StreamSuite5()->AEGP_DisposeStream(streamH);
	return okB;
}

static A_Boolean
ReadColorStream(AEGP_EffectRefH fxH, A_long idx, double *rP, double *gP, double *bP)
{
	AEGP_SuiteHandler	suites(sP);
	AEGP_StreamRefH		streamH = NULL;
	AEGP_StreamValue2	val;
	A_Time				zero = {0, 100};

	if (suites.StreamSuite5()->AEGP_GetNewEffectStreamByIndex(S_my_id, fxH, idx, &streamH)) {
		return FALSE;
	}
	AEFX_CLR_STRUCT(val);
	A_Boolean okB = FALSE;
	if (!suites.StreamSuite5()->AEGP_GetNewStreamValue(S_my_id, streamH,
				AEGP_LTimeMode_CompTime, &zero, FALSE, &val)) {
		*rP = val.val.color.redF;
		*gP = val.val.color.greenF;
		*bP = val.val.color.blueF;
		okB = TRUE;
		suites.StreamSuite5()->AEGP_DisposeStreamValue(&val);
	}
	suites.StreamSuite5()->AEGP_DisposeStream(streamH);
	return okB;
}

//	Writes one param on one effect instance. Assumes an undo group is already
//	open around the whole broadcast - see BroadcastWrite.
static A_Boolean
WriteStreamNoUndo(AEGP_EffectRefH fxH, A_long idx, double value)
{
	AEGP_SuiteHandler	suites(sP);
	AEGP_StreamRefH		streamH = NULL;
	AEGP_StreamValue2	val;
	A_Time				zero = {0, 100};
	A_long				n_kfs = 0;

	if (suites.StreamSuite5()->AEGP_GetNewEffectStreamByIndex(S_my_id, fxH, idx, &streamH)) {
		return FALSE;
	}

	//	AEGP_SetStreamValue is "only legal to call when AEGP_GetStreamNumKFs==0"
	//	(AE_GeneralPlug.h). A keyframed param cannot be written this way at all,
	//	so refuse and say so rather than call into undefined behaviour. Found in
	//	B3, and still true.
	if (!suites.KeyframeSuite3()->AEGP_GetStreamNumKFs(streamH, &n_kfs) && n_kfs > 0) {
		Log("WRITE   param %ld REFUSED: %d keyframes", (long)idx, (int)n_kfs);
		sprintf(S_snap.note, "\"%s\" is keyframed - change it in the timeline.",
				(idx == OS_STRENGTH) ? "Strength" :
				(idx == OS_PREV_FRAMES) ? "Previous Frames" :
				(idx == OS_NEXT_FRAMES) ? "Next Frames" : "That parameter");
		suites.StreamSuite5()->AEGP_DisposeStream(streamH);
		return FALSE;
	}

	AEFX_CLR_STRUCT(val);
	A_Boolean okB = FALSE;
	if (!suites.StreamSuite5()->AEGP_GetNewStreamValue(S_my_id, streamH,
				AEGP_LTimeMode_CompTime, &zero, FALSE, &val)) {
		val.val.one_d = value;
		okB = (suites.StreamSuite5()->AEGP_SetStreamValue(S_my_id, streamH, &val) == A_Err_NONE);
		suites.StreamSuite5()->AEGP_DisposeStreamValue(&val);
	}
	suites.StreamSuite5()->AEGP_DisposeStream(streamH);
	return okB;
}

//	Writes N params to EVERY instance in the comp, under ONE undo group.
//
//	Broadcast because onion-skin settings are a workspace preference, not
//	per-layer art direction - and because a panel that silently drove only the
//	first of three instances would look broken on the other two.
//
//	N params rather than one because the shortcuts move Previous and Next
//	together: two separate calls would open two undo groups and cost the user two
//	presses of Ctrl+Z for one press of the key, which is exactly the defect B3's
//	runbook was written to catch.
static void
BroadcastWriteN(const A_long *idxP, const double *valP, A_long n_params,
				const char *whatZ)
{
	AEGP_SuiteHandler	suites(sP);
	AEGP_CompH			compH = NULL;
	A_long				n = 0, hits = 0;
	A_Boolean			group_openedB = FALSE;

	if (ActiveComp(&compH) || !compH) return;
	if (suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &n)) return;

	//	Rule 3: tracked with a flag, never left to call order under ERR.
	if (!suites.UtilitySuite3()->AEGP_StartUndoGroup(whatZ)) {
		group_openedB = TRUE;
	}

	for (A_long i = 0; i < n; i++) {
		AEGP_LayerH		layerH = NULL;
		AEGP_EffectRefH	fxH = NULL;

		if (suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layerH)) continue;
		fxH = OurEffectOn(layerH);
		if (!fxH) continue;

		for (A_long k = 0; k < n_params; k++) {
			if (WriteStreamNoUndo(fxH, idxP[k], valP[k])) hits++;
		}
		suites.EffectSuite4()->AEGP_DisposeEffect(fxH);
	}

	if (group_openedB) {
		suites.UtilitySuite3()->AEGP_EndUndoGroup();
	}
	Log("WRITE   %s -> %ld write(s)", whatZ, (long)hits);
}

static void
BroadcastWrite(A_long idx, double value, const char *whatZ)
{
	BroadcastWriteN(&idx, &value, 1, whatZ);
}

/* ------------------------------------------------------------------ */
/*  The managed layer                                                  */
/* ------------------------------------------------------------------ */

//	Found by carrying the effect AND being an adjustment layer. The name is for
//	the user; the effect is the identity. A layer renamed by hand still works.
static AEGP_LayerH
FindManagedLayer(AEGP_CompH compH, A_long *indexPL)
{
	AEGP_SuiteHandler	suites(sP);
	A_long				n = 0;

	if (suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &n)) return NULL;

	for (A_long i = 0; i < n; i++) {
		AEGP_LayerH		layerH = NULL;
		AEGP_LayerFlags	flags = 0;

		if (suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layerH)) continue;
		if (suites.LayerSuite9()->AEGP_GetLayerFlags(layerH, &flags)) continue;
		if (!(flags & AEGP_LayerFlag_ADJUSTMENT_LAYER)) continue;

		AEGP_EffectRefH fxH = OurEffectOn(layerH);
		if (fxH) {
			suites.EffectSuite4()->AEGP_DisposeEffect(fxH);
			if (indexPL) *indexPL = i;
			return layerH;
		}
	}
	return NULL;
}

static void
CreateManagedLayer()
{
	AEGP_SuiteHandler	suites(sP);
	A_Err				err = A_Err_NONE;
	AEGP_CompH			compH = NULL;
	AEGP_ItemH			comp_itemH = NULL;
	AEGP_LayerH			layerH = NULL;
	A_long				cw = 0, ch = 0;
	A_Boolean			group_openedB = FALSE;

	if (ActiveComp(&compH) || !compH) {
		sprintf(S_snap.note, "Open a comp first.");
		return;
	}

	AEGP_InstalledEffectKey key = OurInstalledKey();
	if (key == AEGP_InstalledEffectKey_NONE) {
		sprintf(S_snap.note, "onionSkin.aex is not installed.");
		Log("CREATE  FAILED: effect %s not installed", OS_MATCH_NAME);
		return;
	}

	ERR(suites.CompSuite4()->AEGP_GetItemFromComp(compH, &comp_itemH));
	if (err || !comp_itemH) return;			// success != non-NULL handle
	ERR(suites.ItemSuite6()->AEGP_GetItemDimensions(comp_itemH, &cw, &ch));
	if (err || cw <= 0 || ch <= 0) return;

	if (!suites.UtilitySuite3()->AEGP_StartUndoGroup("Onion Skin On")) {
		group_openedB = TRUE;
	}

	AEGP_ColorVal white = {1.0, 1.0, 1.0, 1.0};

	//	CompSuite4 takes A_char here; later suites take UTF16. Using the suite
	//	the SuiteHandler actually hands us rather than the one the docs show.
	ERR(suites.CompSuite4()->AEGP_CreateSolidInComp(OS_LAYER_NAME, cw, ch, &white,
													compH, NULL, &layerH));
	if (!err && layerH) {
		//	Adjustment so it sees the composite below and therefore every kind of
		//	animation, transforms included. Guide so it never renders to output
		//	by accident - onion skinning is a drawing aid, not a look.
		suites.LayerSuite9()->AEGP_SetLayerFlag(layerH, AEGP_LayerFlag_ADJUSTMENT_LAYER, TRUE);
		suites.LayerSuite9()->AEGP_SetLayerFlag(layerH, AEGP_LayerFlag_GUIDE_LAYER, TRUE);

		AEGP_EffectRefH fxH = NULL;
		if (!suites.EffectSuite4()->AEGP_ApplyEffect(S_my_id, layerH, key, &fxH)) {
			suites.EffectSuite4()->AEGP_DisposeEffect(fxH);
		}
		Log("CREATE  managed layer, %ldx%ld", (long)cw, (long)ch);
	} else {
		Log("CREATE  FAILED err=%d", (int)err);
	}

	if (group_openedB) {
		suites.UtilitySuite3()->AEGP_EndUndoGroup();
	}
}

static void
RemoveManagedLayer()
{
	AEGP_SuiteHandler	suites(sP);
	AEGP_CompH			compH = NULL;
	A_long				idx = -1;
	A_Boolean			group_openedB = FALSE;

	if (ActiveComp(&compH) || !compH) return;

	AEGP_LayerH layerH = FindManagedLayer(compH, &idx);
	if (!layerH) return;

	if (!suites.UtilitySuite3()->AEGP_StartUndoGroup("Onion Skin Off")) {
		group_openedB = TRUE;
	}
	suites.LayerSuite9()->AEGP_DeleteLayer(layerH);
	if (group_openedB) {
		suites.UtilitySuite3()->AEGP_EndUndoGroup();
	}
	Log("REMOVE  managed layer at index %ld", (long)idx);
}

/* ------------------------------------------------------------------ */
/*  Snapshot refresh - the only place AE is READ                       */
/* ------------------------------------------------------------------ */

static void
RefreshSnapshot()
{
	AEGP_SuiteHandler	suites(sP);
	Snapshot			s;
	AEGP_CompH			compH = NULL;
	A_long				n = 0, onion_index = -1;

	AEFX_CLR_STRUCT(s);
	strcpy(s.note, S_snap.note);		// notes persist until something replaces them

	if (ActiveComp(&compH) || !compH) {
		S_snap = s;
		return;
	}
	s.has_compB = TRUE;

	AEGP_LayerH managedH = FindManagedLayer(compH, &onion_index);
	s.has_layerB = (managedH != NULL);

	if (!suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &n)) {
		for (A_long i = 0; i < n; i++) {
			AEGP_LayerH layerH = NULL;
			if (suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layerH)) continue;
			AEGP_EffectRefH fxH = OurEffectOn(layerH);
			if (fxH) {
				if (s.n_instances == 0) {
					double v;
					if (ReadStream(fxH, OS_ENABLE, &v))			s.enabledB = (v > 0.5);
					if (ReadStream(fxH, OS_PREV_FRAMES, &v))	s.prev = (A_long)(v + 0.5);
					if (ReadStream(fxH, OS_NEXT_FRAMES, &v))	s.next = (A_long)(v + 0.5);
					if (ReadStream(fxH, OS_STRENGTH, &v))		s.strength = v;
					ReadColorStream(fxH, OS_PAST_COLOR,   &s.past_r, &s.past_g, &s.past_b);
					ReadColorStream(fxH, OS_FUTURE_COLOR, &s.next_r, &s.next_g, &s.next_b);
				}
				s.n_instances++;
				suites.EffectSuite4()->AEGP_DisposeEffect(fxH);
			}
		}
	}

	if (s.has_layerB && onion_index >= 0) {
		s.opaque_belowB = OpaqueLayerBelow(compH, onion_index);
	}

	S_snap = s;
}

/* ------------------------------------------------------------------ */
/*  Queue + idle hook - rule 2                                         */
/* ------------------------------------------------------------------ */

enum {
	kReqNone = -1,
	kReqToggleLayer = 0,
	kReqEnableFlip,
	kReqPrevUp, kReqPrevDn,
	kReqNextUp, kReqNextDn,
	kReqStrUp,  kReqStrDn,

	//	Both directions at once - what the keyboard shortcuts drive.
	kReqBothUp, kReqBothDn
};

static volatile LONG S_pending = kReqNone;

static void Queue(LONG req) { InterlockedExchange(&S_pending, req); }

static void
Perform(LONG req)
{
	//	Clamps are applied here, against the snapshot's freshly-read values, so
	//	the panel and the streams cannot disagree about where the limits are.
	switch (req) {
		case kReqToggleLayer:
			S_snap.note[0] = '\0';
			if (S_snap.has_layerB)	RemoveManagedLayer();
			else					CreateManagedLayer();
			break;

		case kReqEnableFlip:
			BroadcastWrite(OS_ENABLE, S_snap.enabledB ? 0.0 : 1.0, "Onion Skin Enable");
			break;

		case kReqPrevUp:
			BroadcastWrite(OS_PREV_FRAMES, MIN(OS_MAX_SKINS, S_snap.prev + 1), "Onion Skin Previous");
			break;
		case kReqPrevDn:
			BroadcastWrite(OS_PREV_FRAMES, MAX(0, S_snap.prev - 1), "Onion Skin Previous");
			break;
		case kReqNextUp:
			BroadcastWrite(OS_NEXT_FRAMES, MIN(OS_MAX_SKINS, S_snap.next + 1), "Onion Skin Next");
			break;
		case kReqNextDn:
			BroadcastWrite(OS_NEXT_FRAMES, MAX(0, S_snap.next - 1), "Onion Skin Next");
			break;
		case kReqStrUp:
			BroadcastWrite(OS_STRENGTH, MIN(100.0, S_snap.strength + 5.0), "Onion Skin Strength");
			break;
		case kReqStrDn:
			BroadcastWrite(OS_STRENGTH, MAX(0.0, S_snap.strength - 5.0), "Onion Skin Strength");
			break;

		//	Previous and Next move together, in one undo step. Each side is
		//	clamped on its own, so a side already at the limit simply stays there
		//	rather than blocking the other.
		case kReqBothUp: {
			A_long	idx[2] = {OS_PREV_FRAMES, OS_NEXT_FRAMES};
			double	val[2] = {(double)MIN(OS_MAX_SKINS, S_snap.prev + 1),
							  (double)MIN(OS_MAX_SKINS, S_snap.next + 1)};
			BroadcastWriteN(idx, val, 2, "Onion Skin More Frames");
			break;
		}
		case kReqBothDn: {
			A_long	idx[2] = {OS_PREV_FRAMES, OS_NEXT_FRAMES};
			double	val[2] = {(double)MAX(0, S_snap.prev - 1),
							  (double)MAX(0, S_snap.next - 1)};
			BroadcastWriteN(idx, val, 2, "Onion Skin Fewer Frames");
			break;
		}
	}
}

static HWND S_panel_hwnd = NULL;

static A_Err
IdleHook(AEGP_GlobalRefcon, AEGP_IdleRefcon, A_long *max_sleepPL)
{
	LONG req = InterlockedExchange(&S_pending, (LONG)kReqNone);

	if (req != kReqNone) {
		Perform(req);
	}

	//	Refresh every tick, not only after our own writes: the streams are the
	//	source of truth and the user may have changed them in Effect Controls.
	Snapshot before = S_snap;
	RefreshSnapshot();

	if (S_panel_hwnd &&
		(before.has_layerB	!= S_snap.has_layerB	||
		 before.enabledB	!= S_snap.enabledB		||
		 before.prev		!= S_snap.prev			||
		 before.next		!= S_snap.next			||
		 before.strength	!= S_snap.strength		||
		 before.past_r		!= S_snap.past_r		||
		 before.next_b		!= S_snap.next_b		||
		 before.n_instances	!= S_snap.n_instances	||
		 before.opaque_belowB != S_snap.opaque_belowB ||
		 strcmp(before.note, S_snap.note)))
	{
		InvalidateRect(S_panel_hwnd, NULL, FALSE);
	}

	if (max_sleepPL) *max_sleepPL = 6;		// ~10/sec is plenty for a readout
	return A_Err_NONE;
}

/* ------------------------------------------------------------------ */
/*  Panel                                                              */
/* ------------------------------------------------------------------ */

static const char *S_propZ = "OnionSkinPanelInst";

//	The stepper loop below builds its control ids by arithmetic, which only
//	works while the ids are laid out as consecutive DOWN/UP pairs. Reordering
//	them in the header would silently wire "+" to the wrong row rather than
//	failing to compile, so it is made to fail to compile.
static_assert(OSP_BTN_PREV_UP == OSP_BTN_PREV_DN + 1, "stepper ids must be DN,UP pairs");
static_assert(OSP_BTN_NEXT_DN == OSP_BTN_PREV_DN + 2, "stepper ids must be consecutive");
static_assert(OSP_BTN_NEXT_UP == OSP_BTN_PREV_DN + 3, "stepper ids must be DN,UP pairs");
static_assert(OSP_BTN_STR_DN  == OSP_BTN_PREV_DN + 4, "stepper ids must be consecutive");
static_assert(OSP_BTN_STR_UP  == OSP_BTN_PREV_DN + 5, "stepper ids must be DN,UP pairs");

//	One grid, named once. Scattering magic numbers through Paint is how a panel
//	drifts half a pixel out of line every time somebody touches it.
#define PAD		12
#define ROW0	78
#define ROWH	28
#define STEP_X	150

class OSPanel
{
public:
	OSPanel(AEGP_PanelH panelH, AEGP_PlatformViewRef container,
			AEGP_PanelFunctions1 *tableP)
		: i_panelH(panelH), i_hwnd(container)
	{
		i_prev = (WNDPROC)SetWindowLongPtrA(i_hwnd, GWLP_WNDPROC, (LONG_PTR)S_WndProc);
		::SetPropA(i_hwnd, S_propZ, (HANDLE)this);

		//	AE's own UI font. The stock Win32 default is a bitmap face from the
		//	nineties and makes a panel look broken rather than plain.
		i_font = CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
								DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
								CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
								DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
		i_font_bold = CreateFontA(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
								DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
								CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
								DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

		Btn("Turn Onion Skin On / Off", OSP_BTN_TOGGLE, PAD, 32, 232, 26);

		//	Steppers flank the value, so the eye reads  -  2  +  as one control
		//	rather than as three. Rows share one grid rather than each carrying
		//	its own offsets.
		for (int r = 0; r < 3; r++) {
			int y = ROW0 + r * ROWH;
			Btn("-", OSP_BTN_PREV_DN + r * 2,     STEP_X,      y, 22, 20);
			Btn("+", OSP_BTN_PREV_DN + r * 2 + 1, STEP_X + 74, y, 22, 20);
		}

		tableP->DoFlyoutCommand	= S_Flyout;
		tableP->GetSnapSizes	= S_Snap;
		tableP->PopulateFlyout	= S_Populate;

		S_panel_hwnd = i_hwnd;
		Log("PANEL   created");
	}

private:
	AEGP_PanelH	i_panelH;
	HWND		i_hwnd;
	WNDPROC		i_prev;
	HFONT		i_font;
	HFONT		i_font_bold;

	void Btn(const char *z, int id, int x, int y, int w, int h)
	{
		HWND b = CreateWindowA("BUTTON", z, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
								x, y, w, h, i_hwnd, (HMENU)(INT_PTR)id, NULL, NULL);
		if (b && i_font) SendMessage(b, WM_SETFONT, (WPARAM)i_font, TRUE);
	}

	static LRESULT CALLBACK S_WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
	{
		OSPanel *p = reinterpret_cast<OSPanel *>(::GetPropA(h, S_propZ));
		return p ? p->Proc(h, m, w, l) : DefWindowProc(h, m, w, l);
	}

	LRESULT Proc(HWND h, UINT m, WPARAM w, LPARAM l)
	{
		bool handledB = false;

		switch (m) {
			case WM_PAINT:	Paint(h); handledB = true; break;
			case WM_SIZE:	InvalidateRect(h, NULL, FALSE); break;

			case WM_COMMAND:
				if (HIWORD(w) == BN_CLICKED) {
					//	Rule 2: these QUEUE. Not one AEGP call happens here.
					switch (LOWORD(w)) {
						case OSP_BTN_TOGGLE:	Queue(kReqToggleLayer);	handledB = true; break;
						case OSP_BTN_PREV_DN:	Queue(kReqPrevDn);		handledB = true; break;
						case OSP_BTN_PREV_UP:	Queue(kReqPrevUp);		handledB = true; break;
						case OSP_BTN_NEXT_DN:	Queue(kReqNextDn);		handledB = true; break;
						case OSP_BTN_NEXT_UP:	Queue(kReqNextUp);		handledB = true; break;
						case OSP_BTN_STR_DN:	Queue(kReqStrDn);		handledB = true; break;
						case OSP_BTN_STR_UP:	Queue(kReqStrUp);		handledB = true; break;
					}
				}
				break;

			case WM_DESTROY:
				if (S_panel_hwnd == h) S_panel_hwnd = NULL;
				if (i_font)      { DeleteObject(i_font);      i_font = NULL; }
				if (i_font_bold) { DeleteObject(i_font_bold); i_font_bold = NULL; }
				break;
		}

		if (i_prev && !handledB) return CallWindowProc(i_prev, h, m, w, l);
		return handledB ? 0 : DefWindowProc(h, m, w, l);
	}

	void Line(HDC dc, int y, int right)
	{
		RECT r = {PAD, y, right - PAD, y + 1};
		HBRUSH b = CreateSolidBrush(RGB(70, 70, 70));
		FillRect(dc, &r, b);
		DeleteObject(b);
	}

	void Row(HDC dc, const char *labelZ, const char *valZ, int y)
	{
		RECT r;
		SetBkMode(dc, TRANSPARENT);

		SetTextColor(dc, RGB(165, 165, 165));
		r.left = PAD; r.top = y; r.right = STEP_X - 8; r.bottom = y + 20;
		DrawTextA(dc, labelZ, (int)strlen(labelZ), &r, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

		//	The value sits BETWEEN the two steppers so the control reads as one
		//	thing rather than as two buttons and a number that happen to be near
		//	each other.
		SetTextColor(dc, RGB(238, 238, 238));
		r.left = STEP_X + 22; r.top = y; r.right = STEP_X + 74; r.bottom = y + 20;
		DrawTextA(dc, valZ, (int)strlen(valZ), &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
	}

	void Swatch(HDC dc, const char *labelZ, double r01, double g01, double b01,
				int x, int y)
	{
		RECT sw = {x, y + 2, x + 14, y + 16};
		HBRUSH b = CreateSolidBrush(RGB((int)(r01 * 255), (int)(g01 * 255), (int)(b01 * 255)));
		FillRect(dc, &sw, b);
		DeleteObject(b);

		//	A hairline, so a dark tint does not vanish into the panel.
		FrameRect(dc, &sw, (HBRUSH)GetStockObject(GRAY_BRUSH));

		RECT t = {x + 20, y, x + 96, y + 18};
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, RGB(165, 165, 165));
		DrawTextA(dc, labelZ, (int)strlen(labelZ), &t, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
	}

	//	Returns the next free y. Everything below the swatches is conditional, so
	//	it FLOWS rather than sitting at fixed offsets - a warning that gets
	//	clipped is a warning nobody reads.
	int Note(HDC dc, const char *z, COLORREF col, int y, int right)
	{
		RECT m = {PAD, y, right - PAD, y + 400};
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, col);

		int h = DrawTextA(dc, z, (int)strlen(z), &m, DT_WORDBREAK | DT_CALCRECT);

		RECT r = {PAD, y, right - PAD, y + h};
		DrawTextA(dc, z, (int)strlen(z), &r, DT_WORDBREAK);
		return y + h + 8;
	}

	void Paint(HWND h)
	{
		//	Rule 1 and rule 2 together: this reads S_snap and nothing else. No
		//	AEGP call, no cached "what I last wrote" - only what was last READ
		//	from the streams.
		PAINTSTRUCT	ps;
		HDC			dc = BeginPaint(h, &ps);
		RECT		client;
		char		buf[240];

		GetClientRect(h, &client);
		int right = client.right;

		AEGP_SuiteHandler	suites(sP);
		PF_App_Color		bg = {0};
		HBRUSH				br;

		if (!suites.AppSuite4()->PF_AppGetColor(PF_App_Color_PANEL_BACKGROUND, &bg)) {
			br = CreateSolidBrush(RGB(bg.red / 255, bg.green / 255, bg.blue / 255));
		} else {
			br = CreateSolidBrush(RGB(48, 48, 48));
		}
		FillRect(dc, &client, br);
		DeleteObject(br);

		HFONT old = (HFONT)SelectObject(dc, i_font);
		SetBkMode(dc, TRANSPARENT);

		//	Three lamp states, distinguishable at a glance: green means ghosts
		//	are on, amber means the layer is there but the effect is disabled,
		//	grey means nothing. The middle one exists because it is a real state
		//	the user can reach and would otherwise look identical to "off".
		COLORREF lamp = RGB(95, 95, 95);
		const char *stateZ = "off";
		if (S_snap.has_layerB) {
			if (S_snap.enabledB) { lamp = RGB(86, 196, 108); stateZ = "on"; }
			else                 { lamp = RGB(214, 162, 66); stateZ = "disabled"; }
		}

		RECT dot = {PAD, 10, PAD + 10, 20};
		br = CreateSolidBrush(lamp);
		FillRect(dc, &dot, br);
		DeleteObject(br);

		SelectObject(dc, i_font_bold);
		SetTextColor(dc, RGB(238, 238, 238));
		RECT t = {PAD + 18, 5, right - 80, 25};
		DrawTextA(dc, "ONION SKIN", 10, &t, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

		SelectObject(dc, i_font);
		SetTextColor(dc, lamp);
		RECT ts = {right - 80, 5, right - PAD, 25};
		DrawTextA(dc, stateZ, (int)strlen(stateZ), &ts, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);

		Line(dc, 68, right);

		sprintf(buf, "%ld", (long)S_snap.prev);
		Row(dc, "Previous frames", buf, ROW0);
		sprintf(buf, "%ld", (long)S_snap.next);
		Row(dc, "Next frames", buf, ROW0 + ROWH);
		sprintf(buf, "%.0f%%", S_snap.strength);
		Row(dc, "Strength", buf, ROW0 + ROWH * 2);

		int y = ROW0 + ROWH * 3 + 2;
		Line(dc, y, right);
		y += 10;

		Swatch(dc, "Past",   S_snap.past_r, S_snap.past_g, S_snap.past_b, PAD, y);
		Swatch(dc, "Future", S_snap.next_r, S_snap.next_g, S_snap.next_b, PAD + 112, y);
		y += 28;

		if (!S_snap.has_compB) {
			y = Note(dc, "No comp open.", RGB(165, 165, 165), y, right);
		}

		if (S_snap.opaque_belowB) {
			y = Note(dc, "An opaque full-frame layer sits below the onion skin "
						"layer, so the ghosts will be invisible. Move the "
						"background above it, or make it a guide layer.",
						RGB(255, 190, 90), y, right);
		}

		if (S_snap.n_instances > 1) {
			sprintf(buf, "Driving %ld effect instances.", (long)S_snap.n_instances);
			y = Note(dc, buf, RGB(150, 150, 150), y, right);
		}

		if (S_snap.note[0]) {
			y = Note(dc, S_snap.note, RGB(255, 190, 90), y, right);
		}

		SelectObject(dc, old);
		EndPaint(h, &ps);
	}

	static A_Err S_Snap(AEGP_PanelRefcon, A_LPoint *s, A_long *nP)
	{ s[0].x = 268; s[0].y = 250; s[1].x = 340; s[1].y = 340; *nP = 2; return A_Err_NONE; }
	static A_Err S_Populate(AEGP_PanelRefcon, AEGP_FlyoutMenuItem *, A_long *nP)
	{ *nP = 0; return A_Err_NONE; }
	static A_Err S_Flyout(AEGP_PanelRefcon, AEGP_FlyoutMenuCmdID)
	{ return A_Err_NONE; }
};

/* ------------------------------------------------------------------ */
/*  Hooks                                                              */
/* ------------------------------------------------------------------ */

static A_Err
CreatePanelHook(AEGP_GlobalRefcon, AEGP_CreatePanelRefcon,
				AEGP_PlatformViewRef container, AEGP_PanelH panelH,
				AEGP_PanelFunctions1 *tableP, AEGP_PanelRefcon *outRefcon)
{
	*outRefcon = reinterpret_cast<AEGP_PanelRefcon>(new OSPanel(panelH, container, tableP));
	return A_Err_NONE;
}

static A_Err
CommandHook(AEGP_GlobalRefcon, AEGP_CommandRefcon, AEGP_Command cmd,
			AEGP_HookPriority, A_Boolean, A_Boolean *handledPB)
{
	if (cmd == S_cmd_panel && S_panelP) {
		S_panelP->AEGP_ToggleVisibility(S_panel_nameZ);
		*handledPB = TRUE;

	} else if (cmd == S_cmd_toggle) {
		//	The shortcut people will actually use. Queued like everything else.
		Queue(kReqToggleLayer);
		*handledPB = TRUE;

	} else if (cmd == S_cmd_more) {
		Queue(kReqBothUp);
		*handledPB = TRUE;

	} else if (cmd == S_cmd_fewer) {
		Queue(kReqBothDn);
		*handledPB = TRUE;
	}
	return A_Err_NONE;
}

static A_Err
UpdateMenuHook(AEGP_GlobalRefcon, AEGP_UpdateMenuRefcon, AEGP_WindowType)
{
	AEGP_SuiteHandler suites(sP);

	//	Enabled unconditionally. AE will not deliver a keyboard shortcut to a
	//	DISABLED command, so a conditional enable here would make the shortcuts
	//	dead exactly when they are wanted - B5's finding.
	suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_panel);
	suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_toggle);
	suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_more);
	suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_fewer);

	suites.CommandSuite1()->AEGP_CheckMarkMenuCommand(S_cmd_toggle,
														S_snap.has_layerB && S_snap.enabledB);
	return A_Err_NONE;
}

/* ------------------------------------------------------------------ */
/*  Entry                                                              */
/* ------------------------------------------------------------------ */

A_Err
EntryPointFunc(
	struct SPBasicSuite	*pica_basicP,
	A_long				major_versionL,
	A_long				minor_versionL,
	AEGP_PluginID		aegp_plugin_id,
	AEGP_GlobalRefcon	*global_refconP)
{
	A_Err err = A_Err_NONE;

	sP		= pica_basicP;
	S_my_id	= aegp_plugin_id;

	AEFX_CLR_STRUCT(S_snap);
	Log("STARTUP AE %d.%d, plugin id %d", (int)major_versionL, (int)minor_versionL, (int)S_my_id);

	AEGP_SuiteHandler suites(sP);

	err = sP->AcquireSuite(kAEGPPanelSuite, kAEGPPanelSuiteVersion1,
							(const void **)&S_panelP);
	if (err || !S_panelP) {
		Log("FAIL    no panel suite (err %d)", (int)err);
		return err ? err : A_Err_GENERIC;
	}

	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_panel));
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_toggle));
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_more));
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_fewer));

	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_cmd_panel, OSP_PANEL_MENU,
														AEGP_Menu_WINDOW, AEGP_MENU_INSERT_SORTED));
	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_cmd_toggle, OSP_CMD_TOGGLE,
														AEGP_Menu_ANIMATION, AEGP_MENU_INSERT_AT_BOTTOM));
	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_cmd_more, OSP_CMD_MORE,
														AEGP_Menu_ANIMATION, AEGP_MENU_INSERT_AT_BOTTOM));
	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_cmd_fewer, OSP_CMD_FEWER,
														AEGP_Menu_ANIMATION, AEGP_MENU_INSERT_AT_BOTTOM));

	ERR(suites.RegisterSuite5()->AEGP_RegisterCommandHook(S_my_id, AEGP_HP_BeforeAE, S_cmd_panel,  CommandHook, NULL));
	ERR(suites.RegisterSuite5()->AEGP_RegisterCommandHook(S_my_id, AEGP_HP_BeforeAE, S_cmd_toggle, CommandHook, NULL));
	ERR(suites.RegisterSuite5()->AEGP_RegisterCommandHook(S_my_id, AEGP_HP_BeforeAE, S_cmd_more,   CommandHook, NULL));
	ERR(suites.RegisterSuite5()->AEGP_RegisterCommandHook(S_my_id, AEGP_HP_BeforeAE, S_cmd_fewer,  CommandHook, NULL));
	ERR(suites.RegisterSuite5()->AEGP_RegisterUpdateMenuHook(S_my_id, UpdateMenuHook, NULL));
	ERR(suites.RegisterSuite5()->AEGP_RegisterIdleHook(S_my_id, IdleHook, NULL));

	ERR(S_panelP->AEGP_RegisterCreatePanelHook(S_my_id, S_panel_nameZ,
												CreatePanelHook, NULL, true));

	Log(err ? "FAIL    registration err %d" : "READY   panel + 4 commands", (int)err);

	*global_refconP = NULL;
	return err;
}
