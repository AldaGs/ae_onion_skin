/*
	A3e_SlipProbe.cpp (Onion Skin) - Phase 0, spikes A3e and A3f, out of process.

	A3e ASKED: is one frame of GDI capture latency good enough for a glued
	overlay, or is Windows.Graphics.Capture required? Run 1 answered it, and the
	answer was that latency was the wrong thing to worry about. Capture was
	healthy (worst 25.3ms, no stall) and the typical slip sat ON the quantisation
	floor - half a capture interval, which WGC is paced by too and would not buy
	back. What busted the budget was the tail, and the tail was not lag.

	A3f IS WHAT RUN 1 FOUND. For 12.6% of a 60s session the detector rejected
	every capture and the overlay FROZE while the picture moved - the failure the
	user reported as "it doesn't work when the comp bounds are out of the view".
	Two causes, both measured off the screen recording:

	  1. the comp drawn LARGER THAN THE PANEL (here at s > 0.675 vertically), so
	     a scan line found background at neither end;
	  2. the single fixed sample line MISSING A SMALL COMP at low zoom - at 21.9%
	     the comp was 420x236 and the sample column fell just outside it.

	This file now implements A3f stages 1 and 2.

	STAGE 1a - PER-AXIS ACCEPTANCE. Run 1 rejected the WHOLE frame if either axis
	failed. But at 69% zoom the comp's left and right edges were at x=493 and
	x=1818, both comfortably inside the panel: the horizontal axis was perfectly
	measurable and was thrown away because the vertical one was not. That single
	policy accounted for the longest blind run in the session. tx and ty are now
	accepted, held and aged INDEPENDENTLY.

	STAGE 1b - MANY SAMPLE LINES, NOT ONE. Cause 2 is simply that one line can
	miss. Several spread across the panel cannot all miss a comp big enough to
	matter, and the pixels are already captured - the scan is O(w+h) per line, so
	this costs nothing measurable.

	AND THE CONTROL THAT HAD TO BE REPLACED. Accepting axes independently
	DESTROYS the control A3d and A3e relied on: that both axes must imply the
	same zoom. Dropping a control to make a fix fit is how a detector starts
	lying, so it is replaced, not removed:

	  - WITHIN an axis, the sample lines check each other. The comp is a
	    rectangle, so every line that crosses it must return the SAME pair of
	    edges. At least OS_MIN_AGREE lines must agree within OS_AGREE_TOL or the
	    axis is refused. A line that locked onto a layer outline or a stray solid
	    disagrees with its neighbours and is outvoted.
	  - ACROSS axes, the old zoom check is KEPT and still applied whenever both
	    axes are available - which is most of the time. It is now an additional
	    check on top of the within-axis vote, not the only one.

	So the detector is strictly better guarded than in run 1, not worse: it has a
	control when both axes are present AND a control when only one is.

	STAGE 2 - BLIND MUST BE VISIBLE AS BLIND. Run 1 held the last good t and kept
	drawing a confident box. That is precisely the roadmap's founding failure - an
	onion skin that lies about where the previous drawing was is worse than none.
	Now: an axis with no fresh fix for OS_STALE_MS is drawn DASHED AND AMBER, and
	if nothing has been measured for OS_HIDE_MS the overlay hides itself
	completely. Holding a stale value silently is not an option any more.

	WHY IT IS OUT OF PROCESS, AND WHY IT NEEDS NO AE AT ALL. A3d proved the comp
	rectangle can be found from a captured panel, and that its span on an axis
	RECOVERS THE ZOOM. So both halves of the transform come out of the capture
	itself - no AEGP, no ExtendScript, no re-entrancy, and no way to take AE down,
	which two in-process A3d runs already did. This is also the SHIPPING
	CONFIGURATION: one full-panel screen blit on a DEDICATED THREAD, an overlay
	repainting from whatever that thread last published. The 16.7ms is a SYNC
	WAIT, not CPU burn, so the capture loop never blocks the paint. Do not
	"simplify" that away.

	WHAT IS ON SCREEN
	  green    the comp rectangle from the newest capture - the shipping config
	  amber    dashed: that axis is STALE, the value is not currently trusted
	  magenta  the same rectangle from a capture deliberately held OS_STALE_MS old
	  nothing  fully blind - and shown as nothing, which is the honest answer

	Magenta is the BROKEN CONTROL, and it is a control of the MEASUREMENT rather
	than of the detector: if the analysis cannot separate a 100ms-stale overlay
	from a live one, it is not resolving lag at all and may not pronounce on it.

	THE TWO MEASUREMENTS, AND WHY NEITHER ALONE IS ENOUGH

	  A3e_paints.txt   what each paint drew and which capture it drew from. The
	                   capture stream is the reference and is ITSELF one sync
	                   late, so this measures the pipeline only. A LOWER BOUND,
	                   and labelled as one.
	  a screen recording   overlay against comp, same instant, whole chain. THE
	                   VERDICT IS READ FROM THIS. (A3e_video.py.)

	Build (from this directory, any Developer prompt):
	    cl /nologo /EHsc /DUNICODE /D_UNICODE A3e_SlipProbe.cpp /link user32.lib gdi32.lib winmm.lib /OUT:os_A3e.exe

	Usage:
	    os_A3e.exe [comp_w] [comp_h] [seconds]      default 1920 1080 60

	Hover over the comp viewer image area for the countdown, then USE AE. Record
	the screen. The probe only reads pixels; it sends no input and never touches
	AE's process.
*/

