/*
	osA3.cpp (Onion Skin) - Phase 0, spike A3, steps a and b.

	WHAT THIS DECIDES

	A1 made the comp->screen transform knowable AT REST: screen = s*comp + t,
	with s read exactly from views[i].options.zoom. A3 asks whether it is still
	knowable IN MOTION, because that is when an onion skin has to stay glued and
	that is when AE is least willing to talk to us.

	The concern is specific, not vague. pieFX S2 established that AE sits in a
	modal loop for the whole duration of a mouse press and does NOT pump AEGP
	idle time - so during exactly the drag where the transform changes fastest,
	an idle hook may never fire. S2 also found that a NULL-hwnd thread timer IS
	dispatched by those modal loops. This spike measures both, side by side,
	rather than carrying that finding over on trust: it was measured for a
	different purpose, in a different loop, on a different AE version.

	  A3a  TICK CADENCE. Timer ticks and idle ticks, timestamped, no AE calls at
	       all. Answers: does anything run during a drag, and how regularly?

	  A3b  ZOOM-READ LATENCY. Same ticks, but each one tries to read the viewer
	       zoom and times the attempt. Answers: can the transform be READ while
	       the user drags, and what does it cost?

	They are separate commands on purpose. Doing the read inside the cadence
	measurement would perturb the very thing being measured - if a read costs
	40ms, cadence collapses and we would not be able to tell which of the two
	facts we had discovered.

	WHAT COUNTS AS A PASS is not decided here. This logs; A3_analyse.py judges.

	Nothing is drawn and nothing in the project is modified. Two menu toggles,
	a timer, an idle hook, and a log file.
*/

#include "osA3.h"

#include <mmsystem.h>
#include <math.h>
#pragma comment(lib, "winmm.lib")

static AEGP_Command		S_a3a_cmd	= 0L;
static AEGP_Command		S_a3b_cmd	= 0L;
static AEGP_Command		S_a3c_cmd	= 0L;
static AEGP_Command		S_a3d_cmd	= 0L;
static AEGP_PluginID	S_my_id		= 0L;
static SPBasicSuite		*sP			= NULL;

//	Modelled on Persisto, not Commando: Commando never assigns its plugin id and
//	so registers every hook under id 0. Everything below needs a real one.

/* ------------------------------------------------------------------ */
/*  Sample buffer                                                      */
/* ------------------------------------------------------------------ */

enum SampleKind {
	kTimer = 0,
	kIdle  = 1
};

struct Sample {
	LONGLONG	qpc;			// when the tick fired
	LONGLONG	read_ticks;		// QPC ticks the zoom read took (A3b only, else 0)
	double		zoom;			// what it read, or -1
	int			kind;
	int			ok;				// did the read succeed
};

static Sample		*S_samples	= NULL;
static volatile LONG S_count	= 0;

static UINT_PTR		S_timer		= 0;
static bool			S_a3a_on	= false;
static bool			S_a3b_on	= false;
static bool			S_idle_registered = false;

static LONGLONG		S_qpc_freq	= 0;
static LONGLONG		S_started	= 0;

static char			S_log_path[AEGP_MAX_PATH_SIZE] = {0};

static LONGLONG
Now(void)
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return li.QuadPart;
}

/* ------------------------------------------------------------------ */
/*  Reading the zoom                                                   */
/*                                                                     */
/*  ExtendScript is the only route: A1b established that AEGP exposes   */
/*  no viewer geometry at all (AEGP_ItemViewSuite1 is one call and it   */
/*  returns playback time). So this is not a lazy choice - it is the    */
/*  only door, which is exactly why its cost during a drag matters.     */
/* ------------------------------------------------------------------ */

static const A_char *S_zoom_script =
	"(function(){"
	"  try {"
	"    var v = app.activeViewer;"
	"    if (!v) { return 'x'; }"
	"    return String(v.views[v.activeViewIndex].options.zoom);"
	"  } catch (e) { return 'x'; }"
	"})()";

//	Returns true and fills zoomP on success. Never reports an AE error to the
//	user: this runs up to 60 times a second and an alert per tick would make AE
//	unusable and destroy the measurement.
static bool
ReadZoom(double *zoomP)
{
	if (!sP) return false;

	AEGP_SuiteHandler	suites(sP);
	AEGP_MemHandle		resultH	= NULL;
	AEGP_MemHandle		errH	= NULL;
	A_Err				err		= A_Err_NONE;

	err = suites.UtilitySuite6()->AEGP_ExecuteScript(S_my_id, S_zoom_script,
	                                                 FALSE, &resultH, &errH);

	bool got = false;
	if (!err && resultH) {
		A_char *strP = NULL;
		suites.MemorySuite1()->AEGP_LockMemHandle(resultH, (void **)&strP);
		if (strP && strP[0] && strP[0] != 'x') {
			*zoomP = atof(strP);
			got = true;
		}
		suites.MemorySuite1()->AEGP_UnlockMemHandle(resultH);
	}

	//	AEGP_ExecuteScript returns a non-NULL but EMPTY error handle on success -
	//	testing the handle alone would report a failure on every call.
	if (resultH) suites.MemorySuite1()->AEGP_FreeMemHandle(resultH);
	if (errH)    suites.MemorySuite1()->AEGP_FreeMemHandle(errH);

	return got;
}

/* ------------------------------------------------------------------ */
/*  Recording                                                          */
/* ------------------------------------------------------------------ */

