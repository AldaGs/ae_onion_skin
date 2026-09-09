/*
	A1a_WindowProbe.cpp (Onion Skin) - throwaway .exe for Phase 0, spike A1.

	THE QUESTION THIS ANSWERS, AND ONLY THIS ONE:

	  Is the comp viewer's zoom (and scroll offset) readable from AE's window
	  tree?

	A1 needs a comp->screen transform. AEGP has no source for one: the whole of
	AEGP_ItemViewSuite1 is AEGP_GetItemViewPlaybackTime, and the only other
	thing that takes an AEGP_ItemViewP is a colour-space transform. So the
	ANALYTIC route in the roadmap lives or dies on whether the UI itself will
	tell us - a magnification combo we can read, a scrollbar whose thumb
	position gives us pan.

	If this probe finds them, A1 is cheap. If it finds nothing but
	'DroverLord - Window Class' all the way down, the analytic route is dead
	and A1 falls to the empirical route (screen-capture correlation), which is
	a much bigger spike and a much weaker result.

	This measures. It draws no conclusions and it changes nothing in AE.

	Deliberately dumb, in the S3B tradition: no error recovery, no cleanup. It
	walks, it prints, it dies.

	Build (from this directory, any Developer prompt):
	    cl /nologo /EHsc /DUNICODE /D_UNICODE A1a_WindowProbe.cpp \
	       /link user32.lib /OUT:os_A1a.exe

	Usage:
	    os_A1a.exe              walk AE, print the whole tree + findings
	    os_A1a.exe > tree.txt   same, captured

	Read the FINDINGS block at the end first; the tree is there for when the
	findings are empty and you need to see why.
*/

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

//	AE's main window class is version-stamped (AE_CApplication_26.3), so we
//	never name it - pieFX S2 established that. We find the process instead.
static const wchar_t *kAEProcess = L"AfterFX.exe";

struct Found {
	HWND	hwnd;
	int		depth;
	wchar_t	cls[128];
	wchar_t	txt[256];
	RECT	rc;
	bool	visible;
};

static const int	kMaxFound = 4096;
static Found		g_found[kMaxFound];
static int			g_count = 0;

//	Candidates worth shouting about, collected during the walk.
static int	g_zoomLike[512];	static int g_zoomCount = 0;
static int	g_scrollLike[512];	static int g_scrollCount = 0;
static int	g_nonDrover[4096];	static int g_nonDroverCount = 0;

static DWORD g_aePid = 0;

/* ------------------------------------------------------------------ */
/*  Does this string look like a magnification readout?                */
/*  '100%', '50%', '66.7%', 'Fit', 'Fit up to 100%'                    */
/* ------------------------------------------------------------------ */
static bool
LooksLikeZoom(const wchar_t *s)
{
	if (!s || !*s) return false;

	//	Any string containing a digit immediately followed by '%'.
	for (const wchar_t *p = s; *p; ++p) {
		if (*p == L'%' && p > s && iswdigit(p[-1])) return true;
	}
	//	AE's magnification popup also offers 'Fit' and 'Fit up to 100%'.
	if (wcsstr(s, L"Fit") != NULL) return true;

	return false;
}

static bool
LooksLikeScroll(const wchar_t *cls)
{
	if (!cls) return false;
	if (_wcsicmp(cls, L"ScrollBar") == 0) return true;
	if (wcsstr(cls, L"Scroll") != NULL) return true;
	return false;
}

/* ------------------------------------------------------------------ */
/*  The walk                                                           */
/*                                                                     */
/*  EnumChildWindows already enumerates EVERY descendant, not just the  */
/*  immediate children. Recursing into it therefore re-walks the whole  */
/*  subtree at each level: the first version of this probe reported     */
/*  3219 windows for a tree of a few hundred, and blew the finding caps */
/*  with duplicates - which would have made an empty result meaningless */
/*  because it was also a TRUNCATED one. So enumerate once per          */
/*  top-level window and derive depth from the parent chain.            */
/* ------------------------------------------------------------------ */
static HWND g_currentTop = NULL;

static int
DepthOf(HWND hwnd)
{
	int d = 0;
	HWND p = hwnd;
	while (p && p != g_currentTop && d < 64) {
		p = GetParent(p);
		++d;
	}
	return d;
}