#include <windows.h>
#include <stdio.h>
#include <math.h>

#define BG_TOL			30		// sum |dRGB| to count as "not the background"
#define AXIS_TOL		0.01	// 1% agreement required between the two axes
#define OS_STALE_MS		100.0	// the broken control's deliberate staleness
#define OS_TICK_MS		16		// paint cadence - one frame at 60Hz
#define OS_RING			256		// capture history; ~4s at 60Hz
#define OS_MAX_PAINTS	20000

//	A3f stage 1b. Five lines per axis, spread across the panel. Odd fractions so
//	they cannot share a symmetry with the comp edges at any round zoom, and none
//	at the exact centre where the crosshair is drawn.
#define OS_SCAN_LINES	5
static const double kScanFrac[OS_SCAN_LINES] = {0.17, 0.33, 0.55, 0.71, 0.89};

//	How close two lines' edge pairs must be to count as agreeing, and how many
//	must agree before an axis is believed. Two is the minimum that can disagree
//	at all; one line voting alone is an assertion, not a measurement.
#define OS_AGREE_TOL	2.0
#define OS_MIN_AGREE	2

//	A3f stage 2. Beyond this an axis is drawn as untrusted; beyond the second,
//	the overlay hides rather than show a stale rectangle.
#define OS_STALE_LIMIT_MS	250.0
#define OS_HIDE_MS			500.0

/* ------------------------------------------------------------------ */
/*  Shared state: one capture thread publishes, the paint reads.       */
/* ------------------------------------------------------------------ */

//	Per-axis validity, because A3f accepts the axes independently.
typedef struct {
	double	t_ms;			// when this capture was TAKEN (probe clock)
	double	sx, tx;			// horizontal: zoom and comp left
	double	sy, ty;			// vertical:   zoom and comp top
	int		okx, oky;
} Sample;

static CRITICAL_SECTION	g_lock;
static Sample			g_ring[OS_RING];
static long				g_head		= 0;	// next slot to write
static long				g_filled	= 0;

static volatile LONG	g_stop		= 0;
static HWND				g_viewer	= NULL;
static HWND				g_overlay	= NULL;
static int				g_comp_w	= 1920, g_comp_h = 1080;

static LARGE_INTEGER	g_freq, g_t0;

//	Capture-thread tallies, read only after the join.
static long		g_cap_total = 0, g_cap_bothx = 0, g_cap_okx = 0, g_cap_oky = 0;
static long		g_cap_blind = 0, g_cap_axis = 0, g_cap_onlyx = 0, g_cap_onlyy = 0;
static double	g_cap_cost_sum = 0.0, g_cap_cost_worst = 0.0;

//	The sample lines, published so the paint can leave its alpha gaps on exactly
//	the lines the detector reads.
static CRITICAL_SECTION	g_line_lock;
static int		g_rows[OS_SCAN_LINES], g_cols[OS_SCAN_LINES];
static int		g_nlines = 0;

static double
NowMs(void)
{
	LARGE_INTEGER n; QueryPerformanceCounter(&n);
	return (double)(n.QuadPart - g_t0.QuadPart) * 1000.0 / (double)g_freq.QuadPart;
}

/* ------------------------------------------------------------------ */
/*  Detection                                                          */
/* ------------------------------------------------------------------ */

static HDC		g_capDC		= NULL;
static HBITMAP	g_capBmp	= NULL;
static BYTE		*g_capBits	= NULL;
static int		g_capStride	= 0, g_capW = 0, g_capH = 0;

static void
FreeCapture(void)
{
	if (g_capBmp) { DeleteObject(g_capBmp); g_capBmp = NULL; g_capBits = NULL; }
	if (g_capDC)  { DeleteDC(g_capDC);      g_capDC  = NULL; }
	g_capW = g_capH = 0;
}

