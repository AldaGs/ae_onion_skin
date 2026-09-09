/*
	A3d_StripProbe.cpp (Onion Skin) - Phase 0, spike A3d, out of process.

	WHY THIS EXISTS. The in-AE version of A3d took After Effects down twice. The
	question A3d asks - can the comp's edges be found cheaply and reliably by
	sampling two 1px strips? - has nothing to do with running inside AE. So it
	runs out here, where a bug costs a process nobody minds losing. Same move
	pieFX made with S3B, and for the same reason.

	WHAT IT MEASURES

	  cost      how long one detection takes. THIS IS THE GATE: if a detection
	            cannot fit inside a tick, the approach is dead however accurate.
	  accuracy  whether the edges found are really the comp's.
	  liveness  whether it keeps working while the user scrubs, pans and zooms.

	THE CONTROL, AND WHY IT IS BETTER OUT HERE. In-process the detected span was
	checked against s*comp_size, with s read from ExtendScript - so the check
	depended on AE answering. Out here the two axes check EACH OTHER:

	    span_w / comp_w  and  span_h / comp_h  are both the zoom,
	    so they must agree.

	No AE, no ExtendScript, and it catches exactly the failure that matters - a
	detector that locked onto a panel divider or a layer outline on one axis
	will disagree with the other. A run where every sample "succeeds" but the
	two axes disagree is a FAILED run, not a passed one.

	It also means the probe MEASURES THE ZOOM as a by-product, which is a free
	cross-check against what ExtendScript reported in A3b.

	Build (from this directory, any Developer prompt):
	    cl /nologo /EHsc /DUNICODE /D_UNICODE A3d_StripProbe.cpp \
	       /link user32.lib gdi32.lib /OUT:os_A3d.exe

	Usage:
	    os_A3d.exe [comp_w] [comp_h] [seconds]      default 1920 1080 30

	DIFFERENTIAL MODE. A3d2 found that GetDC(hwnd) skips the composition sync
	that makes a screen-DC readback cost a whole frame: 0.19ms against 33ms, a
	176x speedup. That is the cheapest possible fix, which is exactly why it
	cannot be taken on trust - a blit that returns in 0.19ms may be reading a
	STALE or BLANK surface rather than being fast.

	So every iteration now detects the comp origin BOTH ways and logs both. The
	test is differential and needs no external ground truth:

	  at rest    the two must agree EXACTLY. Any standing difference means the
	             window DC is showing something other than the live window.
	  in motion  a difference is LAG. Its size in pixels is how far behind the
	             window-DC path runs, which is the number that decides whether
	             it can drive an overlay.

	A window-DC path that is blank or garbage shows up as its own no_edge count
	while the screen-DC path succeeds.

	Hover over the comp viewer image area for the countdown, then USE AE
	NORMALLY - scrub, wheel-scroll, zoom, resize the panel. The probe only
	reads pixels; it never sends input and never touches AE's process.

	Writes A3d_strips.txt: one row per sample.
*/

#include <windows.h>
#include <stdio.h>
#include <math.h>

#define BG_TOL		30			// sum |dRGB| to count as "not the background"
#define AXIS_TOL	0.01		// 1% agreement required between the two axes

static HDC		g_stripDC	= NULL;
static HBITMAP	g_bmpH		= NULL, g_bmpV = NULL;
static BYTE		*g_bitsH	= NULL, *g_bitsV = NULL;
static HBITMAP	g_bmpFull	= NULL;
static BYTE		*g_bitsFull	= NULL;
static int		g_fullStride= 0;
static int		g_w			= 0,    g_h    = 0;

static void
FreeStrips(void)
{
	if (g_bmpH)    { DeleteObject(g_bmpH); g_bmpH = NULL; g_bitsH = NULL; }
	if (g_bmpV)    { DeleteObject(g_bmpV); g_bmpV = NULL; g_bitsV = NULL; }
	if (g_bmpFull) { DeleteObject(g_bmpFull); g_bmpFull = NULL; g_bitsFull = NULL; }
	if (g_stripDC) { DeleteDC(g_stripDC);  g_stripDC = NULL; }
	g_w = g_h = 0;
}