static void
Record(int kind)
{
	LONG i = InterlockedIncrement(&S_count) - 1;
	if (i < 0 || i >= OS_MAX_SAMPLES || !S_samples) return;

	Sample &s = S_samples[i];
	s.kind		 = kind;
	s.read_ticks = 0;
	s.zoom		 = -1.0;
	s.ok		 = 0;

	//	Timestamp BEFORE any AE call, so a slow read cannot be mistaken for a
	//	late tick. The two are different failures and the log has to tell them
	//	apart: a late tick means AE starved us, a slow read means AE answered
	//	but not in time.
	s.qpc = Now();

	if (S_a3b_on) {
		LONGLONG t0 = Now();
		double z = -1.0;
		bool ok = ReadZoom(&z);
		s.read_ticks = Now() - t0;
		s.zoom = z;
		s.ok = ok ? 1 : 0;
	}
}

static VOID CALLBACK
TickProc(HWND, UINT, UINT_PTR, DWORD)
{
	Record(kTimer);

	//	Self-limiting: a spike must not be able to leave a timer running.
	if (S_qpc_freq && (Now() - S_started) > (LONGLONG)OS_MAX_SECONDS * S_qpc_freq) {
		if (S_timer) { KillTimer(NULL, S_timer); S_timer = 0; }
	}
}

static A_Err
IdleHook(AEGP_GlobalRefcon, AEGP_IdleRefcon, A_long *max_sleepPL)
{
	if (S_a3a_on || S_a3b_on) Record(kIdle);
	if (max_sleepPL) *max_sleepPL = 1;		// ask to be called back promptly
	return A_Err_NONE;
}

/* ------------------------------------------------------------------ */

static void
WriteLog(AEGP_SuiteHandler &suites, const char *which)
{
	LONG n = S_count;
	if (n > OS_MAX_SAMPLES) n = OS_MAX_SAMPLES;

	FILE *f = fopen(S_log_path, "w");
	if (!f) {
		suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id,
			"A3: could not open the log for writing.");
		return;
	}

	fprintf(f, "# onion skin A3 tick log\n");
	fprintf(f, "# mode\t%s\n", which);
	fprintf(f, "# qpc_freq\t%lld\n", (long long)S_qpc_freq);
	fprintf(f, "# requested_tick_ms\t%d\n", OS_TICK_MS);
	fprintf(f, "# samples\t%ld\n", (long)n);
	fprintf(f, "kind\tms_since_start\tread_ms\tzoom\tok\n");

	for (LONG i = 0; i < n; ++i) {
		const Sample &s = S_samples[i];
		double ms   = S_qpc_freq ? (double)(s.qpc - S_started) * 1000.0 / (double)S_qpc_freq : 0.0;
		double rms  = S_qpc_freq ? (double)s.read_ticks       * 1000.0 / (double)S_qpc_freq : 0.0;
		fprintf(f, "%s\t%.4f\t%.4f\t%.10f\t%d\n",
		        s.kind == kTimer ? "timer" : "idle", ms, rms, s.zoom, s.ok);
	}
	fclose(f);

	A_char msg[AEGP_MAX_PATH_SIZE + 128];
	sprintf(msg, "A3 %s: %ld samples written to\n%s", which, (long)n, S_log_path);
	suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id, msg);
}

static bool
ResolveLogPath(AEGP_SuiteHandler &suites, const char *leaf)
{
	const char *tmp = getenv("TEMP");
	if (!tmp) tmp = getenv("TMP");
	if (!tmp) {
		suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id,
			"A3: could not resolve a temp path.");
		return false;
	}
	sprintf(S_log_path, "%s\\%s", tmp, leaf);
	return true;
}

static void
StartLogging(AEGP_SuiteHandler &suites, bool a3b)
{
	if (!S_samples) {
		S_samples = (Sample *)malloc(sizeof(Sample) * OS_MAX_SAMPLES);
		if (!S_samples) {
			suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id, "A3: out of memory.");
			return;
		}
	}

	if (!ResolveLogPath(suites, a3b ? "onionskin_A3b.txt" : "onionskin_A3a.txt"))
		return;

	LARGE_INTEGER li;
	QueryPerformanceFrequency(&li);
	S_qpc_freq = li.QuadPart;

	S_count		= 0;
	S_started	= Now();
	S_a3a_on	= !a3b;
	S_a3b_on	= a3b;

	//	NULL-hwnd thread timer. pieFX S2 found this is the one callback AE's
	//	modal drag loop still dispatches - a window timer or an idle hook is not
	//	enough. Re-measured here rather than assumed.
	S_timer = SetTimer(NULL, 0, OS_TICK_MS, TickProc);
	if (!S_timer) {
		A_char m[128];
		sprintf(m, "A3: SetTimer FAILED, GetLastError = %lu", GetLastError());
		suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id, m);
		S_a3a_on = S_a3b_on = false;
		return;
	}

	A_char msg[512];
	sprintf(msg,
		"A3%s LOGGING ON (%d ms ticks, stops itself after %d s).\n\n"
		"Now, in order, pausing ~2 s between each:\n"
		"  1. do NOTHING (baseline)\n"
		"  2. SCRUB the time indicator back and forth\n"
		"  3. PAN the comp (hold space, drag) - HOLD THE DRAG\n"
		"  4. ZOOM with the scroll wheel\n\n"
		"Then run the menu item again to write the log.",
		a3b ? "b" : "a", OS_TICK_MS, OS_MAX_SECONDS);
	suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id, msg);
}

static void
StopLogging(AEGP_SuiteHandler &suites, bool a3b)
{
	if (S_timer) { KillTimer(NULL, S_timer); S_timer = 0; }
	S_a3a_on = S_a3b_on = false;
	WriteLog(suites, a3b ? "A3b" : "A3a");
}

