/*
	A1c1_Capture.cpp (Onion Skin) - throwaway .exe for Phase 0, spike A1c.

	THE JOB:

	  Capture the comp viewer panel's pixels, and record exactly which window
	  was captured and where it sits on screen. Everything else in A1c is
	  offline analysis of what this produces.

	WHY THIS AND NOT saveBlittedImageToPng. That method silently no-ops: 32
	argument combinations, zero throws, zero files (A1c0). Its integer is
	probably an AE enum in the 7000-8100 range - viewer.type was 7612, channels
	7812, fastPreview 8012 - so the 0..7 sweep was outside the domain. It could
	be chased. It is not worth chasing: it writes a PNG to DISK, so it could
	only ever be a calibration instrument, never a live per-frame source, and
	for calibration a screen capture does the same job with no undocumented
	behaviour underneath.

	WHAT A1 NEEDS FROM THIS. A1b solved the scale exactly
	(views[i].options.zoom). The pan translation is the only unknown left. With
	markers at known comp coordinates, one capture gives both - and the solved
	zoom is then an INDEPENDENT check on the value ExtendScript reported. Two
	sources that must agree beats one source that cannot be verified.

	PICKING THE WINDOW. A1a established nothing can name AE's panels: every one
	is 'DroverLord - Window Class'. So the user points at it. A countdown, then
	WindowFromPoint at the cursor - no hooks, no permissions, no guessing.

	OUTPUT
	  A1c1_cap_<n>.bmp    the panel's client area, top-down 24-bit BMP
	  A1c1_cap_<n>.txt    the window handle, class, screen rect, client rect,
	                      DPI, and the capture method that worked

	BMP because it needs no encoder and no dependency; PIL reads it fine.

	Build (from this directory, any Developer prompt):
	    cl /nologo /EHsc /DUNICODE /D_UNICODE A1c1_Capture.cpp \
	       /link user32.lib gdi32.lib /OUT:os_A1c1.exe

	Usage:
	    os_A1c1.exe [seconds]      default 5

	Hover the cursor over the COMP VIEWER IMAGE AREA and wait.
*/

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  A top-down 24-bit BMP, written by hand.                            */
/* ------------------------------------------------------------------ */
static bool
WriteBMP(const wchar_t *path, const BYTE *bgr, int w, int h)
{
	//	Rows are DWORD-aligned in a BMP; the caller already produced them that
	//	way via GetDIBits, so just pass the stride through.
	int stride = ((w * 3) + 3) & ~3;

	BITMAPFILEHEADER fh = {0};
	BITMAPINFOHEADER ih = {0};

	fh.bfType    = 0x4D42;			// 'BM'
	fh.bfOffBits = sizeof(fh) + sizeof(ih);
	fh.bfSize    = fh.bfOffBits + stride * h;

	ih.biSize        = sizeof(ih);
	ih.biWidth       = w;
	//	Negative height = top-down, so row 0 is the TOP row. Getting this
	//	backwards would flip every marker's y and the pan solve with it.
	ih.biHeight      = -h;
	ih.biPlanes      = 1;
	ih.biBitCount    = 24;
	ih.biCompression = BI_RGB;

	FILE *f = _wfopen(path, L"wb");
	if (!f) return false;
	fwrite(&fh, sizeof(fh), 1, f);
	fwrite(&ih, sizeof(ih), 1, f);
	fwrite(bgr, stride * h, 1, f);
	fclose(f);
	return true;
}

/* ------------------------------------------------------------------ */