//	Allocated once per panel size, NEVER per frame. A3d's in-process run was
//	allocating and blitting a 3.6MB DIB up to 125 times a second on AE's UI
//	thread - ~450MB/s of GDI work - and that is a plausible half of why AE froze.
static bool
EnsureCapture(HDC screenDC, int pw, int ph)
{
	if (g_capDC && g_capW == pw && g_capH == ph) return true;
	FreeCapture();

	g_capDC = CreateCompatibleDC(screenDC);
	if (!g_capDC) return false;

	BITMAPINFO bi = {0};
	bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth       = pw;
	bi.bmiHeader.biHeight      = -ph;			// top-down
	bi.bmiHeader.biPlanes      = 1;
	bi.bmiHeader.biBitCount    = 24;
	bi.bmiHeader.biCompression = BI_RGB;

	g_capBmp = CreateDIBSection(g_capDC, &bi, DIB_RGB_COLORS, (void **)&g_capBits, NULL, 0);
	if (!g_capBmp) { FreeCapture(); return false; }

	g_capStride = ((pw * 3) + 3) & ~3;
	g_capW = pw; g_capH = ph;
	return true;
}

static int
PixDiff(const BYTE *a, const BYTE *b)
{
	int d = 0;
	for (int i = 0; i < 3; ++i) { int v = (int)a[i] - (int)b[i]; d += v < 0 ? -v : v; }
	return d;
}

//	Why one line failed. Kept split rather than lumped as "no_edge", because run
//	1's blind time could not be attributed without knowing which test rejected
//	it - the geometry alone cleared every suspect.
enum {
	kScanOK = 0,
	kScanTooShort,
	kScanEndsDiffer,	//	the comp runs off that side of the line
	kScanNoTransition,	//	the line never crossed the comp at all
	kScanDegenerate
};

//	First and last pixel on the line differing from the line's own background.
//	step is the byte distance between consecutive pixels: 3 along a row, the
//	stride down a column.
static int
Scan(const BYTE *bits, int n, int step, int *loP, int *hiP)
{
	if (n < 16) return kScanTooShort;

	const BYTE *a = bits + (size_t)2 * step;
	const BYTE *b = bits + (size_t)(n - 3) * step;

	if (PixDiff(a, b) > BG_TOL) return kScanEndsDiffer;

	int lo = -1, hi = -1;
	for (int i = 2; i < n - 2; ++i)
		if (PixDiff(bits + (size_t)i * step, a) > BG_TOL) { lo = i; break; }
	if (lo < 0) return kScanNoTransition;
	for (int i = n - 3; i > lo; --i)
		if (PixDiff(bits + (size_t)i * step, a) > BG_TOL) { hi = i; break; }
	if (hi <= lo) return kScanDegenerate;

	*loP = lo; *hiP = hi;
	return kScanOK;
}

//	A3f stage 1: scan every sample line on one axis and let them vote.
//
//	THE WITHIN-AXIS CONTROL. The comp is a rectangle, so every line that crosses
//	it must return the SAME two edges. Lines are clustered on their (lo, hi) pair
//	and the largest cluster wins; fewer than OS_MIN_AGREE agreeing means the axis
//	is refused, not guessed. This is what replaces the cross-axis zoom check when
//	only one axis is available - a line that locked onto a layer outline or a
//	stray solid disagrees with its neighbours and is outvoted.
//
//	Returns the number of lines that agreed (0 = axis refused), and reports the
//	most common failure reason so blind time stays attributable.
static int
DetectAxis(const BYTE *base, int nlines, const int *at,
           int n, int step, int line_step,
           double *loP, double *hiP, int *whyP)
{
	int lo[OS_SCAN_LINES], hi[OS_SCAN_LINES], got = 0;
	int why_count[5] = {0, 0, 0, 0, 0};

	for (int i = 0; i < nlines; ++i) {
		int l = 0, h = 0;
		int why = Scan(base + (size_t)at[i] * line_step, n, step, &l, &h);
		++why_count[why];
		if (why == kScanOK) { lo[got] = l; hi[got] = h; ++got; }
	}

	//	Report the commonest reason among the lines that failed, so a blind
	//	frame says WHY rather than merely that it happened.
	int why_best = kScanNoTransition, best_n = -1;
	for (int w = 1; w < 5; ++w)
		if (why_count[w] > best_n) { best_n = why_count[w]; why_best = w; }
	*whyP = why_best;

	if (got < OS_MIN_AGREE) return 0;

	//	Largest cluster of agreeing lines.
	int best_i = -1, best_c = 0;
	for (int i = 0; i < got; ++i) {
		int c = 0;
		for (int j = 0; j < got; ++j)
			if (fabs((double)lo[j] - lo[i]) <= OS_AGREE_TOL &&
			    fabs((double)hi[j] - hi[i]) <= OS_AGREE_TOL) ++c;
		if (c > best_c) { best_c = c; best_i = i; }
	}
	if (best_c < OS_MIN_AGREE) return 0;

	//	Median of the winning cluster, so one ragged line cannot pull the answer.
	double sl = 0.0, sh = 0.0;
	int c = 0;
	for (int j = 0; j < got; ++j)
		if (fabs((double)lo[j] - lo[best_i]) <= OS_AGREE_TOL &&
		    fabs((double)hi[j] - hi[best_i]) <= OS_AGREE_TOL) {
			sl += lo[j]; sh += hi[j]; ++c;
		}
	*loP = sl / c;
	*hiP = sh / c;
	*whyP = kScanOK;
	return best_c;
}