/* ================================================================== */
/*  A3c - the glued overlay                                            */
/*                                                                     */
/*  THE MODEL, and why it is this shape:                               */
/*                                                                     */
/*      s = zoom                     read per tick; A3b proved this is  */
/*                                   readable during a drag at 0.19ms   */
/*      t = panel_centre                                                */
/*          - s * comp_centre        ANALYTIC. Re-analysis of A1's 14   */
/*          + pan_offset             captures showed AE centres the     */
/*                                   comp to within 0.5px in x and 1px  */
/*                                   in y on every unpanned capture,    */
/*                                   while panned ones miss by up to    */
/*                                   234px. So t is not an unknown to   */
/*                                   be recovered - only the pan offset */
/*                                   is, and that is zero until the     */
/*                                   user pans.                         */
/*                                                                     */
/*  So the only thing A3c actually has to track is the pan offset, and  */
/*  the claim under test is that it moves 1:1 with the cursor during a   */
/*  pan drag. If that holds, the overlay stays glued; if it drifts, we   */
/*  will see it as the overlay walking away from the comp.              */
/*                                                                     */
/*  DELIBERATELY NOT SOLVED HERE:                                       */
/*   - PAR correction. It is a viewer toggle ExtendScript cannot read    */
/*     (A1 run 2). This assumes OFF, which is the default. If it is on,  */
/*     the overlay will be visibly wrong in x by the PAR factor - which  */
/*     is a useful demonstration of the unread state, not a bug to hide. */
/*   - Which window is the viewer. A2 is not started; the user points.   */
/* ================================================================== */

static HWND			S_overlay		= NULL;
static HWND			S_viewer		= NULL;
static UINT_PTR		S_c_timer		= 0;
static UINT_PTR		S_arm_timer		= 0;
static bool			S_a3c_on		= false;
static bool			S_period_raised	= false;

static int			S_comp_w		= 0;
static int			S_comp_h		= 0;

//	Pan offset, accumulated from cursor deltas while a pan gesture is active.
static double		S_pan_x			= 0.0;
static double		S_pan_y			= 0.0;
static bool			S_panning		= false;
static POINT		S_last_cursor	= {0, 0};

static HHOOK		S_mouse_hook	= NULL;

//	Cadence samples for A3c, so the overlay's real tick rate is measured rather
//	than asserted from the requested period.
static LONGLONG		S_c_last_tick	= 0;
static double		S_c_worst_gap	= 0.0;
static long			S_c_ticks		= 0;
static double		S_c_gap_sum		= 0.0;

static const A_char *S_comp_script =
	"(function(){"
	"  try {"
	"    var c = app.project.activeItem;"
	"    if (!(c instanceof CompItem)) { return 'x'; }"
	"    return c.width + ',' + c.height;"
	"  } catch (e) { return 'x'; }"
	"})()";

static bool
ReadCompSize(int *wP, int *hP)
{
	if (!sP) return false;
	AEGP_SuiteHandler	suites(sP);
	AEGP_MemHandle		resultH = NULL, errH = NULL;
	A_Err				err = A_Err_NONE;

	err = suites.UtilitySuite6()->AEGP_ExecuteScript(S_my_id, S_comp_script,
	                                                 FALSE, &resultH, &errH);
	bool got = false;
	if (!err && resultH) {
		A_char *strP = NULL;
		suites.MemorySuite1()->AEGP_LockMemHandle(resultH, (void **)&strP);
		if (strP && strP[0] && strP[0] != 'x') {
			int w = 0, h = 0;
			if (sscanf(strP, "%d,%d", &w, &h) == 2 && w > 0 && h > 0) {
				*wP = w; *hP = h; got = true;
			}
		}
		suites.MemorySuite1()->AEGP_UnlockMemHandle(resultH);
	}
	if (resultH) suites.MemorySuite1()->AEGP_FreeMemHandle(resultH);
	if (errH)    suites.MemorySuite1()->AEGP_FreeMemHandle(errH);
	return got;
}

/* ------------------------------------------------------------------ */
/*  Pan detection                                                      */
/*                                                                     */
/*  AE pans on a middle-button drag, and on space+left-drag. A                */
/*  thread-local WH_MOUSE hook sees both without touching another       */
/*  process and without any OS permission - the same mechanism pieFX    */
/*  S2 settled on, and for the same reason.                             */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK
MouseProc(int code, WPARAM wParam, LPARAM lParam)
{
	if (code >= 0 && S_a3c_on) {
		const MOUSEHOOKSTRUCT *mh = (const MOUSEHOOKSTRUCT *)lParam;

		bool space_down = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;

		if (wParam == WM_MBUTTONDOWN ||
		    (wParam == WM_LBUTTONDOWN && space_down)) {
			S_panning = true;
			if (mh) S_last_cursor = mh->pt;
		} else if (wParam == WM_MBUTTONUP || wParam == WM_LBUTTONUP) {
			S_panning = false;
		} else if (wParam == WM_MOUSEMOVE && S_panning && mh) {
			//	The claim under test: the picture tracks the cursor 1:1, so the
			//	delta IS the pan, not an estimate of it.
			S_pan_x += (double)(mh->pt.x - S_last_cursor.x);
			S_pan_y += (double)(mh->pt.y - S_last_cursor.y);
			S_last_cursor = mh->pt;
		}
	}
	return CallNextHookEx(S_mouse_hook, code, wParam, lParam);
}