static bool
EnsureStrips(HDC screenDC, int pw, int ph)
{
	if (g_stripDC && g_w == pw && g_h == ph) return true;
	FreeStrips();

	g_stripDC = CreateCompatibleDC(screenDC);
	if (!g_stripDC) return false;

	BITMAPINFO bi = {0};
	bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biPlanes      = 1;
	bi.bmiHeader.biBitCount    = 24;
	bi.bmiHeader.biCompression = BI_RGB;

	bi.bmiHeader.biWidth = pw;  bi.bmiHeader.biHeight = -1;
	g_bmpH = CreateDIBSection(g_stripDC, &bi, DIB_RGB_COLORS, (void **)&g_bitsH, NULL, 0);

	bi.bmiHeader.biWidth = 1;   bi.bmiHeader.biHeight = -ph;
	g_bmpV = CreateDIBSection(g_stripDC, &bi, DIB_RGB_COLORS, (void **)&g_bitsV, NULL, 0);

	//	A3d2 measured a full-panel blit at the same cost as a 1px strip
	//	(16.744ms vs 16.575ms): the screen readback pays ONE composition sync
	//	per call and the pixels are free. So take the whole panel in one call
	//	and read both axes out of it, instead of paying two syncs for two lines.
	bi.bmiHeader.biWidth = pw; bi.bmiHeader.biHeight = -ph;
	g_bmpFull = CreateDIBSection(g_stripDC, &bi, DIB_RGB_COLORS, (void **)&g_bitsFull, NULL, 0);
	g_fullStride = ((pw * 3) + 3) & ~3;

	if (!g_bmpH || !g_bmpV || !g_bmpFull) { FreeStrips(); return false; }
	g_w = pw; g_h = ph;
	return true;
}

static int
PixDiff(const BYTE *a, const BYTE *b)
{
	int d = 0;
	for (int i = 0; i < 3; ++i) { int v = (int)a[i] - (int)b[i]; d += v < 0 ? -v : v; }
	return d;
}

//	Find the first and last pixel differing from the line's own background.
//	step is the byte distance between consecutive pixels: 3 for the packed
//	horizontal row, 4 for the vertical strip whose 1px rows pad to a DWORD.
static bool
Scan(const BYTE *bits, int n, int step, int *loP, int *hiP)
{
	if (n < 16) return false;

	const BYTE *a = bits + (size_t)2 * step;
	const BYTE *b = bits + (size_t)(n - 3) * step;

	//	Both ends must agree, or the comp runs off that side and no edge is on
	//	screen. Reporting that honestly matters: at high zoom it is the CORRECT
	//	answer, not a failure of the method.
	if (PixDiff(a, b) > BG_TOL) return false;

	int lo = -1, hi = -1;
	for (int i = 2; i < n - 2; ++i)
		if (PixDiff(bits + (size_t)i * step, a) > BG_TOL) { lo = i; break; }
	if (lo < 0) return false;
	for (int i = n - 3; i > lo; --i)
		if (PixDiff(bits + (size_t)i * step, a) > BG_TOL) { hi = i; break; }
	if (hi <= lo) return false;

	*loP = lo; *hiP = hi;
	return true;
}