/* ------------------------------------------------------------------ */
/*  The capture thread.                                               */
/* ------------------------------------------------------------------ */

static const char *
WhyName(int w)
{
	static const char *n[] = {"ok", "short", "ends_differ", "no_transition",
	                          "degenerate"};
	return n[w];
}

static DWORD WINAPI
CaptureThread(LPVOID)
{
	HDC screenDC = GetDC(NULL);
	FILE *clog = _wfopen(L"A3e_captures.txt", L"w");
	if (clog) {
		fprintf(clog, "# onion skin A3e/A3f capture stream (dedicated thread)\n");
		fprintf(clog, "# comp\t%d\t%d\n", g_comp_w, g_comp_h);
		fprintf(clog, "t_ms\tpw\tph\tsx\tsy\ttx\tty\taxis_diff_pct\tcost_ms\tstatus\tnx\tny\twhy_x\twhy_y\n");
	}

	while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
		RECT cr; GetClientRect(g_viewer, &cr);
		int pw = cr.right - cr.left, ph = cr.bottom - cr.top;
		POINT origin = {0, 0}; ClientToScreen(g_viewer, &origin);
		if (pw <= 0 || ph <= 0) { Sleep(16); continue; }

		int rows[OS_SCAN_LINES], cols[OS_SCAN_LINES];
		for (int i = 0; i < OS_SCAN_LINES; ++i) {
			rows[i] = (int)(kScanFrac[i] * ph);
			cols[i] = (int)(kScanFrac[i] * pw);
			if (rows[i] < 1) rows[i] = 1;
			if (rows[i] > ph - 2) rows[i] = ph - 2;
			if (cols[i] < 1) cols[i] = 1;
			if (cols[i] > pw - 2) cols[i] = pw - 2;
		}
		EnterCriticalSection(&g_line_lock);
		memcpy(g_rows, rows, sizeof(rows));
		memcpy(g_cols, cols, sizeof(cols));
		g_nlines = OS_SCAN_LINES;
		LeaveCriticalSection(&g_line_lock);

		double c0 = NowMs();

		double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
		int nx = 0, ny = 0, why_x = kScanNoTransition, why_y = kScanNoTransition;

		if (EnsureCapture(screenDC, pw, ph)) {
			HGDIOBJ old = SelectObject(g_capDC, g_capBmp);
			//	ONE full-panel blit from the screen DC. A3d2: this costs the
			//	same as a 1px strip - the price is one composition sync and the
			//	pixels are free. Which is exactly why sampling five lines per
			//	axis instead of one is free too.
			BitBlt(g_capDC, 0, 0, pw, ph, screenDC, origin.x, origin.y, SRCCOPY);
			SelectObject(g_capDC, old);

			nx = DetectAxis(g_capBits, OS_SCAN_LINES, rows, pw, 3, g_capStride,
			                &x0, &x1, &why_x);
			ny = DetectAxis(g_capBits, OS_SCAN_LINES, cols, ph, g_capStride, 3,
			                &y0, &y1, &why_y);
		}

		double c1 = NowMs();
		double cost = c1 - c0;
		//	Timestamp at the MIDPOINT of the blit. The pixels are a snapshot
		//	somewhere inside that window; stamping at the end would build a
		//	systematic half-frame error into the number being measured.
		double t_taken = (c0 + c1) * 0.5;

		double sx = 0, sy = 0, diff = 0;
		int okx = 0, oky = 0;
		const char *status = "blind";

		if (nx) sx = (x1 - x0 + 1.0) / g_comp_w;
		if (ny) sy = (y1 - y0 + 1.0) / g_comp_h;

		if (nx && ny) {
			//	THE CROSS-AXIS CONTROL, KEPT. Whenever both axes are available
			//	they must imply the same zoom, exactly as in A3d and A3e. Going
			//	per-axis added a control, it did not trade this one away.
			diff = (sy > 0) ? fabs(sx - sy) / sy : 1.0;
			if (diff > AXIS_TOL) {
				++g_cap_axis;
				status = "axis_disagree";		// both refused, as before
			} else {
				okx = oky = 1;
				status = "ok";
				++g_cap_bothx;
			}
		} else if (nx) {
			okx = 1; status = "x_only"; ++g_cap_onlyx;
		} else if (ny) {
			oky = 1; status = "y_only"; ++g_cap_onlyy;
		}

		if (okx) ++g_cap_okx;
		if (oky) ++g_cap_oky;
		if (!okx && !oky) ++g_cap_blind;

		++g_cap_total;
		g_cap_cost_sum += cost;
		if (cost > g_cap_cost_worst) g_cap_cost_worst = cost;

		if (okx || oky) {
			EnterCriticalSection(&g_lock);
			Sample *s = &g_ring[g_head % OS_RING];
			s->t_ms = t_taken;
			s->sx = sx; s->tx = x0; s->okx = okx;
			s->sy = sy; s->ty = y0; s->oky = oky;
			++g_head; if (g_filled < OS_RING) ++g_filled;
			LeaveCriticalSection(&g_lock);
		}

		if (clog)
			fprintf(clog, "%.3f\t%d\t%d\t%.6f\t%.6f\t%.2f\t%.2f\t%.3f\t%.4f\t%s\t%d\t%d\t%s\t%s\n",
			        t_taken, pw, ph, sx, sy, x0, y0, diff * 100.0, cost, status,
			        nx, ny, WhyName(why_x), WhyName(why_y));

		//	No Sleep. The BitBlt's own composition sync paces this loop at the
		//	display rate, which is the cadence being characterised.
	}

	if (clog) fclose(clog);
	FreeCapture();
	ReleaseDC(NULL, screenDC);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Reading the ring, PER AXIS - that is the whole of stage 1a.        */