/* ================================================================== */
/*  A3d - measuring t instead of inferring it                          */
/*                                                                     */
/*  WHY A3c FAILED, and why this is not just "add the missing case".    */
/*                                                                     */
/*  A3c tracked the pan by watching for pan GESTURES. The recording     */
/*  showed the box perfectly glued at rest (0.5px) and then completely  */
/*  static through a 110px pan, because the pan was a WHEEL SCROLL and  */
/*  the hook only watched middle-drag and space+drag. Adding            */
/*  WM_MOUSEWHEEL would have fixed that recording and left the approach */
/*  exactly as fragile: AE also pans via shift+wheel, the Hand tool,    */
/*  scrollbars, zoom-about-cursor, panel resize and Fit. A missed       */
/*  gesture produces a SILENTLY WRONG overlay - and for onion skin that */
/*  is indistinguishable from the thing the tool exists to show.        */
/*                                                                     */
/*  So stop asking HOW the comp moved and measure WHERE IT IS.          */
/*                                                                     */
/*  We already know s exactly (A3b) and therefore the comp's on-screen  */
/*  size. Blit one horizontal and one vertical 1px strip through the    */
/*  panel, find where the comp's edges cross them, and t falls straight */
/*  out. Cause-agnostic by construction.                                */
/*                                                                     */
/*  THE CONTROL: the detected span must equal s*comp_size within a few  */
/*  pixels, or the detection is REJECTED and the last good t is kept.   */
/*  Without that, a mis-detection becomes a wrong transform silently -  */
/*  which is the very failure this is replacing.                        */
/*                                                                     */
/*  READING PAST OUR OWN OVERLAY: the overlay is layered and topmost, so */
/*  a screen blit would sample the green box, not AE. Handled by never  */
/*  painting on the two sample lines - alpha stays 0 there, so the      */
/*  composited screen shows AE's own pixels through the gap.            */
/* ================================================================== */

static bool		S_strip_mode	= false;

static HDC		S_stripDC		= NULL;
static HBITMAP	S_stripH		= NULL;
static HBITMAP	S_stripV		= NULL;
static BYTE		*S_stripBitsH	= NULL;
static BYTE		*S_stripBitsV	= NULL;
static int		S_stripW		= 0;
static int		S_stripHt		= 0;

//	Live strip geometry, panel-local. PaintOverlay must skip these exact lines.
static int		S_strip_row		= -1;
static int		S_strip_col		= -1;

//	Last accepted comp origin, panel-local, and whether we have one at all.
static double	S_meas_tx		= 0.0;
static double	S_meas_ty		= 0.0;
static bool		S_have_meas		= false;

//	Detection stats, reported on stop.
static long		S_det_ok		= 0;
static long		S_det_rej[5]	= {0, 0, 0, 0, 0};
static double	S_det_cost_sum	= 0.0;
static double	S_det_cost_worst= 0.0;
static long		S_det_calls		= 0;

enum RejectWhy {
	kOK = 0, kNoBackground, kNoEdge, kWidthMismatch, kHeightMismatch
};

static void
FreeStrips(void)
{
	if (S_stripH)  { DeleteObject(S_stripH);  S_stripH  = NULL; S_stripBitsH = NULL; }
	if (S_stripV)  { DeleteObject(S_stripV);  S_stripV  = NULL; S_stripBitsV = NULL; }
	if (S_stripDC) { DeleteDC(S_stripDC);     S_stripDC = NULL; }
	S_stripW = S_stripHt = 0;
}

//	Cached, because allocating two DIB sections 60 times a second would make the
//	cost measurement about GDI allocation rather than about the sampling.
static bool
EnsureStrips(HDC screenDC, int pw, int ph)
{
	if (S_stripDC && S_stripW == pw && S_stripHt == ph) return true;
	FreeStrips();

	S_stripDC = CreateCompatibleDC(screenDC);
	if (!S_stripDC) return false;

	BITMAPINFO bi = {0};
	bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biPlanes      = 1;
	bi.bmiHeader.biBitCount    = 24;
	bi.bmiHeader.biCompression = BI_RGB;

	bi.bmiHeader.biWidth  = pw;
	bi.bmiHeader.biHeight = -1;
	S_stripH = CreateDIBSection(S_stripDC, &bi, DIB_RGB_COLORS,
	                            (void **)&S_stripBitsH, NULL, 0);

	bi.bmiHeader.biWidth  = 1;
	bi.bmiHeader.biHeight = -ph;
	S_stripV = CreateDIBSection(S_stripDC, &bi, DIB_RGB_COLORS,
	                            (void **)&S_stripBitsV, NULL, 0);

	if (!S_stripH || !S_stripV) { FreeStrips(); return false; }

	S_stripW  = pw;
	S_stripHt = ph;
	return true;
}

//	Distance between two 24-bit pixels, sum of absolute components.
static int
PixDiff(const BYTE *a, const BYTE *b)
{
	int d = 0;
	for (int i = 0; i < 3; ++i) {
		int v = (int)a[i] - (int)b[i];
		d += v < 0 ? -v : v;
	}
	return d;
}

//	Scan one line for the first and last pixel that differ from the background.
//	Returns false if the line has no usable background (the comp fills the
//	panel, so no edge is on screen) or no edge at all.
static bool
ScanLine(const BYTE *bits, int n, int stride_px, int *loP, int *hiP)
{
	if (n < 16) return false;

	//	Background sampled from both ends. If they disagree, the comp is running
	//	off at least one side and this line cannot be trusted.
	const BYTE *a = bits + (size_t)2 * stride_px * 3;
	const BYTE *b = bits + (size_t)(n - 3) * stride_px * 3;
	if (PixDiff(a, b) > OS_STRIP_BG_TOL) return false;

	int lo = -1, hi = -1;
	for (int i = 2; i < n - 2; ++i) {
		if (PixDiff(bits + (size_t)i * stride_px * 3, a) > OS_STRIP_BG_TOL) { lo = i; break; }
	}
	if (lo < 0) return false;
	for (int i = n - 3; i > lo; --i) {
		if (PixDiff(bits + (size_t)i * stride_px * 3, a) > OS_STRIP_BG_TOL) { hi = i; break; }
	}
	if (hi <= lo) return false;

	*loP = lo; *hiP = hi;
	return true;
}

