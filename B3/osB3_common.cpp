/*
	osB3_common.cpp - the pieces BOTH binaries need.

	Compiled into osB3fx.aex and osB3panel.aex alike. It exists because B1 came
	back "two binaries": AE loaded the effect PiPL and never called the AEGP
	entry point, so the two halves ship as separate files and each needs its own
	copy of the log and the panel-opening call.

	Nothing in here holds AE state. It takes pica_basicP as an argument every
	time, precisely so it can be linked into two plug-ins that acquire their
	suites independently.
*/

#include "osB3.h"

static char S_log_path[AEGP_MAX_PATH_SIZE] = {'\0'};

//	Both binaries append to ONE log. That is deliberate: the interleaving is how
//	you see the effect half and the panel half acting on the same param, and a
//	line's prefix (FX / WRITE / STARTUP) says which binary wrote it.

void
OSB3_ResolveLogPath()
{
	if (S_log_path[0]) return;

	const char *tmp = getenv("TEMP");
	if (!tmp) tmp = getenv("TMP");
	if (!tmp) tmp = ".";
	sprintf(S_log_path, "%s\\%s", tmp, OS_B3_LOG_LEAF);
}

void
OSB3_Log(const char *fmt, ...)
{
	if (!S_log_path[0]) OSB3_ResolveLogPath();

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

A_Err
OSB3_ShowPanel(SPBasicSuite *pica_basicP)
{
	A_Err				err		= A_Err_NONE;
	AEGP_PanelSuite1	*panelP	= NULL;
	A_Boolean			shownB = FALSE, frontB = FALSE;

	const A_u_char *match_nameZ = reinterpret_cast<const A_u_char *>(OS_B3_MATCH_NAME);

	//	Acquired locally every call. When this runs from the EFFECT binary there
	//	is no AEGP global state to borrow from at all - the panel lives in the
	//	other .aex - so the suite is the only thing connecting them, and the
	//	match name is the address.
	ERR(pica_basicP->AcquireSuite(kAEGPPanelSuite, kAEGPPanelSuiteVersion1,
									(const void **)&panelP));
	if (err || !panelP) {
		OSB3_Log("FX      panel suite unavailable (err %d)", (int)err);
		return err;
	}

	ERR(panelP->AEGP_IsShown(match_nameZ, &shownB, &frontB));

	//	Only toggle when it is not already up. AEGP_ToggleVisibility would CLOSE
	//	an open panel, which is the wrong thing for a button labelled "Open".
	if (!err && !(shownB && frontB)) {
		ERR(panelP->AEGP_ToggleVisibility(match_nameZ));
	}

	pica_basicP->ReleaseSuite(kAEGPPanelSuite, kAEGPPanelSuiteVersion1);
	return err;
}
