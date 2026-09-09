/*
	osB3_PiPL.r - TWO PiPL resources in one file.

	16000 declares the effect, 16001 declares the AEGP. This file IS spike B1:
	if AE loads the resulting .aex and both halves come alive, the answer to
	"one binary or two?" is one.

	If AE loads only the effect, or only the AEGP, or neither, split them into
	two projects and let the AEGP find the effect by match name. The product
	does not depend on the answer - only the file count does.
*/

#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include "AE_General.r"
#endif

/*
	B1 came back TWO BINARIES. AE loaded this file's first PiPL (the effect) and
	never called EntryPointFunc -- one .aex is claimed by one kind. So the halves
	ship separately and find each other by name: the panel by its match name, the
	effect by its own. Nothing about the product changes; only the file count.
*/

resource 'PiPL' (16000) {
	{
		Kind {
			AEGP
		},
		Name {
			"Onion Skin B3 Panel"
		},
		Category {
			"General Plugin"
		},
		Version {
			65536
		},
#ifdef AE_OS_WIN
    #if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EntryPointFunc"},
    #elif defined(AE_PROC_ARM64)
		CodeWinARM64 {"EntryPointFunc"},
    #endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 {"EntryPointFunc"},
		CodeMacARM64 {"EntryPointFunc"},
#endif
	}
};
