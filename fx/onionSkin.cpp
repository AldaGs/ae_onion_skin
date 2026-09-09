/*
	onionSkin.cpp - Phase 1, the effect. See onionSkin.h for the model.

	RENDER SHAPE

	    output = 0
	    for k, farthest first:  output = skin_k OVER output
	    output = C(t) OVER output

	Compositing runs over the OUTPUT world and reads each skin by manual
	indexing rather than by iterating the skin world. That is deliberate: a
	checked-out layer need not share the output's dimensions, and iterating it as
	the source would either crash or silently mis-register. Reading it with an
	explicit bounds test treats "outside the skin" as transparent, which is what
	it is.

	PASS-THROUGH IS EXACT

	Enable off, Strength 0, or no skins requested all copy the input verbatim.
	The proto made that an invariant worth having (its check 3), so it is a
	single early exit here rather than an emergent property of the maths.
*/

#include "onionSkin.h"

#include <stdarg.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Diagnostics                                                        */
/* ------------------------------------------------------------------ */

static bool S_logB = false;

void
OS_Log(const char *fmt, ...)
{
	if (!S_logB) return;

	static char pathZ[512] = {'\0'};
	if (!pathZ[0]) {
		const char *tmp = getenv("TEMP");
		if (!tmp) tmp = getenv("TMP");
		if (!tmp) tmp = ".";
		sprintf(pathZ, "%s\\%s", tmp, OS_LOG_LEAF);
	}

	FILE *f = fopen(pathZ, "a");
	if (!f) return;

	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fprintf(f, "\n");
	fclose(f);
}

/* ------------------------------------------------------------------ */
/*  Setup                                                              */
/* ------------------------------------------------------------------ */

static PF_Err
About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *[], PF_LayerDef *)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg,
		"%s v%d.%d\r"
		"Ghosts of neighbouring frames, drawn under the current one.\r"
		"Set a Source Layer to work over any background.",
		OS_NAME, OS_MAJOR, OS_MINOR);
	return PF_Err_NONE;
}

static PF_Err
GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *[], PF_LayerDef *)
{
	out_data->my_version = PF_VERSION(OS_MAJOR, OS_MINOR, OS_BUG, OS_STAGE, OS_BUILD);

	//	WIDE_TIME_INPUT is the one that matters, and its absence was the v1.1
	//	defect: "Set this flag if the effect calls get_param to inquire a
	//	parameter at a time besides the current one (e.g. to get the previous
	//	video frame)" -- AE_Effect.h, describing this effect exactly. Without it
	//	AE does not know our output depends on OTHER FRAMES, so it happily serves
	//	a cached frame that was rendered before the neighbours changed. Run 2
	//	reported ghosts that only updated after a manual cache purge, which is
	//	precisely what a missed frame dependency looks like from the outside.
	//
	//	NOT PIX_INDEPENDENT: an output pixel depends on other frames, so it is
	//	emphatically not a function of the co-located input pixel alone.
	//	SEND_UPDATE_PARAMS_UI is what makes the Open Panel button's
	//	PF_Cmd_USER_CHANGED_PARAM arrive at all.
	out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE |
							PF_OutFlag_SEND_UPDATE_PARAMS_UI |
							PF_OutFlag_WIDE_TIME_INPUT;
	return PF_Err_NONE;
}

