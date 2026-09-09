/*
	A3d2_CaptureBench.cpp (Onion Skin) - Phase 0, spike A3d, step 2.

	THE QUESTION. A3d's strip detection WORKS - 83.5% accepted, only 1.1% of
	samples where the two axes disagreed, and it recovered 15 distinct zoom
	values from 0.22 to 0.63. What it cannot do is fit in a tick: mean 31.6ms,
	worst 45ms, for two 1px BitBlts and an O(w+h) scan.

	The cost is not the scan. It was identical whether an edge was found
	(31.7ms) or not (30.9ms), and it did not vary with panel size. It is the
	BitBlt.

	THE HYPOTHESIS. 31.6ms for two blits on a 60Hz display is suspiciously close
	to 2 x 16.7ms. If each screen readback blocks on a composition/vsync sync,
	then the cost is PER CALL, not per pixel - and one blit of the whole panel
	would cost the same as one thin strip while giving us both axes.

	That is worth measuring rather than believing, because it decides whether
	A3d is dead or merely mis-implemented:

	  cost per CALL   -> one full-panel blit ~16ms, a 60Hz budget, A3d lives
	  cost per PIXEL  -> a full panel blit is far worse, and the thin strips
	                     were already the cheap version. A3d is dead as designed.

	So this times six strategies against the same window, back to back:

	  A  two thin blits, screen DC          (what A3d does now)
	  B  one thin blit, screen DC           (halves the calls)
	  C  one full-panel blit, screen DC     (one call, all the pixels)
	  D  two thin blits, window DC          (does GetDC(hwnd) skip the desktop?)
	  E  one full-panel blit, window DC
	  F  PrintWindow whole window           (what A1c1 used, never timed)

	A and B differing by ~2x with C ~= B proves per-call. A ~= B with C much
	worse proves per-pixel. Anything else means neither model is right.

	Build:
	    cl /nologo /EHsc /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE \
	       A3d2_CaptureBench.cpp /link user32.lib gdi32.lib /OUT:os_A3d2.exe

	Usage: os_A3d2.exe [iterations]     default 60

	Hover over the comp viewer for the countdown. Leave AE ALONE while it runs -
	this measures capture cost, and interacting would add AE's redraw to it.
*/

#include <windows.h>
#include <stdio.h>

static LARGE_INTEGER g_freq;

static double
Ms(LARGE_INTEGER a, LARGE_INTEGER b)
{
	return (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)g_freq.QuadPart;
}

struct Result {
	const wchar_t *name;
	double mean, p50, worst, best;
};

static int
CmpD(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : (x > y ? 1 : 0);
}