static BOOL CALLBACK
ChildProc(HWND hwnd, LPARAM lParam)
{
	(void)lParam;
	int depth = DepthOf(hwnd);

	if (g_count >= kMaxFound) return FALSE;

	Found &f = g_found[g_count];
	f.hwnd    = hwnd;
	f.depth   = depth;
	f.visible = IsWindowVisible(hwnd) ? true : false;

	f.cls[0] = 0;
	GetClassNameW(hwnd, f.cls, 128);

	//	GetWindowTextW does not cross process boundaries for non-standard
	//	controls, but we are asking about AE's own windows from outside AE, so
	//	use WM_GETTEXT with a timeout - a hung AE must not hang the probe.
	f.txt[0] = 0;
	DWORD_PTR res = 0;
	SendMessageTimeoutW(hwnd, WM_GETTEXT, 256, (LPARAM)f.txt,
	                    SMTO_ABORTIFHUNG, 200, &res);

	GetWindowRect(hwnd, &f.rc);

	int idx = g_count++;

	if (LooksLikeZoom(f.txt) && g_zoomCount < 512) {
		g_zoomLike[g_zoomCount++] = idx;
	}
	if (LooksLikeScroll(f.cls) && g_scrollCount < 512) {
		g_scrollLike[g_scrollCount++] = idx;
	}
	//	The interesting thing is anything that is NOT AE's generic panel class.
	//	Those are real OS controls, and real OS controls can be read.
	if (_wcsicmp(f.cls, L"DroverLord - Window Class") != 0 &&
	    g_nonDroverCount < 4096) {
		g_nonDrover[g_nonDroverCount++] = idx;
	}

	return TRUE;
}

static void
WalkChildren(HWND top)
{
	g_currentTop = top;
	EnumChildWindows(top, ChildProc, 0);
}

/* ------------------------------------------------------------------ */
/*  Finding AE's top-level windows                                     */
/* ------------------------------------------------------------------ */
static HWND g_topLevel[64];
static int  g_topCount = 0;

static BOOL CALLBACK
TopProc(HWND hwnd, LPARAM)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (pid == g_aePid && IsWindowVisible(hwnd) && g_topCount < 64) {
		g_topLevel[g_topCount++] = hwnd;
	}
	return TRUE;
}

/* ------------------------------------------------------------------ */

static DWORD
FindAEPid(void)
{
	//	Cheap and dependency-free: ask the window manager, not the process
	//	list, so we need no tlhelp32 and no elevation.
	DWORD pid = 0;
	HWND h = NULL;
	while ((h = FindWindowExW(NULL, h, NULL, NULL)) != NULL) {
		wchar_t cls[128] = {0};
		GetClassNameW(h, cls, 128);
		//	Version-stamped, so match the STEM only, never the whole string.
		if (wcsncmp(cls, L"AE_CApplication", 15) == 0) {
			GetWindowThreadProcessId(h, &pid);
			if (pid) return pid;
		}
	}
	return 0;
}

static void
PrintRow(const Found &f)
{
	int w = f.rc.right - f.rc.left;
	int h = f.rc.bottom - f.rc.top;

	wprintf(L"%*s[%p] %-32.32s  (%5d,%5d) %4dx%-4d %s  txt=\"%s\"\n",
	        f.depth * 2, L"",
	        (void *)f.hwnd,
	        f.cls,
	        (int)f.rc.left, (int)f.rc.top, w, h,
	        f.visible ? L"vis" : L"HID",
	        f.txt);
}