static int
DetectCompOrigin(HDC screenDC, POINT origin, int pw, int ph,
                 double sx, double sy, double *txP, double *tyP)
{
	if (!EnsureStrips(screenDC, pw, ph)) return kNoBackground;

	S_strip_row = ph / 2 + OS_STRIP_ROW_OFF;
	S_strip_col = pw / 2 + OS_STRIP_COL_OFF;
	if (S_strip_row < 0 || S_strip_row >= ph) S_strip_row = ph / 2;
	if (S_strip_col < 0 || S_strip_col >= pw) S_strip_col = pw / 2;

	HGDIOBJ old = SelectObject(S_stripDC, S_stripH);
	BitBlt(S_stripDC, 0, 0, pw, 1, screenDC,
	       origin.x, origin.y + S_strip_row, SRCCOPY);
	SelectObject(S_stripDC, S_stripV);
	BitBlt(S_stripDC, 0, 0, 1, ph, screenDC,
	       origin.x + S_strip_col, origin.y, SRCCOPY);
	SelectObject(S_stripDC, old);

	int x0 = 0, x1 = 0, y0 = 0, y1 = 0;

	//	The horizontal strip's rows are DWORD-aligned but it is one row, so the
	//	pixels are simply consecutive. Same for the vertical strip: a 1px-wide
	//	DIB row pads to 4 bytes, so its stride in pixels is 4/3 of a pixel - use
	//	an explicit 4-byte row step instead of assuming packing.
	if (!ScanLine(S_stripBitsH, pw, 1, &x0, &x1)) return kNoEdge;

	//	Vertical strip: each row is 4 bytes (1 px padded), so step 4 bytes.
	{
		int lo = -1, hi = -1;
		const BYTE *bits = S_stripBitsV;
		const BYTE *a = bits + (size_t)2 * 4;
		const BYTE *b = bits + (size_t)(ph - 3) * 4;
		if (PixDiff(a, b) > OS_STRIP_BG_TOL) return kNoEdge;
		for (int i = 2; i < ph - 2; ++i)
			if (PixDiff(bits + (size_t)i * 4, a) > OS_STRIP_BG_TOL) { lo = i; break; }
		if (lo < 0) return kNoEdge;
		for (int i = ph - 3; i > lo; --i)
			if (PixDiff(bits + (size_t)i * 4, a) > OS_STRIP_BG_TOL) { hi = i; break; }
		if (hi <= lo) return kNoEdge;
		y0 = lo; y1 = hi;
	}

	//	THE CONTROL. A detected span that does not match s*comp_size means we
	//	found something other than the comp's edges - a panel divider, a layer
	//	outline, content that happens to match the background. Reject rather
	//	than believe it.
	double want_w = sx * S_comp_w;
	double want_h = sy * S_comp_h;
	double got_w  = (double)(x1 - x0 + 1);
	double got_h  = (double)(y1 - y0 + 1);

	if (fabs(got_w - want_w) > OS_STRIP_SIZE_TOL) return kWidthMismatch;
	if (fabs(got_h - want_h) > OS_STRIP_SIZE_TOL) return kHeightMismatch;

	*txP = (double)x0;
	*tyP = (double)y0;
	return kOK;
}

/* ------------------------------------------------------------------ */
/*  Re-entrancy and read throttling                                    */
/*                                                                     */
/*  THIS IS WHAT CRASHED AE on the first A3d run, during a scrub.       */
/*                                                                     */
/*  ReadZoom calls AEGP_ExecuteScript, and ExecuteScript PUMPS          */
/*  MESSAGES. A WM_TIMER dispatched inside that pump re-enters the      */
/*  paint, and the re-entrant call can reach EnsureStrips -> FreeStrips */
/*  -> DeleteObject/DeleteDC on the very handles the outer call is      */
/*  still using. Use-after-free on GDI objects.                         */
/*                                                                     */
/*  A3c survived the same re-entrancy because it held no shared GDI     */
/*  state across the script call; A3d does, so the latent bug became a  */
/*  crash. A scrub triggers it because a busy AE makes ExecuteScript    */
/*  slower, which widens the window for a tick to land inside it.       */
/*                                                                     */
/*  Two fixes, both needed:                                            */
/*   - a re-entrancy guard, so a nested tick returns immediately;       */
/*   - a throttle on the script call itself. A3c/A3d tick at 8ms with   */
/*     timeBeginPeriod(1), so up to 125 ExecuteScript calls a second.   */
/*     A3b proved 30Hz safe and cheap; 125Hz is four times the pressure */
/*     on AE for no extra fidelity, since the overlay can repaint from  */
/*     a cached zoom between reads.                                     */
/* ------------------------------------------------------------------ */

static volatile LONG	S_in_paint		= 0;
static double			S_last_zoom		= 0.0;
static LONGLONG			S_last_zoom_at	= 0;

//	Never ask AE for the zoom more often than this. 32ms ~= 30Hz, the rate A3b
//	measured at 100% success and 0.19ms.
#define OS_ZOOM_MAX_AGE_MS	32.0

static bool
ZoomCached(double *zP)
{
	LONGLONG now = Now();
	double age = (S_qpc_freq && S_last_zoom_at)
		? (double)(now - S_last_zoom_at) * 1000.0 / (double)S_qpc_freq
		: 1e9;

	if (S_last_zoom > 0.0 && age < OS_ZOOM_MAX_AGE_MS) {
		*zP = S_last_zoom;
		return true;
	}

	double v = 0.0;
	if (ReadZoom(&v) && v > 0.0) {
		S_last_zoom = v;
		S_last_zoom_at = now;
		*zP = v;
		return true;
	}

	//	A failed read is not a reason to stop drawing - hold the last good value.
	if (S_last_zoom > 0.0) { *zP = S_last_zoom; return true; }
	return false;
}

/* ------------------------------------------------------------------ */
/*  Painting                                                           */
/* ------------------------------------------------------------------ */

static void PaintOverlayBody(void);