/* ------------------------------------------------------------------ */

//	Newest sample whose chosen axis is valid and which is at least age_ms old.
//	Each axis ages on its own clock, so one going blind no longer freezes the
//	other - which in run 1 threw away a perfectly good horizontal fix for 2.7s.
static bool
GetAxis(double now_ms, double age_ms, int want_x,
        double *sP, double *tP, double *whenP)
{
	bool found = false;
	EnterCriticalSection(&g_lock);
	for (long i = 1; i <= g_filled; ++i) {
		Sample *s = &g_ring[(g_head - i + OS_RING * 2) % OS_RING];
		if (want_x ? !s->okx : !s->oky) continue;
		if (now_ms - s->t_ms >= age_ms) {
			*sP = want_x ? s->sx : s->sy;
			*tP = want_x ? s->tx : s->ty;
			*whenP = s->t_ms;
			found = true;
			break;
		}
	}
	LeaveCriticalSection(&g_lock);
	return found;
}

/* ------------------------------------------------------------------ */
/*  The overlay.                                                      */
/* ------------------------------------------------------------------ */

static FILE		*g_plog		= NULL;
static long		g_paints	= 0, g_paints_hidden = 0, g_paints_stale = 0;
static double	g_last_paint = 0.0, g_paint_gap_sum = 0.0, g_paint_gap_worst = 0.0;
static bool		g_shown		= true;

static LRESULT CALLBACK
OverlayWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return DefWindowProcW(h, m, w, l);
}

