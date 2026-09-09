#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include "AE_General.r"
#endif

resource 'PiPL' (16000) {
	{
		Kind {
			AEEffect
		},
		Name {
			"Onion Skin"
		},
		Category {
			"ags_utilities"
		},
#ifdef AE_OS_WIN
    #if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EffectMain"},
    #elif defined(AE_PROC_ARM64)
		CodeWinARM64 {"EffectMain"},
    #endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
#endif
		AE_PiPL_Version {
			2,
			0
		},
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		/*	PF_VERSION bit-packing, NOT the AEGP 'Version' convention -- the two
			look alike and are not. 1.2.0 build 3 = 589827.
			MUST move in lockstep with PF_VERSION() in GlobalSetup. */
		AE_Effect_Version {
			589827
		},
		AE_Effect_Info_Flags {
			0
		},
		/*	DEEP_COLOR_AWARE (1<<25) | SEND_UPDATE_PARAMS_UI (1<<26) |
			WIDE_TIME_INPUT (1<<1). WIDE_TIME_INPUT declares that we read the
			input at times other than the current one; without it AE caches our
			output as if it depended only on this frame and serves stale ghosts
			until a manual purge. Must match GlobalSetup's out_flags exactly.
			Deliberately NOT PIX_INDEPENDENT: an output pixel depends on other
			FRAMES, so it is not a function of the co-located input pixel. */
		AE_Effect_Global_OutFlags {
			0x06000002
		},
		AE_Effect_Global_OutFlags_2 {
			0x00000000
		},
		AE_Effect_Match_Name {
			"aldai OnionSkin"
		},
		AE_Reserved_Info {
			0
		}
	}
};