static PF_Err
ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *[], PF_LayerDef *)
{
	PF_Err		err = PF_Err_NONE;
	PF_ParamDef	def;

	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOX("", "Enable", TRUE, 0, OS_ENABLE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER("Previous Frames", 0, OS_MAX_SKINS, 0, 8, OS_PREV_DFLT, OS_PREV_FRAMES);

	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER("Next Frames", 0, OS_MAX_SKINS, 0, 8, OS_NEXT_DFLT, OS_NEXT_FRAMES);

	//	Spacing. Step 2 skins every other frame - the usual want on 2s.
	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER("Frame Step", 1, 10, 1, 6, OS_STEP_DFLT, OS_FRAME_STEP);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Strength", 0, 100, 0, 100, OS_STRENGTH_DFLT,
							PF_Precision_INTEGER, 0, 0, OS_STRENGTH);

	//	100 = every skin equally strong; lower fades with distance.
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Falloff", 0, 100, 0, 100, OS_FALLOFF_DFLT,
							PF_Precision_INTEGER, 0, 0, OS_FALLOFF);

	//	100 = flat coloured ghost; 0 = the drawing's own colours, only faded.
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Tint Amount", 0, 100, 0, 100, OS_TINT_DFLT,
							PF_Precision_INTEGER, 0, 0, OS_TINT_AMOUNT);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Past Colour", 242, 64, 64, OS_PAST_COLOR);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Future Colour", 64, 178, 242, OS_FUTURE_COLOR);

	//	Optional. When none is set the skins come from the effect's own input,
	//	which is correct on a drawing layer and correct on an adjustment layer
	//	ONLY if everything below carries alpha.
	AEFX_CLR_STRUCT(def);
	PF_ADD_LAYER("Source Layer 1", PF_LayerDefault_NONE, OS_SOURCE_1);

	AEFX_CLR_STRUCT(def);
	PF_ADD_LAYER("Source Layer 2", PF_LayerDefault_NONE, OS_SOURCE_2);

	AEFX_CLR_STRUCT(def);
	PF_ADD_LAYER("Source Layer 3", PF_LayerDefault_NONE, OS_SOURCE_3);

	AEFX_CLR_STRUCT(def);
	def.flags = PF_ParamFlag_SUPERVISE;
	PF_ADD_BUTTON("Panel", "Open Onion Skin Panel", 0, PF_ParamFlag_SUPERVISE,
					OS_OPEN_PANEL);

	//	Appended in v1.1. Writes %TEMP%\onionskin_fx.txt describing what each
	//	render actually received. Off by default.
	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOX("", "Debug Log", FALSE, 0, OS_DEBUG_LOG);

	out_data->num_params = OS_NUM_PARAMS;
	return err;
}

/* ------------------------------------------------------------------ */
/*  The launcher                                                       */
/* ------------------------------------------------------------------ */

static PF_Err
UserChangedParam(PF_InData *in_data, const PF_UserChangedParamExtra *extraP)
{
	PF_Err				err		= PF_Err_NONE;
	AEGP_PanelSuite1	*panelP	= NULL;

	if (extraP->param_index != OS_OPEN_PANEL) return err;

	const A_u_char *nameZ = reinterpret_cast<const A_u_char *>(OS_PANEL_MATCH_NAME);

	//	An effect reaching an AEGP suite through pica_basicP is ordinary and
	//	already proven in this tree (B3, and BTSOverlay before it).
	err = in_data->pica_basicP->AcquireSuite(kAEGPPanelSuite, kAEGPPanelSuiteVersion1,
												(const void **)&panelP);
	if (err || !panelP) return PF_Err_NONE;		// no panel installed: not fatal

	A_Boolean shownB = FALSE, frontB = FALSE;
	if (!panelP->AEGP_IsShown(nameZ, &shownB, &frontB)) {
		//	Only open when it is not already up. ToggleVisibility alone would
		//	CLOSE an open panel, which is wrong for a button labelled "Open".
		if (!(shownB && frontB)) {
			panelP->AEGP_ToggleVisibility(nameZ);
		}
	}
	in_data->pica_basicP->ReleaseSuite(kAEGPPanelSuite, kAEGPPanelSuiteVersion1);
	return PF_Err_NONE;
}