static void
Paint(void)
{
	RECT cr; GetClientRect(g_viewer, &cr);
	int pw = cr.right - cr.left, ph = cr.bottom - cr.top;
	if (pw <= 0 || ph <= 0) return;

	POINT origin = {0, 0}; ClientToScreen(g_viewer, &origin);
	SetWindowPos(g_overlay, HWND_TOPMOST, origin.x, origin.y, pw, ph, SWP_NOACTIVATE);

	double now = NowMs();

	double sx = 0, tx = 0, wx = 0, sy = 0, ty = 0, wy = 0;
	bool hx = GetAxis(now, 0.0, 1, &sx, &tx, &wx);
	bool hy = GetAxis(now, 0.0, 0, &sy, &ty, &wy);

	double age_x = hx ? now - wx : 1e9;
	double age_y = hy ? now - wy : 1e9;

	//	A3f STAGE 2. An axis with no fresh fix is UNTRUSTED, and beyond
	//	OS_HIDE_MS the overlay shows nothing at all. Run 1 held the last good
	//	value and kept drawing a confident rectangle; that is the roadmap's
	//	founding failure - an onion skin that lies about where the drawing was is
	//	worse than no onion skin. Being wrong is not allowed to look like being
	//	right.
	bool stale_x = !hx || age_x > OS_STALE_LIMIT_MS;
	bool stale_y = !hy || age_y > OS_STALE_LIMIT_MS;
	bool hide    = (!hx && !hy) ||
	               (age_x > OS_HIDE_MS && age_y > OS_HIDE_MS);

	if (hide) {
		if (g_shown) { ShowWindow(g_overlay, SW_HIDE); g_shown = false; }
		++g_paints_hidden;
		++g_paints;
		if (g_plog && g_paints < OS_MAX_PAINTS)
			fprintf(g_plog, "%.3f\t\t\t\t\t0\t\t\t\t0.0\thidden\n", now);
		return;
	}
	if (!g_shown) { ShowWindow(g_overlay, SW_SHOWNOACTIVATE); g_shown = true; }
	if (stale_x || stale_y) ++g_paints_stale;

	double sxs = 0, txs = 0, wxs = 0, sys = 0, tys = 0, wys = 0;
	bool cx = GetAxis(now, OS_STALE_MS, 1, &sxs, &txs, &wxs);
	bool cy = GetAxis(now, OS_STALE_MS, 0, &sys, &tys, &wys);

	int stride = pw * 4;
	BYTE *bits = NULL;

	BITMAPINFO bi = {0};
	bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth       = pw;
	bi.bmiHeader.biHeight      = -ph;
	bi.bmiHeader.biPlanes      = 1;
	bi.bmiHeader.biBitCount    = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	HDC screenDC = GetDC(NULL);
	HDC memDC    = CreateCompatibleDC(screenDC);
	HBITMAP dib  = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
	HGDIOBJ old  = SelectObject(memDC, dib);

	int rows[OS_SCAN_LINES], cols[OS_SCAN_LINES], nlines;
	EnterCriticalSection(&g_line_lock);
	memcpy(rows, g_rows, sizeof(rows));
	memcpy(cols, g_cols, sizeof(cols));
	nlines = g_nlines;
	LeaveCriticalSection(&g_line_lock);

	if (bits) {
		memset(bits, 0, (size_t)stride * ph);

		//	Alpha stays 0 on EVERY sample line - five rows and five columns now,
		//	not one of each - so the detector always reads AE through the gap and
		//	can never converge on our own output.
		#define OS_PLOT(px, py, B, G, R) do {                              \
			int xx = (int)(px), yy = (int)(py);                            \
			if (xx >= 0 && xx < pw && yy >= 0 && yy < ph) {                \
				int _hit = 0;                                              \
				for (int _k = 0; _k < nlines; ++_k)                        \
					if (yy == rows[_k] || xx == cols[_k]) { _hit = 1; break; } \
				if (!_hit) {                                               \
					BYTE *p = bits + (size_t)yy * stride + (size_t)xx * 4; \
					p[0] = (B); p[1] = (G); p[2] = (R); p[3] = 255;        \
				} } } while (0)

		//	dash != 0 draws a broken line: that is what "do not trust this edge"
		//	looks like without hiding information the run still needs.
		#define OS_BOX(X0, Y0, X1, Y1, B, G, R, thick, dash) do {                   \
			for (int _t = 0; _t < (thick); ++_t) {                                 \
				for (double _x = (X0); _x <= (X1); _x += 1.0) {                     \
					if ((dash) && ((int)_x / 8) % 2) continue;                      \
					OS_PLOT(_x, (Y0) + _t, B, G, R);                               \
					OS_PLOT(_x, (Y1) - _t, B, G, R);                               \
				}                                                                   \
				for (double _y = (Y0); _y <= (Y1); _y += 1.0) {                     \
					if ((dash) && ((int)_y / 8) % 2) continue;                      \
					OS_PLOT((X0) + _t, _y, B, G, R);                               \
					OS_PLOT((X1) - _t, _y, B, G, R);                               \
				}                                                                   \
			} } while (0)

		double bx0 = tx, bx1 = tx + sx * g_comp_w;
		double by0 = ty, by1 = ty + sy * g_comp_h;

		//	An axis with no fix at all has no extent to draw; fall back to the
		//	panel so the other axis is still legible, and dash it.
		if (!hx) { bx0 = 0; bx1 = pw - 1; }
		if (!hy) { by0 = 0; by1 = ph - 1; }

		//	Magenta first so green wins where they coincide.
		if (cx && cy)
			OS_BOX(txs, tys, txs + sxs * g_comp_w, tys + sys * g_comp_h,
			       255, 40, 255, 2, 0);

		if (stale_x || stale_y)
			OS_BOX(bx0, by0, bx1, by1, 40, 190, 255, 3, 1);		// amber, dashed
		else
			OS_BOX(bx0, by0, bx1, by1, 40, 255, 40, 3, 0);		// green, solid

		#undef OS_BOX
		#undef OS_PLOT

		POINT ptSrc = {0, 0};
		POINT ptDst = {origin.x, origin.y};
		SIZE  size  = {pw, ph};
		BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
		UpdateLayeredWindow(g_overlay, screenDC, &ptDst, &size,
		                    memDC, &ptSrc, 0, &bf, ULW_ALPHA);
	}

	SelectObject(memDC, old);
	DeleteObject(dib);
	DeleteDC(memDC);
	ReleaseDC(NULL, screenDC);

	double done = NowMs();
	if (g_last_paint > 0.0) {
		double gap = now - g_last_paint;
		g_paint_gap_sum += gap;
		if (gap > g_paint_gap_worst) g_paint_gap_worst = gap;
	}
	g_last_paint = now;

	if (g_plog && g_paints < OS_MAX_PAINTS)
		fprintf(g_plog, "%.3f\t%.3f\t%.2f\t%.2f\t%.6f\t%d\t%.3f\t%.2f\t%.2f\t%.4f\t%s\n",
		        now, hx ? wx : -9999.0, tx, ty, sx,
		        (cx && cy) ? 1 : 0,
		        (cx && cy) ? wxs : -9999.0,
		        (cx && cy) ? txs : -9999.0,
		        (cx && cy) ? tys : -9999.0,
		        done - now,
		        (stale_x || stale_y) ? "stale" : "ok");
	++g_paints;
}

