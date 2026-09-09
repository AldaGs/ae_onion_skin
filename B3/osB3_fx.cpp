/*
	osB3_fx.cpp - the stub EFFECT half of spike B3.

	Its entire job is to have a param worth writing and to render something that
	makes a write VISIBLE. Brightness was chosen over anything cleverer for one
	reason: a wrong value is unmistakable at a glance, so "did the viewer
	repaint?" needs no measurement instrument beyond an eye.

	There is no onion-skin logic here. That is Phase 1.
*/

#include "osB3.h"

/* ------------------------------------------------------------------ */
/*  Setup                                                              */
/* ------------------------------------------------------------------ */

static PF_Err
About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *[], PF_LayerDef *)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg,
		"%s\rPhase 0 spike. Not the product.", OS_B3_FX_NAME);
	return PF_Err_NONE;
}

static PF_Err
GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *[], PF_LayerDef *)
{
	out_data->my_version	= PF_VERSION(1, 0, 0, PF_Stage_DEVELOP, 0);
	out_data->out_flags		= PF_OutFlag_DEEP_COLOR_AWARE |
								PF_OutFlag_PIX_INDEPENDENT |
								//	Required for the button: without it AE never
								//	sends PF_Cmd_USER_CHANGED_PARAM and the
								//	launcher is silently dead.
								PF_OutFlag_SEND_UPDATE_PARAMS_UI;
	return PF_Err_NONE;
}

static PF_Err
ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *[], PF_LayerDef *)
{
	PF_Err			err = PF_Err_NONE;
	PF_ParamDef		def;

	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER("Brightness", OS_B3_BRIGHT_MIN, OS_B3_BRIGHT_MAX,
					OS_B3_BRIGHT_MIN, OS_B3_BRIGHT_MAX,
					OS_B3_BRIGHT_DFLT, OS_B3_BRIGHTNESS);

	AEFX_CLR_STRUCT(def);
	//	PF_Param_BUTTON "must combine with PF_ParamFlag_SUPERVISE" (AE_Effect.h).
	//	Without SUPERVISE the click never reaches PF_Cmd_USER_CHANGED_PARAM.
	def.flags = PF_ParamFlag_SUPERVISE;
	PF_ADD_BUTTON("Panel", "Open Onion Skin Panel", 0, PF_ParamFlag_SUPERVISE,
					OS_B3_OPEN_PANEL);

	out_data->num_params = OS_B3_NUM_PARAMS;
	return err;
}

/* ------------------------------------------------------------------ */
/*  The launcher                                                       */
/* ------------------------------------------------------------------ */

static PF_Err
UserChangedParam(PF_InData *in_data, PF_OutData *out_data,
					PF_ParamDef *params[], const PF_UserChangedParamExtra *extraP)
{
	PF_Err err = PF_Err_NONE;

	if (extraP->param_index == OS_B3_OPEN_PANEL) {
		//	An effect reaching an AEGP suite through in_data->pica_basicP is an
		//	ordinary, documented move - BTSOverlay in this same tree already does
		//	it. What is new here is using it to open OUR panel, which is what
		//	makes the effect able to summon its own UI.
		OSB3_Log("FX      Open Panel button pressed");
		err = OSB3_ShowPanel(in_data->pica_basicP);
		if (err) {
			OSB3_Log("FX      ShowPanel failed, err %d", (int)err);
		}
	}
	return err;
}

/* ------------------------------------------------------------------ */
/*  Render                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
	A_long	scale_num;		// brightness, 0..200
} BrightInfo;

static PF_Err
Bright8(void *refcon, A_long x, A_long y, PF_Pixel8 *inP, PF_Pixel8 *outP)
{
	BrightInfo	*biP = (BrightInfo *)refcon;
	A_long		r, g, b;

	r = ((A_long)inP->red   * biP->scale_num) / 100;
	g = ((A_long)inP->green * biP->scale_num) / 100;
	b = ((A_long)inP->blue  * biP->scale_num) / 100;

	outP->alpha = inP->alpha;
	outP->red	= (A_u_char)MIN(PF_MAX_CHAN8, r);
	outP->green	= (A_u_char)MIN(PF_MAX_CHAN8, g);
	outP->blue	= (A_u_char)MIN(PF_MAX_CHAN8, b);

	return PF_Err_NONE;
}

static PF_Err
Bright16(void *refcon, A_long x, A_long y, PF_Pixel16 *inP, PF_Pixel16 *outP)
{
	BrightInfo	*biP = (BrightInfo *)refcon;
	A_long		r, g, b;

	r = ((A_long)inP->red   * biP->scale_num) / 100;
	g = ((A_long)inP->green * biP->scale_num) / 100;
	b = ((A_long)inP->blue  * biP->scale_num) / 100;

	outP->alpha = inP->alpha;
	outP->red	= (A_u_short)MIN(PF_MAX_CHAN16, r);
	outP->green	= (A_u_short)MIN(PF_MAX_CHAN16, g);
	outP->blue	= (A_u_short)MIN(PF_MAX_CHAN16, b);

	return PF_Err_NONE;
}

static PF_Err
Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_Err				err = PF_Err_NONE;
	AEGP_SuiteHandler	suites(in_data->pica_basicP);
	BrightInfo			bi;
	A_long				linesL = output->extent_hint.bottom - output->extent_hint.top;

	AEFX_CLR_STRUCT(bi);
	bi.scale_num = params[OS_B3_BRIGHTNESS]->u.sd.value;

	if (PF_WORLD_IS_DEEP(output)) {
		ERR(suites.Iterate16Suite2()->iterate(in_data, 0, linesL,
					&params[OS_B3_INPUT]->u.ld, NULL, (void *)&bi, Bright16, output));
	} else {
		ERR(suites.Iterate8Suite2()->iterate(in_data, 0, linesL,
					&params[OS_B3_INPUT]->u.ld, NULL, (void *)&bi, Bright8, output));
	}
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
				OSB3_ResolveLogPath();
				OSB3_Log("FX      GLOBAL_SETUP - the effect half loaded");
				err = GlobalSetup(in_data, out_data, params, output);
				break;
			case PF_Cmd_PARAMS_SETUP:
				err = ParamsSetup(in_data, out_data, params, output);
				break;
			case PF_Cmd_USER_CHANGED_PARAM:
				err = UserChangedParam(in_data, out_data, params,
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