/* ------------------------------------------------------------------ */
/*  Compositing                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
	PF_EffectWorld	*skinP;			// the frame being laid down
	double			opacity;		// 0..1, already includes falloff
	double			tr, tg, tb;		// tint colour, 0..1
	double			tint_amount;	// 0..1
} SkinInfo;

//	Straight-alpha source-over of one skin pixel onto the accumulator.
//
//	The transparent-pixel rule is carried from the proto: where the resulting
//	alpha is ~0 the colour is mathematically arbitrary, and writing black there
//	would mean Strength=0 is not a bit-exact pass-through, because AE's buffers
//	carry real RGB beneath transparent pixels. Keep the source's colour instead.
static void
CompositeStraight(double sa, double sr, double sg, double sb,
					double da, double dr, double dg, double db,
					double *oaP, double *orP, double *ogP, double *obP)
{
	double oa = sa + da * (1.0 - sa);

	if (oa > 1e-6) {
		double w = da * (1.0 - sa);
		*orP = (sr * sa + dr * w) / oa;
		*ogP = (sg * sa + dg * w) / oa;
		*obP = (sb * sa + db * w) / oa;
	} else {
		*orP = sr; *ogP = sg; *obP = sb;
	}
	*oaP = oa;
}

static PF_Err
SkinOver8(void *refcon, A_long x, A_long y, PF_Pixel8 *, PF_Pixel8 *outP)
{
	SkinInfo *siP = (SkinInfo *)refcon;
	PF_EffectWorld *sw = siP->skinP;

	//	Outside the skin world is transparent, not an error. A checked-out layer
	//	need not match the output's dimensions.
	if (x < 0 || y < 0 || x >= sw->width || y >= sw->height) return PF_Err_NONE;

	PF_Pixel8 *sP = (PF_Pixel8 *)((char *)sw->data + (size_t)y * sw->rowbytes) + x;

	double ta = siP->tint_amount;
	double sa = (sP->alpha / (double)PF_MAX_CHAN8) * siP->opacity;
	double sr = (sP->red   / (double)PF_MAX_CHAN8) * (1.0 - ta) + siP->tr * ta;
	double sg = (sP->green / (double)PF_MAX_CHAN8) * (1.0 - ta) + siP->tg * ta;
	double sb = (sP->blue  / (double)PF_MAX_CHAN8) * (1.0 - ta) + siP->tb * ta;

	double oa, orr, og, ob;
	CompositeStraight(sa, sr, sg, sb,
						outP->alpha / (double)PF_MAX_CHAN8,
						outP->red   / (double)PF_MAX_CHAN8,
						outP->green / (double)PF_MAX_CHAN8,
						outP->blue  / (double)PF_MAX_CHAN8,
						&oa, &orr, &og, &ob);

	outP->alpha = (A_u_char)(MIN(1.0, MAX(0.0, oa))  * PF_MAX_CHAN8 + 0.5);
	outP->red   = (A_u_char)(MIN(1.0, MAX(0.0, orr)) * PF_MAX_CHAN8 + 0.5);
	outP->green = (A_u_char)(MIN(1.0, MAX(0.0, og))  * PF_MAX_CHAN8 + 0.5);
	outP->blue  = (A_u_char)(MIN(1.0, MAX(0.0, ob))  * PF_MAX_CHAN8 + 0.5);
	return PF_Err_NONE;
}

static PF_Err
SkinOver16(void *refcon, A_long x, A_long y, PF_Pixel16 *, PF_Pixel16 *outP)
{
	SkinInfo *siP = (SkinInfo *)refcon;
	PF_EffectWorld *sw = siP->skinP;

	if (x < 0 || y < 0 || x >= sw->width || y >= sw->height) return PF_Err_NONE;

	PF_Pixel16 *sP = (PF_Pixel16 *)((char *)sw->data + (size_t)y * sw->rowbytes) + x;

	double ta = siP->tint_amount;
	double sa = (sP->alpha / (double)PF_MAX_CHAN16) * siP->opacity;
	double sr = (sP->red   / (double)PF_MAX_CHAN16) * (1.0 - ta) + siP->tr * ta;
	double sg = (sP->green / (double)PF_MAX_CHAN16) * (1.0 - ta) + siP->tg * ta;
	double sb = (sP->blue  / (double)PF_MAX_CHAN16) * (1.0 - ta) + siP->tb * ta;

	double oa, orr, og, ob;
	CompositeStraight(sa, sr, sg, sb,
						outP->alpha / (double)PF_MAX_CHAN16,
						outP->red   / (double)PF_MAX_CHAN16,
						outP->green / (double)PF_MAX_CHAN16,
						outP->blue  / (double)PF_MAX_CHAN16,
						&oa, &orr, &og, &ob);

	outP->alpha = (A_u_short)(MIN(1.0, MAX(0.0, oa))  * PF_MAX_CHAN16 + 0.5);
	outP->red   = (A_u_short)(MIN(1.0, MAX(0.0, orr)) * PF_MAX_CHAN16 + 0.5);
	outP->green = (A_u_short)(MIN(1.0, MAX(0.0, og))  * PF_MAX_CHAN16 + 0.5);
	outP->blue  = (A_u_short)(MIN(1.0, MAX(0.0, ob))  * PF_MAX_CHAN16 + 0.5);
	return PF_Err_NONE;
}

//	Lay one world down onto the output.
static PF_Err
LayDown(PF_InData *in_data, PF_LayerDef *output, PF_EffectWorld *skinP,
		double opacity, double tr, double tg, double tb, double tint_amount)
{
	PF_Err				err = PF_Err_NONE;
	AEGP_SuiteHandler	suites(in_data->pica_basicP);
	SkinInfo			si;
	A_long				lines = output->height;

	if (!skinP || !skinP->data || opacity <= 0.0) return err;

	AEFX_CLR_STRUCT(si);
	si.skinP		= skinP;
	si.opacity		= opacity;
	si.tr			= tr;
	si.tg			= tg;
	si.tb			= tb;
	si.tint_amount	= tint_amount;

	//	src and dst are both the output world: the callback ignores its input
	//	pixel and reads the skin by index instead. See the file header.
	if (PF_WORLD_IS_DEEP(output)) {
		ERR(suites.Iterate16Suite2()->iterate(in_data, 0, lines, output, NULL,
												(void *)&si, SkinOver16, output));
	} else {
		ERR(suites.Iterate8Suite2()->iterate(in_data, 0, lines, output, NULL,
												(void *)&si, SkinOver8, output));
	}
	return err;
}

/* ------------------------------------------------------------------ */
/*  Render                                                             */
/* ------------------------------------------------------------------ */