static VOID CALLBACK
TickProc(HWND, UINT, UINT_PTR, DWORD) { Paint(); }

/* ------------------------------------------------------------------ */

int
wmain(int argc, wchar_t **argv)
{
	{
		typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
		HMODULE u32 = GetModuleHandleW(L"user32.dll");
		SetCtxFn set = u32 ? (SetCtxFn)GetProcAddress(u32, "SetProcessDpiAwarenessContext") : NULL;
		if (!set || !set((HANDLE)(INT_PTR)-4)) SetProcessDPIAware();
	}

	g_comp_w = (argc > 1) ? _wtoi(argv[1]) : 1920;
	g_comp_h = (argc > 2) ? _wtoi(argv[2]) : 1080;
	int secs = (argc > 3) ? _wtoi(argv[3]) : 60;
	if (g_comp_w <= 0) g_comp_w = 1920;
	if (g_comp_h <= 0) g_comp_h = 1080;
	if (secs <= 0 || secs > 600) secs = 60;

	QueryPerformanceFrequency(&g_freq);
	QueryPerformanceCounter(&g_t0);
	InitializeCriticalSection(&g_lock);
	InitializeCriticalSection(&g_line_lock);

	wprintf(L"A3e/A3f slip probe - comp %dx%d, %ds\n", g_comp_w, g_comp_h, secs);
	wprintf(L"Hover over the COMP VIEWER IMAGE AREA...\n");
	for (int i = 5; i > 0; --i) { wprintf(L"  %d...\n", i); Sleep(1000); }

	POINT pt; GetCursorPos(&pt);
	g_viewer = WindowFromPoint(pt);
	if (!g_viewer) { wprintf(L"FAIL: no window under the cursor.\n"); return 1; }

	wchar_t cls[128] = {0};
	GetClassNameW(g_viewer, cls, 128);
	wprintf(L"\nviewer hwnd %p  class %s\n", (void *)g_viewer, cls);

	WNDCLASSEXW wc = {0};
	wc.cbSize        = sizeof(wc);
	wc.lpfnWndProc   = OverlayWndProc;
	wc.hInstance     = GetModuleHandleW(NULL);
	wc.lpszClassName = L"OnionSkinA3eOverlay";
	RegisterClassExW(&wc);

	RECT cr; GetClientRect(g_viewer, &cr);
	POINT origin = {0, 0}; ClientToScreen(g_viewer, &origin);

	g_overlay = CreateWindowExW(
		WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
		WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
		L"OnionSkinA3eOverlay", L"", WS_POPUP,
		origin.x, origin.y, cr.right - cr.left, cr.bottom - cr.top,
		NULL, NULL, wc.hInstance, NULL);
	if (!g_overlay) { wprintf(L"FAIL: no overlay window.\n"); return 1; }
	ShowWindow(g_overlay, SW_SHOWNOACTIVATE);

	g_plog = _wfopen(L"A3e_paints.txt", L"w");
	if (g_plog) {
		fprintf(g_plog, "# onion skin A3e/A3f paint log\n");
		fprintf(g_plog, "# comp\t%d\t%d\n", g_comp_w, g_comp_h);
		fprintf(g_plog, "# stale_ms\t%.1f\n", OS_STALE_MS);
		fprintf(g_plog, "t_ms\tsrc_t_ms\ttx\tty\tsx\thave_stale\tstale_t_ms\tstale_tx\tstale_ty\tpaint_ms\ttrust\n");
	}

	HANDLE cap = CreateThread(NULL, 0, CaptureThread, NULL, 0, NULL);
	if (!cap) { wprintf(L"FAIL: no capture thread.\n"); return 1; }
	SetThreadPriority(cap, THREAD_PRIORITY_ABOVE_NORMAL);

	timeBeginPeriod(1);
	UINT_PTR timer = SetTimer(NULL, 0, OS_TICK_MS, TickProc);

	wprintf(L"\nOVERLAY UP.\n");
	wprintf(L"  GREEN solid   = live fix on both axes\n");
	wprintf(L"  AMBER dashed  = an axis is STALE (>%.0fms) - do not trust it\n",
	        OS_STALE_LIMIT_MS);
	wprintf(L"  NOTHING       = fully blind (>%.0fms). Honest, and the point of stage 2.\n",
	        OS_HIDE_MS);
	wprintf(L"  MAGENTA       = the %.0fms-stale measurement control\n", OS_STALE_MS);
	wprintf(L"\nSTART RECORDING, then in order:\n");
	wprintf(L"  1. hold still                     (~10s)\n");
	wprintf(L"  2. wheel-scroll                   (~10s)\n");
	wprintf(L"  3. hand-drag hard                 (~10s)\n");
	wprintf(L"  4. ZOOM PAST 100%% and pan around  (~20s)  <- the A3f case\n");
	wprintf(L"  5. zoom way OUT, small comp, pan  (~10s)  <- the other A3f case\n\n");

	DWORD end = GetTickCount() + (DWORD)secs * 1000;
	MSG msg;
	for (;;) {
		if (GetTickCount() >= end) break;
		if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		} else {
			MsgWaitForMultipleObjects(0, NULL, FALSE, 5, QS_ALLINPUT);
		}
	}

	KillTimer(NULL, timer);
	timeEndPeriod(1);
	InterlockedExchange(&g_stop, 1);
	WaitForSingleObject(cap, 2000);
	CloseHandle(cap);
	DestroyWindow(g_overlay);
	if (g_plog) fclose(g_plog);
	DeleteCriticalSection(&g_lock);
	DeleteCriticalSection(&g_line_lock);

	wprintf(L"\nCAPTURE THREAD\n");
	wprintf(L"  samples        %ld\n", g_cap_total);
	wprintf(L"  both axes      %ld  (%.1f%%)\n", g_cap_bothx,
	        g_cap_total ? 100.0 * g_cap_bothx / g_cap_total : 0.0);
	wprintf(L"  x only         %ld   <- would have been BLIND in run 1\n", g_cap_onlyx);
	wprintf(L"  y only         %ld   <- would have been BLIND in run 1\n", g_cap_onlyy);
	wprintf(L"  axes disagree  %ld   (both refused - the cross-axis control)\n", g_cap_axis);
	wprintf(L"  FULLY BLIND    %ld  (%.1f%%)  <- THE A3f NUMBER\n", g_cap_blind,
	        g_cap_total ? 100.0 * g_cap_blind / g_cap_total : 0.0);
	wprintf(L"  x available    %ld  (%.1f%%)\n", g_cap_okx,
	        g_cap_total ? 100.0 * g_cap_okx / g_cap_total : 0.0);
	wprintf(L"  y available    %ld  (%.1f%%)\n", g_cap_oky,
	        g_cap_total ? 100.0 * g_cap_oky / g_cap_total : 0.0);
	wprintf(L"  cost mean      %.3f ms   worst %.3f ms\n",
	        g_cap_total ? g_cap_cost_sum / g_cap_total : 0.0, g_cap_cost_worst);

	wprintf(L"\nPAINT THREAD\n");
	wprintf(L"  paints         %ld\n", g_paints);
	wprintf(L"  drawn stale    %ld  (amber dashed - shown as untrusted)\n", g_paints_stale);
	wprintf(L"  hidden         %ld  (%.1f%% - shown as nothing, which is honest)\n",
	        g_paints_hidden, g_paints ? 100.0 * g_paints_hidden / g_paints : 0.0);
	wprintf(L"  gap mean       %.2f ms   worst %.2f ms\n",
	        g_paints > 1 ? g_paint_gap_sum / (g_paints - 1) : 0.0, g_paint_gap_worst);

	wprintf(L"\nwrote A3e_captures.txt and A3e_paints.txt\n");
	wprintf(L"A3f PASSES if FULLY BLIND is under 1%% across the matrix above,\n");
	wprintf(L"and every remaining blind frame was SHOWN as blind.\n");
	return 0;
}