static void
PaintOverlay(void)
{
	//	A nested tick does nothing rather than corrupting the outer one's state.
	if (InterlockedCompareExchange(&S_in_paint, 1, 0) != 0) return;
	PaintOverlayBody();
	InterlockedExchange(&S_in_paint, 0);
}

static void
PaintOverlayBody(void)
{
	if (!S_overlay || !S_viewer) return;

	RECT cr;
	GetClientRect(S_viewer, &cr);
	int pw = cr.right - cr.left, ph = cr.bottom - cr.top;
	if (pw <= 0 || ph <= 0) return;

	POINT origin = {0, 0};
	ClientToScreen(S_viewer, &origin);

	//	Follow the panel: it can be resized or moved while we are up, and an
	//	overlay that ignores that is not glued to anything.
	SetWindowPos(S_overlay, HWND_TOPMOST, origin.x, origin.y, pw, ph,
	             SWP_NOACTIVATE);

	double zoom = 0.0;
	if (!ZoomCached(&zoom) || zoom <= 0.0) return;

	double sx = zoom, sy = zoom;		// PAR correction assumed OFF - see header

	//	Analytic centring (A3c's model) is the starting point and the fallback.
	double tx = pw / 2.0 - sx * S_comp_w / 2.0 + S_pan_x;
	double ty = ph / 2.0 - sy * S_comp_h / 2.0 + S_pan_y;

	HDC screenDC0 = GetDC(NULL);

	if (S_strip_mode) {
		LONGLONG t0 = Now();
		double mx = 0.0, my = 0.0;
		int why = DetectCompOrigin(screenDC0, origin, pw, ph, sx, sy, &mx, &my);
		double cost = S_qpc_freq
			? (double)(Now() - t0) * 1000.0 / (double)S_qpc_freq : 0.0;

		++S_det_calls;
		S_det_cost_sum += cost;
		if (cost > S_det_cost_worst) S_det_cost_worst = cost;

		if (why == kOK) {
			S_meas_tx = mx; S_meas_ty = my; S_have_meas = true;
			++S_det_ok;
		} else {
			S_det_rej[why]++;
		}

		//	Hold the last accepted measurement when a frame is rejected. Falling
		//	back to the analytic value would make the box JUMP on every rejected
		//	frame, which reads as a worse failure than staying slightly stale.
		if (S_have_meas) { tx = S_meas_tx; ty = S_meas_ty; }
	}

	//	Premultiplied BGRA, top-down, as UpdateLayeredWindow requires.
	int stride = pw * 4;
	BYTE *bits = NULL;

	BITMAPINFO bi = {0};
	bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth       = pw;
	bi.bmiHeader.biHeight      = -ph;
	bi.bmiHeader.biPlanes      = 1;
	bi.bmiHeader.biBitCount    = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	HDC screenDC = screenDC0;
	HDC memDC    = CreateCompatibleDC(screenDC);
	HBITMAP dib  = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
	HGDIOBJ old  = SelectObject(memDC, dib);

	if (bits) {
		memset(bits, 0, (size_t)stride * ph);

		//	Outline of where the comp SHOULD be drawn. If the model is right this
		//	sits exactly on the edge of the picture at every zoom and pan.
		double x0 = tx, y0 = ty;
		double x1 = tx + sx * S_comp_w, y1 = ty + sy * S_comp_h;

		//	Opaque green, premultiplied (alpha 255 so RGB is unchanged).
		const BYTE B = 40, G = 255, R = 40, A = 255;

		#define OS_PLOT(px, py) do {                                   \
			int xx = (int)(px), yy = (int)(py);                        \
			if (xx >= 0 && xx < pw && yy >= 0 && yy < ph) {            \
				BYTE *p = bits + (size_t)yy * stride + (size_t)xx * 4; \
				p[0] = B; p[1] = G; p[2] = R; p[3] = A;                \
			} } while (0)

		for (int t = 0; t < 3; ++t) {			// 3px thick, easy to judge by eye
			for (double x = x0; x <= x1; x += 1.0) {
				OS_PLOT(x, y0 + t); OS_PLOT(x, y1 - t);
			}
			for (double y = y0; y <= y1; y += 1.0) {
				OS_PLOT(x0 + t, y); OS_PLOT(x1 - t, y);
			}
		}

		//	Crosshair at the comp centre - the single most legible drift cue,
		//	because it should sit on the magenta calibration marker exactly.
		double cx = tx + sx * S_comp_w / 2.0, cy = ty + sy * S_comp_h / 2.0;
		for (int d = -14; d <= 14; ++d) { OS_PLOT(cx + d, cy); OS_PLOT(cx, cy + d); }

		#undef OS_PLOT

		POINT  ptSrc = {0, 0};
		POINT  ptDst = {origin.x, origin.y};
		SIZE   size  = {pw, ph};
		BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

		UpdateLayeredWindow(S_overlay, screenDC, &ptDst, &size,
		                    memDC, &ptSrc, 0, &bf, ULW_ALPHA);
	}

	SelectObject(memDC, old);
	DeleteObject(dib);
	DeleteDC(memDC);
	ReleaseDC(NULL, screenDC);
}

static VOID CALLBACK
OverlayTickProc(HWND, UINT, UINT_PTR, DWORD)
{
	LONGLONG now = Now();
	if (S_c_last_tick && S_qpc_freq) {
		double gap = (double)(now - S_c_last_tick) * 1000.0 / (double)S_qpc_freq;
		if (gap > S_c_worst_gap) S_c_worst_gap = gap;
		S_c_gap_sum += gap;
		++S_c_ticks;
	}
	S_c_last_tick = now;

	PaintOverlay();

	if (S_qpc_freq && (now - S_started) > (LONGLONG)OS_C_MAX_SECONDS * S_qpc_freq) {
		if (S_c_timer) { KillTimer(NULL, S_c_timer); S_c_timer = 0; }
	}
}