static void
Summarise(Result *r, double *samples, int n)
{
	qsort(samples, n, sizeof(double), CmpD);
	double sum = 0.0;
	for (int i = 0; i < n; ++i) sum += samples[i];
	r->mean  = sum / n;
	r->p50   = samples[n / 2];
	r->best  = samples[0];
	r->worst = samples[n - 1];
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
	QueryPerformanceFrequency(&g_freq);

	int iters = (argc > 1) ? _wtoi(argv[1]) : 60;
	if (iters < 5 || iters > 2000) iters = 60;

	wprintf(L"Hover over the COMP VIEWER IMAGE AREA...\n");
	for (int i = 5; i > 0; --i) { wprintf(L"  %d...\n", i); Sleep(1000); }

	POINT pt; GetCursorPos(&pt);
	HWND viewer = WindowFromPoint(pt);
	if (!viewer) { wprintf(L"FAIL: no window under the cursor.\n"); return 1; }

	RECT cr; GetClientRect(viewer, &cr);
	int pw = cr.right - cr.left, ph = cr.bottom - cr.top;
	POINT origin = {0, 0}; ClientToScreen(viewer, &origin);
	if (pw <= 0 || ph <= 0) { wprintf(L"FAIL: client area %dx%d\n", pw, ph); return 1; }

	wprintf(L"\nviewer %p   client %dx%d at %d,%d\n", (void *)viewer, pw, ph,
	        (int)origin.x, (int)origin.y);
	wprintf(L"LEAVE AE ALONE - %d iterations per strategy.\n\n", iters);

	HDC screenDC = GetDC(NULL);
	HDC winDC    = GetDC(viewer);

	//	Every destination allocated once, so allocation is never inside a timed
	//	region - otherwise this would measure GDI allocation, not capture.
	HDC memDC = CreateCompatibleDC(screenDC);
	BITMAPINFO bi = {0};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 24;
	bi.bmiHeader.biCompression = BI_RGB;
	BYTE *bitsH = NULL, *bitsV = NULL, *bitsFull = NULL;

	bi.bmiHeader.biWidth = pw; bi.bmiHeader.biHeight = -1;
	HBITMAP bmpH = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, (void **)&bitsH, NULL, 0);
	bi.bmiHeader.biWidth = 1;  bi.bmiHeader.biHeight = -ph;
	HBITMAP bmpV = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, (void **)&bitsV, NULL, 0);
	bi.bmiHeader.biWidth = pw; bi.bmiHeader.biHeight = -ph;
	HBITMAP bmpFull = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, (void **)&bitsFull, NULL, 0);

	if (!bmpH || !bmpV || !bmpFull) { wprintf(L"FAIL: could not allocate.\n"); return 1; }

	int row = ph / 2 + 37; if (row < 0 || row >= ph) row = ph / 2;
	int col = pw / 2 + 53; if (col < 0 || col >= pw) col = pw / 2;

	double *s = (double *)malloc(sizeof(double) * iters);
	Result results[6];
	int nres = 0;

	#define TIME_BLOCK(NAME, BODY) do {                          \
		for (int i = 0; i < iters; ++i) {                        \
			LARGE_INTEGER a, b;                                  \
			QueryPerformanceCounter(&a);                         \
			BODY;                                                \
			QueryPerformanceCounter(&b);                         \
			s[i] = Ms(a, b);                                     \
		}                                                        \
		results[nres].name = NAME;                               \
		Summarise(&results[nres], s, iters);                     \
		wprintf(L"  %-34s mean %8.3f  p50 %8.3f  best %8.3f  worst %8.3f\n", \
		        results[nres].name, results[nres].mean,          \
		        results[nres].p50, results[nres].best, results[nres].worst); \
		++nres;                                                  \
	} while (0)

	HGDIOBJ old;

	TIME_BLOCK(L"A two thin blits, screen DC", {
		old = SelectObject(memDC, bmpH);
		BitBlt(memDC, 0, 0, pw, 1, screenDC, origin.x, origin.y + row, SRCCOPY);
		SelectObject(memDC, bmpV);
		BitBlt(memDC, 0, 0, 1, ph, screenDC, origin.x + col, origin.y, SRCCOPY);
		SelectObject(memDC, old);
	});

	TIME_BLOCK(L"B one thin blit, screen DC", {
		old = SelectObject(memDC, bmpH);
		BitBlt(memDC, 0, 0, pw, 1, screenDC, origin.x, origin.y + row, SRCCOPY);
		SelectObject(memDC, old);
	});

	TIME_BLOCK(L"C one FULL-panel blit, screen DC", {
		old = SelectObject(memDC, bmpFull);
		BitBlt(memDC, 0, 0, pw, ph, screenDC, origin.x, origin.y, SRCCOPY);
		SelectObject(memDC, old);
	});

	TIME_BLOCK(L"D two thin blits, window DC", {
		old = SelectObject(memDC, bmpH);
		BitBlt(memDC, 0, 0, pw, 1, winDC, 0, row, SRCCOPY);
		SelectObject(memDC, bmpV);
		BitBlt(memDC, 0, 0, 1, ph, winDC, col, 0, SRCCOPY);
		SelectObject(memDC, old);
	});

	TIME_BLOCK(L"E one FULL-panel blit, window DC", {
		old = SelectObject(memDC, bmpFull);
		BitBlt(memDC, 0, 0, pw, ph, winDC, 0, 0, SRCCOPY);
		SelectObject(memDC, old);
	});

	{
		typedef BOOL (WINAPI *PrintWindowFn)(HWND, HDC, UINT);
		PrintWindowFn pwf = (PrintWindowFn)GetProcAddress(
			GetModuleHandleW(L"user32.dll"), "PrintWindow");
		if (pwf) {
			TIME_BLOCK(L"F PrintWindow whole window", {
				old = SelectObject(memDC, bmpFull);
				pwf(viewer, memDC, 2);
				SelectObject(memDC, old);
			});
		}
	}

	#undef TIME_BLOCK

	wprintf(L"\n---- reading this ----\n");
	wprintf(L"A ~= 2x B, and C ~= B  -> cost is PER CALL (a composition sync).\n");
	wprintf(L"   One full-panel blit then buys both axes for one sync, and A3d\n");
	wprintf(L"   fits a 60Hz budget after all.\n");
	wprintf(L"A ~= B, and C much worse -> cost is PER PIXEL. The thin strips were\n");
	wprintf(L"   already the cheap version and A3d is dead as designed.\n");
	wprintf(L"D or E far below A -> the window DC skips the desktop readback,\n");
	wprintf(L"   which would be the cheapest fix of all.\n");
	wprintf(L"\nA blit that returns in well under a millisecond may be reading a\n");
	wprintf(L"STALE or BLANK surface rather than being fast - so any strategy\n");
	wprintf(L"that looks free here must be re-checked with os_A3d.exe for accept\n");
	wprintf(L"rate before it is believed.\n");

	free(s);
	DeleteObject(bmpH); DeleteObject(bmpV); DeleteObject(bmpFull);
	DeleteDC(memDC);
	ReleaseDC(viewer, winDC);
	ReleaseDC(NULL, screenDC);
	return 0;
}
