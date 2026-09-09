/*
	A3e_SlipProbe.cpp (Onion Skin) - Phase 0, spike A3e, out of process.

	THE QUESTION. A3d2 measured GDI screen capture at 16.7ms - one composition
	sync, one display frame - and left the decision open: is one frame of
	latency good enough for a glued overlay, or is Windows.Graphics.Capture
	required? A3e answers it in the only currency that matters: HOW MANY PIXELS
	DOES THE OVERLAY SLIP, and under which gesture.

	WHY IT IS OUT OF PROCESS, AND WHY IT NEEDS NO AE AT ALL. A3d proved the comp
	rectangle can be found from a captured panel, and that its span on the two
	axes RECOVERS THE ZOOM as a by-product. So both halves of the transform,

	    s  = span / comp_size          t  = the top-left crossing

	come out of the capture itself. No AEGP, no ExtendScript, no re-entrancy,
	and no way to take AE down - which two in-process A3d runs already did.
	This is also the SHIPPING CONFIGURATION, not a stand-in for it: one
	full-panel screen blit on a DEDICATED THREAD, an overlay repainting from
	whatever that thread has most recently published.

	The dedicated thread is the whole point. The 16.7ms is a SYNC WAIT, not CPU
	burn, so a capture loop of its own never blocks the paint; the paint reads
	the newest completed sample and returns. If capture were done inside the
	paint instead, the full 16.7ms would land in the path and the answer to this
	spike would be a different, worse number. Do not "simplify" that away.

	WHAT IS ON SCREEN
	  green    the comp rectangle from the NEWEST capture - the shipping config
	  magenta  the same, from a capture deliberately held OS_STALE_MS old

	Magenta is the BROKEN CONTROL, and it is a control of the MEASUREMENT rather
	than of the detector: if the analysis cannot separate a 100ms-stale overlay
	from a live one, then it is not resolving lag at all and the run is INVALID -
	it cannot pronounce 16.7ms acceptable, because it could not have detected
	16.7ms being unacceptable. A control that only guards the subject and not the
	instrument is how a null result gets mistaken for a pass.

	THE TWO MEASUREMENTS, AND WHY NEITHER ALONE IS ENOUGH

	  A3e_paints.txt   every paint: what it drew, and which capture it drew from.
	                   Offline, the capture stream is interpolated to the paint's
	                   own timestamp, and the difference is the slip. 60Hz, sub-
	                   pixel, and it measures the PIPELINE ONLY - capture-to-
	                   screen. It CANNOT see the sync lag itself, because the
	                   capture stream is the reference and is equally late.
	                   Therefore it is a LOWER BOUND, and it is labelled as one.

	  a screen recording   the overlay against the actual comp, same instant,
	                   whole chain. This is the number the verdict is read from.

	The standing rule applies without exception here: when the claim is about
	what the user sees, the check has to be what the user sees. The log explains
	the video; it does not replace it. (pieFX S2 paid for that lesson once.)

	PRE-COMMITTED READING - fixed before the first run, so the result cannot be
	rationalised afterwards. Judged on the VIDEO, binned by gesture:

	    at rest     <= 1 px      and   wheel scroll <= 5 px
	    hand drag   <= 20 px
	      -> GDI SHIPS. Windows.Graphics.Capture becomes a later polish item
	         (for occlusion and self-capture), not a gate.

	    anything worse
	      -> WGC IS REQUIRED on Windows, and it must be built before Phase 1.

	    magenta indistinguishable from green in the moving bins
	      -> INVALID. Fix the instrument and re-run; report nothing.

	SELF-CAPTURE. The detector reads exactly one row and one column of the
	captured panel. The overlay leaves alpha at 0 on those two lines, so the
	composited screen shows AE's pixels through the gap and the detector can
	never converge on our own output. Both boxes obey it - the magenta control
	is drawn OUTSIDE the comp edge while it lags, which is precisely where a
	contaminated detector would lock onto it.

	Build (from this directory, any Developer prompt):
	    cl /nologo /EHsc /DUNICODE /D_UNICODE A3e_SlipProbe.cpp /link user32.lib gdi32.lib winmm.lib /OUT:os_A3e.exe

	Usage:
	    os_A3e.exe [comp_w] [comp_h] [seconds]      default 1920 1080 60

	Hover over the comp viewer image area for the countdown, then USE AE:
	hold still, wheel-scroll, then drag hard with the Hand tool. Record the
	screen while you do it. The probe only reads pixels; it sends no input and
	never touches AE's process.
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

/* ------------------------------------------------------------------ */
/*  Shared state: one capture thread publishes, the paint reads.       */
/* ------------------------------------------------------------------ */