int
wmain(int argc, wchar_t **argv)
{
	//	Same reasoning as A1a: without per-monitor awareness every rect and
	//	every captured pixel is virtualised by the DPI scale, and the transform
	//	we are trying to measure is silently wrong.
	{
		typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
		HMODULE u32 = GetModuleHandleW(L"user32.dll");
		SetCtxFn set = u32 ? (SetCtxFn)GetProcAddress(u32,
		                       "SetProcessDpiAwarenessContext") : NULL;
		if (!set || !set((HANDLE)(INT_PTR)-4)) {
			SetProcessDPIAware();
			wprintf(L"NOTE: only system-DPI aware; rects may be virtualised.\n");
		}
	}

	int secs = 5;
	if (argc > 1) {
		int v = _wtoi(argv[1]);
		if (v > 0 && v < 60) secs = v;
	}

	wprintf(L"Hover the cursor over the COMP VIEWER IMAGE AREA.\n");
	for (int i = secs; i > 0; --i) {
		wprintf(L"  capturing in %d...\n", i);
		Sleep(1000);
	}

	POINT pt;
	GetCursorPos(&pt);
	HWND hwnd = WindowFromPoint(pt);
	if (!hwnd) { wprintf(L"FAIL: no window under the cursor.\n"); return 1; }

	wchar_t cls[128] = {0};
	GetClassNameW(hwnd, cls, 128);

	RECT wr, cr;
	GetWindowRect(hwnd, &wr);
	GetClientRect(hwnd, &cr);

	//	Client origin in screen coordinates - this is the number the transform
	//	is expressed relative to, so record it explicitly rather than deriving
	//	it later from the window rect and guessing at the border.
	POINT origin = {0, 0};
	ClientToScreen(hwnd, &origin);

	int w = cr.right - cr.left;
	int h = cr.bottom - cr.top;
	if (w <= 0 || h <= 0) {
		wprintf(L"FAIL: client area is %dx%d.\n", w, h);
		return 1;
	}

	UINT dpi = 96;
	{
		typedef UINT (WINAPI *GetDpiFn)(HWND);
		HMODULE u32 = GetModuleHandleW(L"user32.dll");
		GetDpiFn g = u32 ? (GetDpiFn)GetProcAddress(u32, "GetDpiForWindow") : NULL;
		if (g) dpi = g(hwnd);
	}

	/* -------------------------------------------------------------- */
	/*  Capture. PrintWindow first, BitBlt from the screen as fallback. */
	/*                                                                  */
	/*  They are NOT equivalent and the difference matters: PrintWindow  */
	/*  asks the window to redraw itself, so it works when occluded but  */
	/*  can come back blank for GPU-composited surfaces - which a comp   */
	/*  viewer very well may be. BitBlt from the screen DC gets exactly  */
	/*  what is displayed, but captures whatever is on top. The .txt     */
	/*  records which one produced the pixels, because a later analysis  */
	/*  that does not know cannot interpret a blank frame.               */
	/* -------------------------------------------------------------- */
	HDC     screenDC = GetDC(NULL);
	HDC     memDC    = CreateCompatibleDC(screenDC);
	HBITMAP bmp      = CreateCompatibleBitmap(screenDC, w, h);
	HGDIOBJ old      = SelectObject(memDC, bmp);

	const wchar_t *method = L"none";

	typedef BOOL (WINAPI *PrintWindowFn)(HWND, HDC, UINT);
	PrintWindowFn pw = (PrintWindowFn)GetProcAddress(
	                     GetModuleHandleW(L"user32.dll"), "PrintWindow");
	//	2 == PW_RENDERFULLCONTENT, needed for anything DirectComposition-backed.
	if (pw && pw(hwnd, memDC, 2)) {
		method = L"PrintWindow(PW_RENDERFULLCONTENT)";
	} else {
		if (BitBlt(memDC, 0, 0, w, h, screenDC, origin.x, origin.y, SRCCOPY)) {
			method = L"BitBlt(screen)";
		}
	}

	int stride = ((w * 3) + 3) & ~3;
	BYTE *pixels = (BYTE *)malloc((size_t)stride * h);
	if (!pixels) { wprintf(L"FAIL: out of memory.\n"); return 1; }

	BITMAPINFO bi = {0};
	bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth       = w;
	bi.bmiHeader.biHeight      = -h;			// top-down, as above
	bi.bmiHeader.biPlanes      = 1;
	bi.bmiHeader.biBitCount    = 24;
	bi.bmiHeader.biCompression = BI_RGB;

	int got = GetDIBits(memDC, bmp, 0, h, pixels, &bi, DIB_RGB_COLORS);

	//	A capture that came back uniformly one colour is the classic silent
	//	failure here, and it looks exactly like a successful capture of a blank
	//	panel. Say so rather than let the analysis discover it.
	bool uniform = true;
	if (got) {
		BYTE r0 = pixels[0], g0 = pixels[1], b0 = pixels[2];
		for (int y = 0; y < h && uniform; ++y) {
			const BYTE *row = pixels + (size_t)y * stride;
			for (int x = 0; x < w; ++x) {
				if (row[x*3] != r0 || row[x*3+1] != g0 || row[x*3+2] != b0) {
					uniform = false; break;
				}
			}
		}
	}

	/* -------------------------------------------------------------- */

	int n = 1;
	wchar_t bmpPath[MAX_PATH], txtPath[MAX_PATH];
	for (;;) {
		swprintf(bmpPath, MAX_PATH, L"A1c1_cap_%d.bmp", n);
		if (GetFileAttributesW(bmpPath) == INVALID_FILE_ATTRIBUTES) break;
		++n;
	}
	swprintf(txtPath, MAX_PATH, L"A1c1_cap_%d.txt", n);

	//	LOCKSTEP, ENFORCED. The solver pairs zoom reading N with capture N. In the
	//	first 15-state run the zoom script was run twice against fifteen captures,
	//	so capture 2 was judged against a reading taken eight minutes later - and
	//	the solver reported "independent sources disagree" for what was really a
	//	bookkeeping slip. Asking the operator to be careful did not work and was
	//	never going to; refuse instead.
	{
		FILE *zf = _wfopen(L"A1c1_zoom_log.txt", L"r");
		int readings = 0;
		if (zf) {
			char line[512];
			while (fgets(line, sizeof(line), zf)) {
				//	Lines look like "<n>\t<zoom>\t..." - count those only, so a
				//	header or a stray blank line cannot shift the index.
				if (line[0] >= '0' && line[0] <= '9' && strchr(line, '\t')) ++readings;
			}
			fclose(zf);
		}
		if (readings != n) {
			wprintf(L"\nREFUSING TO CAPTURE.\n");
			wprintf(L"  This would be capture %d, but the zoom log holds %d reading(s).\n",
			        n, readings);
			if (readings < n) {
				wprintf(L"  Run A1c1_Zoom.jsx in AE first - one reading per capture.\n");
			} else {
				wprintf(L"  There are more readings than captures; a capture was\n");
				wprintf(L"  probably taken and deleted. Clear both and restart the run.\n");
			}
			wprintf(L"\n  Nothing was written. The pairing must be exact or every\n");
			wprintf(L"  measurement after the slip is judged against the wrong zoom.\n");
			free(pixels);
			SelectObject(memDC, old);
			DeleteObject(bmp);
			DeleteDC(memDC);
			ReleaseDC(NULL, screenDC);
			return 4;
		}
	}

	bool wrote = got && WriteBMP(bmpPath, pixels, w, h);

	FILE *tf = _wfopen(txtPath, L"w");
	if (tf) {
		fwprintf(tf, L"capture %d\n", n);
		fwprintf(tf, L"hwnd          = %p\n", (void *)hwnd);
		fwprintf(tf, L"class         = %s\n", cls);
		fwprintf(tf, L"cursor        = %d,%d\n", (int)pt.x, (int)pt.y);
		fwprintf(tf, L"window rect   = %d,%d %dx%d\n",
		         (int)wr.left, (int)wr.top,
		         (int)(wr.right - wr.left), (int)(wr.bottom - wr.top));
		fwprintf(tf, L"client size   = %d x %d\n", w, h);
		fwprintf(tf, L"client origin = %d,%d   (screen coords)\n",
		         (int)origin.x, (int)origin.y);
		fwprintf(tf, L"dpi           = %u\n", dpi);
		fwprintf(tf, L"method        = %s\n", method);
		fwprintf(tf, L"GetDIBits rows= %d of %d\n", got, h);
		fwprintf(tf, L"uniform colour= %s\n", uniform ? L"YES - CAPTURE IS BLANK" : L"no");
		fclose(tf);
	}

	wprintf(L"\nhwnd %p  class %s\n", (void *)hwnd, cls);
	wprintf(L"client %dx%d at screen %d,%d   dpi %u\n",
	        w, h, (int)origin.x, (int)origin.y, dpi);
	wprintf(L"method: %s\n", method);
	if (uniform) {
		wprintf(L"\nWARNING: the capture is a single flat colour.\n");
		wprintf(L"  Either the wrong window was under the cursor, or this\n");
		wprintf(L"  surface cannot be captured this way. Do not analyse it.\n");
	}
	wprintf(L"\nwrote %s %s\n", bmpPath, wrote ? L"" : L"(FAILED)");
	wprintf(L"wrote %s\n", txtPath);

	free(pixels);
	SelectObject(memDC, old);
	DeleteObject(bmp);
	DeleteDC(memDC);
	ReleaseDC(NULL, screenDC);
	return uniform ? 3 : 0;
}