int
wmain(int argc, wchar_t **argv)
{
	(void)argc; (void)argv;

	//	AE is per-monitor DPI aware. If the probe is not, every rect we read
	//	comes back in virtualised coordinates and every later transform is
	//	silently wrong by the DPI scale factor. Load it dynamically so the
	//	binary still runs on an older Windows.
	{
		typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
		HMODULE u32 = GetModuleHandleW(L"user32.dll");
		SetCtxFn set = u32 ? (SetCtxFn)GetProcAddress(u32,
		                       "SetProcessDpiAwarenessContext") : NULL;
		//	-4 == DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
		if (!set || !set((HANDLE)(INT_PTR)-4)) {
			SetProcessDPIAware();
			wprintf(L"NOTE: fell back to SetProcessDPIAware (system aware).\n");
		}
	}

	//	A zero result below is the interesting outcome, so the matcher has to be
	//	shown working first - otherwise 'no zoom control found' and 'my matcher
	//	is broken' are the same output. Includes negatives, so a matcher that
	//	says yes to everything fails this too.
	{
		struct { const wchar_t *s; bool want; } cases[] = {
			{ L"100%",            true  },
			{ L"50%",             true  },
			{ L"66.7%",           true  },
			{ L"Fit",             true  },
			{ L"Fit up to 100%",  true  },
			{ L"",                false },
			{ L"Composition",     false },
			{ L"%",               false },	// bare, no digit
			{ L"Effect Controls", false },
		};
		int n = (int)(sizeof(cases) / sizeof(cases[0])), bad = 0;
		for (int i = 0; i < n; ++i) {
			if (LooksLikeZoom(cases[i].s) != cases[i].want) {
				wprintf(L"SELFTEST FAIL: \"%s\" expected %d\n",
				        cases[i].s, (int)cases[i].want);
				++bad;
			}
		}
		wprintf(L"matcher self-test: %d/%d\n", n - bad, n);
		if (bad) {
			wprintf(L"ABORT: the matcher is broken, so an empty result would\n");
			wprintf(L"       prove nothing. Fix it before reading findings.\n");
			return 2;
		}
	}

	g_aePid = FindAEPid();
	if (!g_aePid) {
		wprintf(L"FAIL: no window of class AE_CApplication* found.\n");
		wprintf(L"      Is %s running, and is this probe on the same desktop?\n",
		        kAEProcess);
		return 1;
	}
	wprintf(L"AE process id: %lu\n", (unsigned long)g_aePid);

	EnumWindows(TopProc, 0);
	wprintf(L"AE visible top-level windows: %d\n\n", g_topCount);

	wprintf(L"================ WINDOW TREE ================\n");
	for (int i = 0; i < g_topCount; ++i) {
		int start = g_count;

		Found &f = g_found[g_count];
		f.hwnd = g_topLevel[i];
		f.depth = 0;
		f.visible = true;
		f.cls[0] = 0; GetClassNameW(f.hwnd, f.cls, 128);
		f.txt[0] = 0;
		DWORD_PTR res = 0;
		SendMessageTimeoutW(f.hwnd, WM_GETTEXT, 256, (LPARAM)f.txt,
		                    SMTO_ABORTIFHUNG, 200, &res);
		GetWindowRect(f.hwnd, &f.rc);
		if (_wcsicmp(f.cls, L"DroverLord - Window Class") != 0 &&
		    g_nonDroverCount < 4096) {
			g_nonDrover[g_nonDroverCount++] = g_count;
		}
		g_count++;

		WalkChildren(g_topLevel[i]);

		wprintf(L"--- top-level #%d (%d windows) ---\n", i, g_count - start);
		for (int j = start; j < g_count; ++j) PrintRow(g_found[j]);
		wprintf(L"\n");
	}

	/* -------------------------------------------------------------- */
	/*  The part that actually decides the spike                       */
	/* -------------------------------------------------------------- */
	wprintf(L"================ FINDINGS ================\n");
	wprintf(L"total windows walked: %d\n\n", g_count);

	wprintf(L"[1] ZOOM-LIKE TEXT (a readable magnification would make the\n");
	wprintf(L"    analytic route viable): %d hit(s)\n", g_zoomCount);
	for (int i = 0; i < g_zoomCount; ++i) PrintRow(g_found[g_zoomLike[i]]);
	if (!g_zoomCount) {
		wprintf(L"    none. No window reports its text as a magnification.\n");
	}

	wprintf(L"\n[2] SCROLLBAR-LIKE CLASSES (a real scrollbar would give pan\n");
	wprintf(L"    offset via GetScrollInfo): %d hit(s)\n", g_scrollCount);
	for (int i = 0; i < g_scrollCount; ++i) PrintRow(g_found[g_scrollLike[i]]);
	if (!g_scrollCount) {
		wprintf(L"    none.\n");
	}

	wprintf(L"\n[3] NON-DroverLord CLASSES (everything AE did not draw itself;\n");
	wprintf(L"    these are the only windows the OS can describe): %d hit(s)\n",
	        g_nonDroverCount);
	for (int i = 0; i < g_nonDroverCount; ++i) PrintRow(g_found[g_nonDrover[i]]);
	if (!g_nonDroverCount) {
		wprintf(L"    none - AE draws its entire UI itself.\n");
	}

	wprintf(L"\n---- how to read this ----\n");
	wprintf(L"[1] non-empty  -> zoom is readable; analytic route is ALIVE.\n");
	wprintf(L"[1] empty and [3] empty\n");
	wprintf(L"               -> AE draws its own controls; nothing to read.\n");
	wprintf(L"                  Analytic route is DEAD; A1 falls to the\n");
	wprintf(L"                  empirical (screen-capture) route.\n");
	wprintf(L"[2] non-empty  -> pan offset may be recoverable too.\n");
	wprintf(L"\nThis probe proves ABSENCE only for the state AE is in right\n");
	wprintf(L"now. Run it with a comp open, the viewer focused, and at a\n");
	wprintf(L"magnification other than 100%% - a control that reads '100%%'\n");
	wprintf(L"when everything is 100%% has told you nothing.\n");

	return 0;
}