typedef struct {
	double	t_ms;			// when this capture was TAKEN (probe clock)
	double	sx, sy;			// recovered zoom per axis
	double	tx, ty;			// comp top-left in panel coords
	int		ok;				// 1 if accepted by the axis-agreement control
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
static long		g_cap_total = 0, g_cap_ok = 0, g_cap_noedge = 0, g_cap_axis = 0;
static long		g_cap_ends = 0, g_cap_notrans = 0;
static double	g_cap_cost_sum = 0.0, g_cap_cost_worst = 0.0;

//	The scan line and column, recomputed per capture from the panel size. The
//	paint needs them too, to leave its alpha gap in the right place.
static volatile LONG g_scan_row = -1, g_scan_col = -1;

static double
NowMs(void)
{
	LARGE_INTEGER n; QueryPerformanceCounter(&n);
	return (double)(n.QuadPart - g_t0.QuadPart) * 1000.0 / (double)g_freq.QuadPart;
}

/* ------------------------------------------------------------------ */
/*  Detection - the A3d method, unchanged, because it already passed.  */
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
//	thread - ~450MB/s of GDI work - and that is a plausible half of why AE
//	froze. The constraint survives into the product.
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

//	Why a scan failed. "no_edge" lumped three different causes together, and the
//	first run's 12.6% blind time could not be attributed to any of them: the comp
//	was fully inside the panel, both edges on screen, and the sample lines
//	crossing it, on every single blind run. So the probe must say WHICH test
//	rejected the line rather than leave the cause to be inferred from geometry
//	that has already been ruled out.
enum {
	kScanOK = 0,
	kScanTooShort,		//	line shorter than the minimum
	kScanEndsDiffer,	//	the two ends are not the same colour: comp runs off
	kScanNoTransition,	//	the whole line is background: comp misses this line
	kScanDegenerate		//	found a start but no end past it
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

	//	Both ends must be background, or the comp runs off that side and no edge
	//	is on screen. At high zoom that is the CORRECT answer, not a failure.
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

static const char *
ScanWhy(int h, int v)
{
	static const char *names[] = {"ok", "short", "ends_differ", "no_transition",
	                              "degenerate"};
	static char buf[64];
	sprintf(buf, "h_%s/v_%s", names[h], names[v]);
	return buf;
}

/* ------------------------------------------------------------------ */
/*  The capture thread.                                               */
/* ------------------------------------------------------------------ */

static DWORD WINAPI
CaptureThread(LPVOID)
{
	HDC screenDC = GetDC(NULL);
	FILE *clog = _wfopen(L"A3e_captures.txt", L"w");
	if (clog) {
		fprintf(clog, "# onion skin A3e capture stream (dedicated thread)\n");
		fprintf(clog, "# comp\t%d\t%d\n", g_comp_w, g_comp_h);
		fprintf(clog, "t_ms\tpw\tph\tsx\tsy\ttx\tty\taxis_diff_pct\tcost_ms\tstatus\n");
	}

	while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
		RECT cr; GetClientRect(g_viewer, &cr);
		int pw = cr.right - cr.left, ph = cr.bottom - cr.top;
		POINT origin = {0, 0}; ClientToScreen(g_viewer, &origin);
		if (pw <= 0 || ph <= 0) { Sleep(16); continue; }

		int row = ph / 2 + 37; if (row < 0 || row >= ph) row = ph / 2;
		int col = pw / 2 + 53; if (col < 0 || col >= pw) col = pw / 2;
		InterlockedExchange(&g_scan_row, row);
		InterlockedExchange(&g_scan_col, col);

		double c0 = NowMs();

		int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
		const char *status = "ok";
		bool got = false;
		int why_h = kScanOK, why_v = kScanOK;

		if (EnsureCapture(screenDC, pw, ph)) {
			HGDIOBJ old = SelectObject(g_capDC, g_capBmp);
			//	ONE full-panel blit from the screen DC. A3d2: this costs the
			//	same as a 1px strip (16.744 vs 16.575ms) because the price is
			//	one composition sync and the pixels are free.
			BitBlt(g_capDC, 0, 0, pw, ph, screenDC, origin.x, origin.y, SRCCOPY);
			SelectObject(g_capDC, old);

			why_h = Scan(g_capBits + (size_t)row * g_capStride, pw, 3, &x0, &x1);
			why_v = Scan(g_capBits + (size_t)col * 3, ph, g_capStride, &y0, &y1);
			if (why_h != kScanOK || why_v != kScanOK) status = ScanWhy(why_h, why_v);
			else got = true;
		} else {
			status = "no_capture";
		}

		double c1 = NowMs();
		double cost = c1 - c0;
		//	Timestamp the sample at the MIDPOINT of the blit, not its end. The
		//	pixels are a snapshot taken somewhere inside that 16.7ms window;
		//	stamping it at the end would build a systematic half-frame error
		//	straight into the slip number the spike exists to measure.
		double t_taken = (c0 + c1) * 0.5;

		double sx = 0, sy = 0, tx = 0, ty = 0, diff = 0;
		if (got) {
			sx = (double)(x1 - x0 + 1) / g_comp_w;
			sy = (double)(y1 - y0 + 1) / g_comp_h;
			//	THE DETECTOR'S CONTROL, carried from A3d: both axes must imply
			//	the same zoom, or what was found is not the comp.
			diff = (sy > 0) ? fabs(sx - sy) / sy : 1.0;
			if (diff > AXIS_TOL) { status = "axis_disagree"; got = false; ++g_cap_axis; }
			else { tx = (double)x0; ty = (double)y0; ++g_cap_ok; }
		} else if (why_h != kScanOK || why_v != kScanOK) {
			++g_cap_noedge;
			if (why_h == kScanEndsDiffer || why_v == kScanEndsDiffer) ++g_cap_ends;
			if (why_h == kScanNoTransition || why_v == kScanNoTransition) ++g_cap_notrans;
		}

		++g_cap_total;
		g_cap_cost_sum += cost;
		if (cost > g_cap_cost_worst) g_cap_cost_worst = cost;

		if (got) {
			EnterCriticalSection(&g_lock);
			Sample *s = &g_ring[g_head % OS_RING];
			s->t_ms = t_taken; s->sx = sx; s->sy = sy;
			s->tx = tx; s->ty = ty; s->ok = 1;
			++g_head; if (g_filled < OS_RING) ++g_filled;
			LeaveCriticalSection(&g_lock);
		}

		if (clog)
			fprintf(clog, "%.3f\t%d\t%d\t%.6f\t%.6f\t%.2f\t%.2f\t%.3f\t%.4f\t%s\n",
			        t_taken, pw, ph, sx, sy, tx, ty, diff * 100.0, cost, status);

		//	No Sleep. The BitBlt's own composition sync paces this loop at the
		//	display rate, which is exactly the cadence being characterised.
		//	Adding a sleep would measure the sleep.
	}

	if (clog) fclose(clog);
	FreeCapture();
	ReleaseDC(NULL, screenDC);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Reading the ring: newest, and newest that is at least age_ms old.  */
/* ------------------------------------------------------------------ */

static bool
GetSample(double now_ms, double age_ms, Sample *out)
{
	bool found = false;
	EnterCriticalSection(&g_lock);
	//	Walk back from newest. age_ms == 0 takes the newest; otherwise the most
	//	recent sample that is already at least that old, which is what a
	//	pipeline running age_ms behind would have had available.
	for (long i = 1; i <= g_filled; ++i) {
		Sample *s = &g_ring[(g_head - i + OS_RING * 2) % OS_RING];
		if (!s->ok) continue;
		if (now_ms - s->t_ms >= age_ms) { *out = *s; found = true; break; }
	}
	LeaveCriticalSection(&g_lock);
	return found;
}

/* ------------------------------------------------------------------ */
/*  The overlay.                                                      */
/* ------------------------------------------------------------------ */

static FILE		*g_plog		= NULL;
static long		g_paints	= 0;
static double	g_last_paint = 0.0, g_paint_gap_sum = 0.0, g_paint_gap_worst = 0.0;

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

	//	Follow the panel every paint: it can be moved or resized while we are
	//	up, and an overlay pinned at arm time is glued to nothing.
	SetWindowPos(g_overlay, HWND_TOPMOST, origin.x, origin.y, pw, ph, SWP_NOACTIVATE);

	double now = NowMs();

	Sample live, stale;
	bool have_live  = GetSample(now, 0.0, &live);
	bool have_stale = GetSample(now, OS_STALE_MS, &stale);
	if (!have_live) return;

	int scan_row = (int)InterlockedCompareExchange(&g_scan_row, -1, -1);
	int scan_col = (int)InterlockedCompareExchange(&g_scan_col, -1, -1);

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

	if (bits) {
		memset(bits, 0, (size_t)stride * ph);

		//	Alpha stays 0 on the scan row and column so the detector reads AE
		//	through the gap and can never converge on our own output.
		#define OS_PLOT(px, py, B, G, R) do {                          \
			int xx = (int)(px), yy = (int)(py);                        \
			if (xx >= 0 && xx < pw && yy >= 0 && yy < ph                \
			    && yy != scan_row && xx != scan_col) {                 \
				BYTE *p = bits + (size_t)yy * stride + (size_t)xx * 4; \
				p[0] = (B); p[1] = (G); p[2] = (R); p[3] = 255;        \
			} } while (0)

		#define OS_BOX(S, B, G, R, thick) do {                                     \
			double _x0 = (S).tx, _y0 = (S).ty;                                     \
			double _x1 = (S).tx + (S).sx * g_comp_w;                               \
			double _y1 = (S).ty + (S).sy * g_comp_h;                               \
			for (int _t = 0; _t < (thick); ++_t) {                                 \
				for (double _x = _x0; _x <= _x1; _x += 1.0) {                       \
					OS_PLOT(_x, _y0 + _t, B, G, R); OS_PLOT(_x, _y1 - _t, B, G, R); \
				}                                                                   \
				for (double _y = _y0; _y <= _y1; _y += 1.0) {                       \
					OS_PLOT(_x0 + _t, _y, B, G, R); OS_PLOT(_x1 - _t, _y, B, G, R); \
				}                                                                   \
			} } while (0)

		//	Magenta FIRST so green wins where they overlap - at rest they
		//	coincide, and the eye should see one green box, not a muddled one.
		if (have_stale) OS_BOX(stale, 255, 40, 255, 2);
		OS_BOX(live, 40, 255, 40, 3);

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

	//	One row per paint. src_t_ms is when the pixels we drew were CAPTURED, so
	//	(t_ms - src_t_ms) is the age of what is on screen. The analyser
	//	interpolates the capture stream to t_ms to get the slip in pixels.
	if (g_plog && g_paints < OS_MAX_PAINTS)
		fprintf(g_plog, "%.3f\t%.3f\t%.2f\t%.2f\t%.6f\t%d\t%.3f\t%.2f\t%.2f\t%.4f\n",
		        now, live.t_ms, live.tx, live.ty, live.sx,
		        have_stale ? 1 : 0,
		        have_stale ? stale.t_ms : -9999.0,
		        have_stale ? stale.tx : -9999.0,
		        have_stale ? stale.ty : -9999.0,
		        done - now);
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

	wprintf(L"A3e slip probe - comp %dx%d, %ds\n", g_comp_w, g_comp_h, secs);
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

	//	WS_EX_TRANSPARENT | WS_EX_NOACTIVATE: an overlay that ate the viewer's
	//	clicks or stole its focus would fail as a product however well it tracks.
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
		fprintf(g_plog, "# onion skin A3e paint log\n");
		fprintf(g_plog, "# comp\t%d\t%d\n", g_comp_w, g_comp_h);
		fprintf(g_plog, "# stale_ms\t%.1f\n", OS_STALE_MS);
		fprintf(g_plog, "t_ms\tsrc_t_ms\ttx\tty\tsx\thave_stale\tstale_t_ms\tstale_tx\tstale_ty\tpaint_ms\n");
	}

	HANDLE cap = CreateThread(NULL, 0, CaptureThread, NULL, 0, NULL);
	if (!cap) { wprintf(L"FAIL: no capture thread.\n"); return 1; }
	//	Above normal: it must not be starved by the paint, or the "one frame"
	//	claim becomes a claim about the scheduler.
	SetThreadPriority(cap, THREAD_PRIORITY_ABOVE_NORMAL);

	//	A3a: a 16ms request lands as ~30ms at Windows' default 15.6ms
	//	granularity. Raise it so the cadence measured is the real ceiling.
	timeBeginPeriod(1);
	UINT_PTR timer = SetTimer(NULL, 0, OS_TICK_MS, TickProc);

	wprintf(L"\nOVERLAY UP. Green = live capture. Magenta = %.0fms stale (the control).\n",
	        OS_STALE_MS);
	wprintf(L"START YOUR SCREEN RECORDING NOW, then in order:\n");
	wprintf(L"  1. hold completely still      (~10s)\n");
	wprintf(L"  2. wheel-scroll the viewer    (~15s)\n");
	wprintf(L"  3. hand-tool drag, hard       (~15s)\n");
	wprintf(L"  4. hold still again           (~10s)\n\n");

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

	wprintf(L"\nCAPTURE THREAD\n");
	wprintf(L"  samples        %ld\n", g_cap_total);
	wprintf(L"  accepted       %ld  (%.1f%%)\n", g_cap_ok,
	        g_cap_total ? 100.0 * g_cap_ok / g_cap_total : 0.0);
	wprintf(L"  no edge        %ld   (correct at high zoom)\n", g_cap_noedge);
	wprintf(L"  axes disagree  %ld   (times it would have LIED)\n", g_cap_axis);
	wprintf(L"  cost mean      %.3f ms   worst %.3f ms\n",
	        g_cap_total ? g_cap_cost_sum / g_cap_total : 0.0, g_cap_cost_worst);
	wprintf(L"  effective rate %.1f Hz\n",
	        g_cap_cost_sum > 0 ? 1000.0 * g_cap_total / g_cap_cost_sum : 0.0);

	wprintf(L"\nPAINT THREAD\n");
	wprintf(L"  paints         %ld\n", g_paints);
	wprintf(L"  gap mean       %.2f ms   worst %.2f ms\n",
	        g_paints > 1 ? g_paint_gap_sum / (g_paints - 1) : 0.0, g_paint_gap_worst);

	wprintf(L"\nwrote A3e_captures.txt and A3e_paints.txt\n");
	wprintf(L"Run:  py A3e_analyse.py\n");
	wprintf(L"THE VERDICT IS READ FROM THE VIDEO. The log is a lower bound on\n");
	wprintf(L"slip - it cannot see the capture sync, only the pipeline after it.\n");
	return 0;
}