static LRESULT CALLBACK
OverlayWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return DefWindowProc(h, m, w, l);
}

static void
StopA3c(AEGP_SuiteHandler &suites)
{
	//	Order matters. Clear the flags and kill the tick FIRST, so nothing can
	//	start another paint while we tear down.
	S_a3c_on = false;
	S_strip_mode = false;

	if (S_c_timer)   { KillTimer(NULL, S_c_timer);   S_c_timer = 0; }
	if (S_arm_timer) { KillTimer(NULL, S_arm_timer); S_arm_timer = 0; }
	if (S_mouse_hook) { UnhookWindowsHookEx(S_mouse_hook); S_mouse_hook = NULL; }
	if (S_period_raised) { timeEndPeriod(1); S_period_raised = false; }

	//	This command can be dispatched from INSIDE a paint: ExecuteScript pumps
	//	messages, and a menu click sitting in the queue gets delivered there. In
	//	that case the paint is still on the stack holding the overlay window and
	//	the strip DCs, so destroying them here is the same use-after-free that
	//	crashed AE. Leave them; the next Start reclaims them, and by then no
	//	paint can be in flight because the timer is already dead.
	if (!S_in_paint) {
		if (S_overlay) { DestroyWindow(S_overlay); S_overlay = NULL; }
		FreeStrips();
		S_strip_row = S_strip_col = -1;
	}

	S_viewer = NULL;

	A_char msg[1024];
	A_char detmsg[512];
	detmsg[0] = 0;
	if (S_det_calls) {
		sprintf(detmsg,
			"\n\nSTRIP DETECTION\n"
			"  attempts      %ld\n"
			"  accepted      %ld  (%.1f%%)\n"
			"  rejected      no-bg %ld, no-edge %ld, width %ld, height %ld\n"
			"  cost mean     %.3f ms\n"
			"  cost worst    %.3f ms",
			S_det_calls, S_det_ok,
			100.0 * (double)S_det_ok / (double)S_det_calls,
			S_det_rej[kNoBackground], S_det_rej[kNoEdge],
			S_det_rej[kWidthMismatch], S_det_rej[kHeightMismatch],
			S_det_cost_sum / (double)S_det_calls, S_det_cost_worst);
	}
	double mean = S_c_ticks ? (S_c_gap_sum / S_c_ticks) : 0.0;
	sprintf(msg,
		"A3c OFF.\n\n"
		"overlay ticks   %ld\n"
		"mean gap        %.2f ms  (requested %d ms)\n"
		"WORST gap       %.2f ms\n"
		"pan offset      %.0f, %.0f px\n\n"
		"A3a measured ~30ms without timeBeginPeriod(1). If the mean above is\n"
		"near %d ms, the 33Hz ceiling was the Windows timer, not AE.%s",
		S_c_ticks, mean, OS_C_TICK_MS, S_c_worst_gap, S_pan_x, S_pan_y,
		OS_C_TICK_MS, detmsg);
	suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id, msg);
}

static VOID CALLBACK
ArmProc(HWND, UINT, UINT_PTR, DWORD)
{
	if (S_arm_timer) { KillTimer(NULL, S_arm_timer); S_arm_timer = 0; }

	AEGP_SuiteHandler suites(sP);

	POINT pt;
	GetCursorPos(&pt);
	S_viewer = WindowFromPoint(pt);
	if (!S_viewer) {
		suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id,
			"A3c: no window under the cursor. Nothing started.");
		S_a3c_on = false;
		return;
	}

	if (!ReadCompSize(&S_comp_w, &S_comp_h)) {
		suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id,
			"A3c: could not read the comp size. Open a comp and retry.");
		S_a3c_on = false;
		return;
	}

	RECT cr; GetClientRect(S_viewer, &cr);
	POINT origin = {0, 0}; ClientToScreen(S_viewer, &origin);

	static bool registered = false;
	if (!registered) {
		WNDCLASSEXW wc = {0};
		wc.cbSize        = sizeof(wc);
		wc.lpfnWndProc   = OverlayWndProc;
		wc.hInstance     = GetModuleHandleW(NULL);
		wc.lpszClassName = L"OnionSkinA3cOverlay";
		RegisterClassExW(&wc);
		registered = true;
	}

	//	WS_EX_TRANSPARENT so clicks pass through to AE - an overlay that ate the
	//	viewer's input would fail as a product no matter how well it tracked.
	//	WS_EX_NOACTIVATE so latching on never steals focus.
	S_overlay = CreateWindowExW(
		WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
		WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
		L"OnionSkinA3cOverlay", L"", WS_POPUP,
		origin.x, origin.y, cr.right - cr.left, cr.bottom - cr.top,
		NULL, NULL, GetModuleHandleW(NULL), NULL);

	if (!S_overlay) {
		A_char m[128];
		sprintf(m, "A3c: CreateWindowEx failed, GetLastError = %lu", GetLastError());
		suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id, m);
		S_a3c_on = false;
		return;
	}
	ShowWindow(S_overlay, SW_SHOWNOACTIVATE);

	S_mouse_hook = SetWindowsHookEx(WH_MOUSE, MouseProc, NULL, GetCurrentThreadId());

	//	Raise the system timer resolution BEFORE starting the tick, so the whole
	//	run is measured under the same conditions.
	if (timeBeginPeriod(1) == TIMERR_NOERROR) S_period_raised = true;

	S_pan_x = S_pan_y = 0.0;
	S_panning = false;
	S_c_worst_gap = 0.0; S_c_ticks = 0; S_c_gap_sum = 0.0; S_c_last_tick = 0;
	S_started = Now();

	S_c_timer = SetTimer(NULL, 0, OS_C_TICK_MS, OverlayTickProc);
	if (!S_c_timer) {
		suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id, "A3c: SetTimer failed.");
		StopA3c(suites);
	}
}