int
wmain(int argc, wchar_t **argv)
{
	{
		typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
		HMODULE u32 = GetModuleHandleW(L"user32.dll");
		SetCtxFn set = u32 ? (SetCtxFn)GetProcAddress(u32, "SetProcessDpiAwarenessContext") : NULL;
		if (!set || !set((HANDLE)(INT_PTR)-4)) SetProcessDPIAware();
	}

	int comp_w = (argc > 1) ? _wtoi(argv[1]) : 1920;
	int comp_h = (argc > 2) ? _wtoi(argv[2]) : 1080;
	int secs   = (argc > 3) ? _wtoi(argv[3]) : 30;
	if (comp_w <= 0) comp_w = 1920;
	if (comp_h <= 0) comp_h = 1080;
	if (secs <= 0 || secs > 300) secs = 30;

	wprintf(L"comp %dx%d, sampling for %ds\n", comp_w, comp_h, secs);
	wprintf(L"Hover over the COMP VIEWER IMAGE AREA...\n");
	for (int i = 5; i > 0; --i) { wprintf(L"  %d...\n", i); Sleep(1000); }

	POINT pt; GetCursorPos(&pt);
	HWND viewer = WindowFromPoint(pt);
	if (!viewer) { wprintf(L"FAIL: no window under the cursor.\n"); return 1; }

	wchar_t cls[128] = {0};
	GetClassNameW(viewer, cls, 128);
	wprintf(L"\nviewer hwnd %p  class %s\n", (void *)viewer, cls);
	wprintf(L"NOW USE AE: scrub, wheel-scroll, zoom, resize the panel.\n\n");

	FILE *log = _wfopen(L"A3d_strips.txt", L"w");
	if (log) {
		fprintf(log, "# onion skin A3d strip probe\n");
		fprintf(log, "# comp\t%d\t%d\n", comp_w, comp_h);
		fprintf(log, "ms\tpw\tph\tx0\tx1\ty0\ty1\tspan_w\tspan_h\tzoom_x\tzoom_y\taxis_diff_pct\tcost_ms\tstatus\twin_ok\twx0\twy0\tdx\tdy\twin_cost_ms\n");
	}

	LARGE_INTEGER f; QueryPerformanceFrequency(&f);
	LARGE_INTEGER t_start; QueryPerformanceCounter(&t_start);

	HDC screenDC = GetDC(NULL);
	HDC winDC    = GetDC(viewer);

	long n_ok = 0, n_noedge = 0, n_axis = 0, n_total = 0;
	double cost_sum = 0.0, cost_worst = 0.0;

	long n_win_ok = 0, n_cmp = 0, n_agree = 0;
	int  worst_dx = 0, worst_dy = 0;
	double wcost_sum = 0.0;

	for (;;) {
		LARGE_INTEGER now; QueryPerformanceCounter(&now);
		double elapsed = (double)(now.QuadPart - t_start.QuadPart) / (double)f.QuadPart;
		if (elapsed > secs) break;

		RECT cr; GetClientRect(viewer, &cr);
		int pw = cr.right - cr.left, ph = cr.bottom - cr.top;
		POINT origin = {0, 0}; ClientToScreen(viewer, &origin);
		if (pw <= 0 || ph <= 0) { Sleep(16); continue; }

		LARGE_INTEGER c0; QueryPerformanceCounter(&c0);

		int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
		int wx0 = 0, wx1 = 0, wy0 = 0, wy1 = 0;
		const char *status = "ok";
		bool got = false, wgot = false;
		double wcost = 0.0;

		int row = ph / 2 + 37; if (row < 0 || row >= ph) row = ph / 2;
		int col = pw / 2 + 53; if (col < 0 || col >= pw) col = pw / 2;

		if (EnsureStrips(screenDC, pw, ph)) {
			//	THE WINDOW-DC PATH IS GONE. Measured at 0.189ms - 176x faster
			//	than the screen DC - and it detected the comp in 0 of 534
			//	samples. GetDC(hwnd) on AE's viewer returns a surface with no
			//	content, so that speed was the cost of reading nothing. Exactly
			//	the trap this probe was written to catch, caught.
			//
			//	Removed rather than kept as a failing control, because its
			//	~0.9ms would land inside the cost measured below, and cost IS
			//	the gate.
			HGDIOBJ old;

			//	SCREEN DC, ONE full-panel blit - the configuration a real overlay
			//	would use. Both axes come out of the single captured frame, so
			//	this pays one composition sync instead of the two the previous
			//	run paid for two separate lines.
			old = SelectObject(g_stripDC, g_bmpFull);
			BitBlt(g_stripDC, 0, 0, pw, ph, screenDC, origin.x, origin.y, SRCCOPY);
			SelectObject(g_stripDC, old);

			//	Row: consecutive pixels, step 3. Column: step is the DIB stride.
			bool okh = Scan(g_bitsFull + (size_t)row * g_fullStride, pw, 3, &x0, &x1);
			bool okv = Scan(g_bitsFull + (size_t)col * 3, ph, g_fullStride, &y0, &y1);
			if (!okh || !okv) status = "no_edge";
			else got = true;
		} else {
			status = "no_strips";
		}

		LARGE_INTEGER c1; QueryPerformanceCounter(&c1);
		double cost = (double)(c1.QuadPart - c0.QuadPart) * 1000.0 / (double)f.QuadPart;
		cost_sum += cost;
		wcost_sum += wcost;
		if (cost > cost_worst) cost_worst = cost;
		++n_total;
		if (wgot) ++n_win_ok;
		if (got && wgot) {
			++n_cmp;
			int adx = (wx0 - x0) < 0 ? (x0 - wx0) : (wx0 - x0);
			int ady = (wy0 - y0) < 0 ? (y0 - wy0) : (wy0 - y0);
			if (adx == 0 && ady == 0) ++n_agree;
			if (adx > worst_dx) worst_dx = adx;
			if (ady > worst_dy) worst_dy = ady;
		}

		double span_w = 0, span_h = 0, zx = 0, zy = 0, diff = 0;
		if (got) {
			span_w = (double)(x1 - x0 + 1);
			span_h = (double)(y1 - y0 + 1);
			zx = span_w / comp_w;
			zy = span_h / comp_h;
			//	THE CONTROL: both axes must imply the same zoom.
			diff = (zy > 0) ? fabs(zx - zy) / zy : 1.0;
			if (diff > AXIS_TOL) { status = "axis_disagree"; ++n_axis; }
			else ++n_ok;
		} else if (status[0] == 'n' && status[1] == 'o' && status[2] == '_' && status[3] == 'e') {
			++n_noedge;
		}

		if (log) {
			//	dx/dy are the whole point of the differential run: how far the
			//	window-DC reading sits from the screen-DC reference. -9999 marks
			//	"not comparable", so a missing comparison can never be read as
			//	agreement.
			int dx = (got && wgot) ? (wx0 - x0) : -9999;
			int dy = (got && wgot) ? (wy0 - y0) : -9999;
			fprintf(log, "%.1f\t%d\t%d\t%d\t%d\t%d\t%d\t%.0f\t%.0f\t%.6f\t%.6f\t%.3f\t%.4f\t%s\t%d\t%d\t%d\t%d\t%d\t%.4f\n",
			        elapsed * 1000.0, pw, ph, x0, x1, y0, y1,
			        span_w, span_h, zx, zy, diff * 100.0, cost, status,
			        wgot ? 1 : 0, wx0, wy0, dx, dy, wcost);
		}

		Sleep(16);			// ~60Hz, the cadence a glued overlay would need
	}

	ReleaseDC(viewer, winDC);
	ReleaseDC(NULL, screenDC);
	FreeStrips();
	if (log) fclose(log);

	wprintf(L"WINDOW DC vs SCREEN DC\n");
	wprintf(L"  win detected  %ld of %ld comparable samples\n", n_win_ok, n_cmp);
	if (n_cmp) {
		wprintf(L"  agreed exactly %ld  (%.1f%%)\n", n_agree,
		        100.0 * n_agree / n_cmp);
		wprintf(L"  worst |dx|     %d px\n", worst_dx);
		wprintf(L"  worst |dy|     %d px\n", worst_dy);
	}
	wprintf(L"  win cost mean %.4f ms   (screen path mean %.4f ms)\n\n",
	        n_total ? wcost_sum / n_total : 0.0,
	        n_total ? cost_sum / n_total : 0.0);

	wprintf(L"samples        %ld\n", n_total);
	wprintf(L"  accepted     %ld  (%.1f%%)\n", n_ok, n_total ? 100.0*n_ok/n_total : 0.0);
	wprintf(L"  no edge      %ld   (correct at high zoom - comp fills the panel)\n", n_noedge);
	wprintf(L"  axes disagree %ld  (detector found something that is not the comp)\n", n_axis);
	wprintf(L"cost mean      %.4f ms\n", n_total ? cost_sum/n_total : 0.0);
	wprintf(L"cost worst     %.4f ms\n", cost_worst);
	wprintf(L"\nwrote A3d_strips.txt\n");
	wprintf(L"\nTHE GATE is the cost: a detection has to fit inside a tick.\n");
	wprintf(L"The accept rate says whether the edges are findable at all, and\n");
	wprintf(L"'axes disagree' is the count of times it would have LIED.\n");
	return 0;
}