static void
ColorToUnit(const PF_Pixel *cP, double *r, double *g, double *b)
{
	*r = cP->red   / (double)PF_MAX_CHAN8;
	*g = cP->green / (double)PF_MAX_CHAN8;
	*b = cP->blue  / (double)PF_MAX_CHAN8;
}

static PF_Err
Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_Err		err		= PF_Err_NONE;
	A_long		prev	= params[OS_PREV_FRAMES]->u.sd.value;
	A_long		next	= params[OS_NEXT_FRAMES]->u.sd.value;
	A_long		step	= MAX(1, params[OS_FRAME_STEP]->u.sd.value);
	double		strength = params[OS_STRENGTH]->u.fs_d.value / 100.0;
	double		falloff	= params[OS_FALLOFF]->u.fs_d.value / 100.0;
	double		tint_am	= params[OS_TINT_AMOUNT]->u.fs_d.value / 100.0;
	A_Boolean	enabled	= params[OS_ENABLE]->u.bd.value;

	S_logB = (params[OS_DEBUG_LOG]->u.bd.value != 0);
	OS_Log("RENDER t=%ld step=%ld prev=%ld next=%ld fstep=%ld strength=%.2f deep=%d",
			(long)in_data->current_time, (long)in_data->time_step,
			(long)prev, (long)next, (long)step, strength,
			(int)PF_WORLD_IS_DEEP(output));

	//	Exact pass-through. Made an early exit rather than left to emerge from
	//	the maths, because "zero strength returns the input untouched" is an
	//	invariant the proto established and the port should be testable against.
	if (!enabled || strength <= 0.0 || (prev == 0 && next == 0)) {
		return PF_COPY(&params[OS_INPUT]->u.ld, output, NULL, NULL);
	}

	double pr, pg, pb, nr, ng, nb;
	ColorToUnit(&params[OS_PAST_COLOR]->u.cd.value,   &pr, &pg, &pb);
	ColorToUnit(&params[OS_FUTURE_COLOR]->u.cd.value, &nr, &ng, &nb);

	//	Which params supply the skins. Any Source Layer set wins; otherwise the
	//	effect's own input. See onionSkin.h for why that distinction exists.
	A_long	sources[OS_MAX_SOURCES];
	A_long	n_sources = 0;

	//	A layer param's u.ld is NOT populated by AE before render. v1.1 tested
	//	params[idx]->u.ld.data and always saw NULL/0x0, so every Source Layer
	//	looked unset and the effect silently fell back to its own input -- which
	//	on an adjustment layer over an opaque background is invisible, and looked
	//	like "selecting a source does nothing".
	//
	//	The SDK's own Checkout sample never reads params[CHECK_LAYER] at all; it
	//	only ever calls PF_CHECKOUT_PARAM. Checking out IS the test.
	for (A_long s = 0; s < OS_MAX_SOURCES; s++) {
		A_long		idx = OS_SOURCE_1 + s;
		PF_ParamDef	probe;
		PF_Err		perr;

		AEFX_CLR_STRUCT(probe);
		perr = PF_CHECKOUT_PARAM(in_data, idx, in_data->current_time,
									in_data->time_step, in_data->time_scale, &probe);

		A_Boolean liveB = (!perr && probe.u.ld.data != NULL);

		OS_Log("  source param %ld: probe_err=%d  data=%s  %ldx%ld",
				(long)idx, (int)perr, liveB ? "yes" : "NO",
				liveB ? (long)probe.u.ld.width  : 0L,
				liveB ? (long)probe.u.ld.height : 0L);

		if (liveB) {
			sources[n_sources++] = idx;
		}
		if (!perr) {
			PF_CHECKIN_PARAM(in_data, &probe);
		}
	}
	if (n_sources == 0) {
		//	Fall back to the effect's own input. On a drawing layer that is
		//	right; on an adjustment layer over an opaque background it yields
		//	nothing visible, which is the finding that put the Source params here.
		sources[n_sources++] = OS_INPUT;
	}
	OS_Log("  resolved %ld source(s); first=%ld", (long)n_sources, (long)sources[0]);

	//	Start empty, then lay skins farthest-first so nearer frames occlude
	//	further ones, and C(t) lands last.
	ERR(PF_FILL(NULL, NULL, output));

	A_long maxd = MAX(prev, next);
	A_long laid = 0;

	//	NOTE the loop conditions carry no !err guard, and that is the fix for
	//	the v1.0 defect where ghosting stopped dead at the ends of the timeline
	//	instead of thinning out. Run 1: at the last frame the FUTURE checkout
	//	failed, err stuck, and every remaining skin -- including the PAST ones
	//	at nearer distances, which were perfectly available -- was skipped, as
	//	was the final C(t) composite.
	//
	//	The absence of a frame is not an error condition. A checkout that fails
	//	off the end of the timeline means "no frame there", and the only correct
	//	response is to lay nothing down and carry on.
	for (A_long d = maxd; d >= 1; d--) {
		//	At equal distance, past then future, so the future skin reads as
		//	sitting on top of the past one.
		for (int which = 0; which < 2; which++) {
			A_long k = (which == 0) ? -d : d;

			if (k < 0 && d > prev) continue;
			if (k > 0 && d > next) continue;

			double opacity = strength * pow(falloff, (double)(d - 1));
			if (opacity <= 0.0) continue;

			A_long offset = k * step * in_data->time_step;

			for (A_long s = 0; s < n_sources; s++) {
				PF_ParamDef	checked;
				PF_Err		cerr = PF_Err_NONE;

				AEFX_CLR_STRUCT(checked);

				//	Exactly the SDK Checkout sample's move: a layer param pulled
				//	at current_time + n * time_step.
				cerr = PF_CHECKOUT_PARAM(in_data, sources[s],
											in_data->current_time + offset,
											in_data->time_step, in_data->time_scale,
											&checked);

				OS_Log("    k=%+ld src_param=%ld  checkout_err=%d  data=%s  %ldx%ld",
						(long)k, (long)sources[s], (int)cerr,
						(!cerr && checked.u.ld.data) ? "yes" : "NO",
						(!cerr) ? (long)checked.u.ld.width  : 0L,
						(!cerr) ? (long)checked.u.ld.height : 0L);

				if (!cerr) {
					if (checked.u.ld.data) {
						//	A LayDown failure IS worth propagating - that is our
						//	own compositing, not the timeline running out.
						ERR(LayDown(in_data, output, &checked.u.ld, opacity,
									(k < 0) ? pr : nr,
									(k < 0) ? pg : ng,
									(k < 0) ? pb : nb,
									tint_am));
						laid++;
					}
					PF_CHECKIN_PARAM(in_data, &checked);
				}
				//	cerr deliberately swallowed. See the note above the loop.
			}
		}
	}

	//	C(t) last, untouched: opacity 1, no tint. Runs whatever happened above.
	ERR(LayDown(in_data, output, &params[OS_INPUT]->u.ld, 1.0, 0, 0, 0, 0.0));

	OS_Log("  laid %ld skin(s); final err=%d", (long)laid, (int)err);
	return err;
}

/* ------------------------------------------------------------------ */
/*  Entry                                                              */
/* ------------------------------------------------------------------ */

PF_Err
EffectMain(
	PF_Cmd			cmd,
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output,
	void			*extra)
{
	PF_Err err = PF_Err_NONE;

	try {
		switch (cmd) {
			case PF_Cmd_ABOUT:
				err = About(in_data, out_data, params, output);
				break;
			case PF_Cmd_GLOBAL_SETUP:
				err = GlobalSetup(in_data, out_data, params, output);
				break;
			case PF_Cmd_PARAMS_SETUP:
				err = ParamsSetup(in_data, out_data, params, output);
				break;
			case PF_Cmd_USER_CHANGED_PARAM:
				err = UserChangedParam(in_data,
							reinterpret_cast<const PF_UserChangedParamExtra *>(extra));
				break;
			case PF_Cmd_RENDER:
				err = Render(in_data, out_data, params, output);
				break;
		}
	} catch (PF_Err &thrown_err) {
		err = thrown_err;
	}
	return err;
}