static void
StartA3c(AEGP_SuiteHandler &suites, bool strips)
{
	S_strip_mode = strips;
	S_have_meas = false;
	S_det_ok = 0; S_det_calls = 0;
	S_det_cost_sum = 0.0; S_det_cost_worst = 0.0;
	for (int i = 0; i < 5; ++i) S_det_rej[i] = 0;

	//	Reclaim anything a previous Stop had to leave behind because it was
	//	dispatched from inside a paint. Safe here: no timer is running.
	S_in_paint = 0;
	if (S_overlay) { DestroyWindow(S_overlay); S_overlay = NULL; }
	FreeStrips();
	S_strip_row = S_strip_col = -1;

	S_last_zoom = 0.0;
	S_last_zoom_at = 0;

	LARGE_INTEGER li;
	QueryPerformanceFrequency(&li);
	S_qpc_freq = li.QuadPart;

	S_a3c_on = true;

	A_char msg[640];
	sprintf(msg,
		"A3c ARMING.\n\n"
		"Put the cursor over the COMP VIEWER IMAGE AREA and leave it there\n"
		"for %d seconds. A green rectangle should appear exactly on the edge\n"
		"of the comp, with a crosshair at its centre.\n\n"
		"Then judge it BY EYE while you:\n"
		"  1. scrub the time indicator\n"
		"  2. pan (middle-drag, or space+drag) - does the box follow?\n"
		"  3. zoom\n"
		"  4. resize the panel\n\n"
		"Record the screen if you can. Run the menu item again to stop.",
		OS_C_ARM_SECONDS);
	suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id, msg);

	S_arm_timer = SetTimer(NULL, 0, OS_C_ARM_SECONDS * 1000, ArmProc);
}

/* ------------------------------------------------------------------ */
/*  Hooks                                                              */
/* ------------------------------------------------------------------ */

static A_Err
CommandHook(
	AEGP_GlobalRefcon,
	AEGP_CommandRefcon,
	AEGP_Command		command,
	AEGP_HookPriority,
	A_Boolean			already_handledB,
	A_Boolean			*handledPB)
{
	if (already_handledB) return A_Err_NONE;

	AEGP_SuiteHandler suites(sP);

	if (command == S_a3a_cmd) {
		if (S_a3a_on || S_a3b_on) StopLogging(suites, S_a3b_on);
		else                      StartLogging(suites, false);
		*handledPB = TRUE;
	} else if (command == S_a3b_cmd) {
		if (S_a3a_on || S_a3b_on) StopLogging(suites, S_a3b_on);
		else                      StartLogging(suites, true);
		*handledPB = TRUE;
	} else if (command == S_a3c_cmd) {
		if (S_a3c_on) StopA3c(suites);
		else          StartA3c(suites, false);
		*handledPB = TRUE;
	} else if (command == S_a3d_cmd) {
		if (S_a3c_on) StopA3c(suites);
		else          StartA3c(suites, true);
		*handledPB = TRUE;
	}
	return A_Err_NONE;
}

static A_Err
UpdateMenuHook(AEGP_GlobalRefcon, AEGP_UpdateMenuRefcon, AEGP_WindowType)
{
	AEGP_SuiteHandler suites(sP);
	suites.CommandSuite1()->AEGP_EnableCommand(S_a3a_cmd);
	suites.CommandSuite1()->AEGP_EnableCommand(S_a3b_cmd);
	suites.CommandSuite1()->AEGP_EnableCommand(S_a3c_cmd);
	suites.CommandSuite1()->AEGP_EnableCommand(S_a3d_cmd);
	return A_Err_NONE;
}

/* ------------------------------------------------------------------ */

A_Err
EntryPointFunc(
	struct SPBasicSuite		*pica_basicP,
	A_long					major_versionL,
	A_long					minor_versionL,
	AEGP_PluginID			aegp_plugin_id,
	AEGP_GlobalRefcon		*global_refconP)
{
	A_Err err = A_Err_NONE, err2 = A_Err_NONE;

	sP		= pica_basicP;
	S_my_id	= aegp_plugin_id;		// Commando forgets this; everything needs it

	AEGP_SuiteHandler suites(pica_basicP);

	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_a3a_cmd));
	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_a3a_cmd,
		OS_A3A_MENU_NAME, AEGP_Menu_WINDOW, AEGP_MENU_INSERT_SORTED));

	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_a3b_cmd));
	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_a3b_cmd,
		OS_A3B_MENU_NAME, AEGP_Menu_WINDOW, AEGP_MENU_INSERT_SORTED));

	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_a3c_cmd));
	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_a3c_cmd,
		OS_A3C_MENU_NAME, AEGP_Menu_WINDOW, AEGP_MENU_INSERT_SORTED));

	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_a3d_cmd));
	ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(S_a3d_cmd,
		OS_A3D_MENU_NAME, AEGP_Menu_WINDOW, AEGP_MENU_INSERT_SORTED));

	ERR(suites.RegisterSuite5()->AEGP_RegisterCommandHook(S_my_id,
		AEGP_HP_BeforeAE, AEGP_Command_ALL, CommandHook, NULL));

	ERR(suites.RegisterSuite5()->AEGP_RegisterUpdateMenuHook(S_my_id,
		UpdateMenuHook, NULL));

	//	The idle hook is half the A3a measurement, not a convenience: the whole
	//	question is whether it goes quiet during a drag while the timer does not.
	ERR(suites.RegisterSuite5()->AEGP_RegisterIdleHook(S_my_id, IdleHook, NULL));
	if (!err) S_idle_registered = true;

	if (err) {
		ERR2(suites.UtilitySuite3()->AEGP_ReportInfo(S_my_id,
			"Onion Skin A3: failed to register."));
	}
	return err;
}
