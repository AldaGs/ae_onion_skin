/*
	A3f_selftest.cpp (Onion Skin) - offline proof of the A3f detector.

	A3f changed the detector in a way that REMOVED a control: accepting the axes
	independently means the cross-axis zoom check is unavailable exactly when one
	axis is missing, which is the case A3f exists to serve. The replacement is a
	within-axis vote across several sample lines. That replacement has to be
	shown to work before a run in AE is worth doing - per
	[[verify-offline-before-rebuild]], the plug-in is a place where nothing can
	be measured.

	It includes the probe's own source so there is ONE DetectAxis, not a copy
	that can drift from the shipping one.

	Synthetic panels, known answers, and a control that must FAIL:

	  1. clean comp, fully inside the panel      -> both axes, 5/5 lines agree
	  2. comp TALLER than the panel              -> x accepted, y refused
	     (the run-1 case: at 69% zoom the horizontal axis was perfectly
	     measurable and run 1 threw it away because the vertical was not)
	  3. small comp missing SOME sample lines     -> still detected by the rest
	     (the other run-1 case, at 21.9% zoom)
	  4. comp covering the panel on both axes     -> fully blind, and says so
	  5. BROKEN CONTROL: a false rectangle on ONE line only, of the kind a layer
	     outline or a stray solid would produce -> must be OUTVOTED and refused.
	     If the vote accepts this, the control is not a control and A3f has
	     traded a real guard for a fix.

	Build:
	    cl /nologo /EHsc /DUNICODE /D_UNICODE A3f_selftest.cpp /link user32.lib gdi32.lib winmm.lib /OUT:os_A3f_test.exe
*/

#define wmain probe_wmain_unused
#include "A3e_SlipProbe.cpp"
#undef wmain

#include <stdlib.h>

#define PW	1592
#define PH	729
#define CW	1920
#define CH	1080

static BYTE *g_buf = NULL;
static int   g_stride = 0;

static void
Fill(int comp_x, int comp_y, int comp_w, int comp_h)
{
	//	Panel background 13,13,13 and comp background 64,64,64 - the real values
	//	measured off the recording, so the BG_TOL of 30 is exercised as it will
	//	be in AE rather than against an easy synthetic contrast.
	for (int y = 0; y < PH; ++y) {
		BYTE *row = g_buf + (size_t)y * g_stride;
		for (int x = 0; x < PW; ++x) {
			bool in = (x >= comp_x && x < comp_x + comp_w &&
			           y >= comp_y && y < comp_y + comp_h);
			BYTE v = in ? 64 : 13;
			row[x * 3 + 0] = v; row[x * 3 + 1] = v; row[x * 3 + 2] = v;
		}
	}
}

//	Paint a bright rectangle on exactly ONE sample row, spanning a different
//	extent from the comp. This is the shape a layer outline or a selected solid
//	leaves on a single line.
static void
Contaminate(int row, int x0, int x1)
{
	BYTE *r = g_buf + (size_t)row * g_stride;
	for (int x = x0; x <= x1; ++x) {
		r[x * 3 + 0] = 200; r[x * 3 + 1] = 200; r[x * 3 + 2] = 200;
	}
}

static int g_fail = 0;

static void
Check(const char *name, bool cond, const char *detail)
{
	wprintf(L"  %-46hs %hs   %hs\n", name, cond ? "PASS" : "FAIL", detail);
	if (!cond) ++g_fail;
}

int
wmain(void)
{
	g_stride = ((PW * 3) + 3) & ~3;
	g_buf = (BYTE *)calloc((size_t)g_stride * PH, 1);

	int rows[OS_SCAN_LINES], cols[OS_SCAN_LINES];
	for (int i = 0; i < OS_SCAN_LINES; ++i) {
		rows[i] = (int)(kScanFrac[i] * PH);
		cols[i] = (int)(kScanFrac[i] * PW);
	}

	double lo, hi;
	int why, n;
	char buf[160];

	wprintf(L"A3f detector self-test  (panel %dx%d, comp %dx%d)\n\n", PW, PH, CW, CH);

	/* 1 -------------------------------------------------------------- */
	wprintf(L"1. clean comp fully inside the panel (s = 0.5)\n");
	Fill(300, 100, 960, 540);
	//	NOT all five lines: rows[4]=648 is below the comp's bottom at 639, and
	//	cols[0]=270 / cols[4]=1416 lie outside 300..1259. Predicting WHICH lines
	//	miss is the point - a test that accepted any count would not notice a
	//	detector that had started missing lines for the wrong reason.
	n = DetectAxis(g_buf, OS_SCAN_LINES, rows, PW, 3, g_stride, &lo, &hi, &why);
	sprintf(buf, "n=%d lo=%.0f hi=%.0f (rows[4]=%d is past the comp)", n, lo, hi, rows[4]);
	Check("x: the 4 crossing lines agree exactly", n == 4 && (int)lo == 300 && (int)hi == 1259, buf);
	n = DetectAxis(g_buf, OS_SCAN_LINES, cols, PH, g_stride, 3, &lo, &hi, &why);
	sprintf(buf, "n=%d lo=%.0f hi=%.0f (cols[0]=%d, cols[4]=%d are outside)", n, lo, hi, cols[0], cols[4]);
	Check("y: the 3 crossing lines agree exactly", n == 3 && (int)lo == 100 && (int)hi == 639, buf);

	/* 2 -------------------------------------------------------------- */
	wprintf(L"\n2. comp TALLER than the panel - the run-1 69%% case\n");
	//	s = 0.69: comp drawn 1325x745 in a 729-tall panel, centred horizontally.
	Fill(133, -8, 1325, 745);
	n = DetectAxis(g_buf, OS_SCAN_LINES, rows, PW, 3, g_stride, &lo, &hi, &why);
	sprintf(buf, "n=%d lo=%.0f", n, lo);
	Check("x STILL accepted (run 1 threw this away)", n >= OS_MIN_AGREE && (int)lo == 133, buf);
	//	The reason is no_transition, NOT ends_differ - and the distinction is
	//	real. Scan takes its background reference FROM THE LINE'S OWN END PIXEL,
	//	so when the comp covers the whole line both ends are comp, they match,
	//	and nothing on the line differs from them. ends_differ is the OTHER
	//	overflow shape: the comp covering one end but not the other.
	n = DetectAxis(g_buf, OS_SCAN_LINES, cols, PH, g_stride, 3, &lo, &hi, &why);
	sprintf(buf, "n=%d why=%hs", n, WhyName(why));
	Check("y correctly refused, as no_transition", n == 0 && why == kScanNoTransition, buf);

	//	And the ends_differ shape itself, so both overflow modes are covered.
	Fill(133, 300, 1325, 745);		// comp covers the BOTTOM end only
	n = DetectAxis(g_buf, OS_SCAN_LINES, cols, PH, g_stride, 3, &lo, &hi, &why);
	sprintf(buf, "n=%d why=%hs", n, WhyName(why));
	Check("y refused as ends_differ when it covers one end", n == 0 && why == kScanEndsDiffer, buf);

	/* 3 -------------------------------------------------------------- */
	wprintf(L"\n3. small comp that ONE line misses - the run-1 21.9%% case\n");
	//	420x236 placed so the old single centre-ish column would miss it.
	Fill(120, 380, 420, 236);
	n = DetectAxis(g_buf, OS_SCAN_LINES, cols, PH, g_stride, 3, &lo, &hi, &why);
	sprintf(buf, "n=%d of %d lines, lo=%.0f", n, OS_SCAN_LINES, lo);
	Check("y found by the lines that DO cross it", n >= OS_MIN_AGREE && (int)lo == 380, buf);
	n = DetectAxis(g_buf, OS_SCAN_LINES, rows, PW, 3, g_stride, &lo, &hi, &why);
	sprintf(buf, "n=%d lo=%.0f", n, lo);
	Check("x found likewise", n >= OS_MIN_AGREE && (int)lo == 120, buf);

	/* 4 -------------------------------------------------------------- */
	wprintf(L"\n4. comp covering the panel on BOTH axes - genuinely blind\n");
	Fill(-200, -200, 2200, 1200);
	n = DetectAxis(g_buf, OS_SCAN_LINES, rows, PW, 3, g_stride, &lo, &hi, &why);
	sprintf(buf, "n=%d why=%hs", n, WhyName(why));
	Check("x refused, and says why", n == 0, buf);
	n = DetectAxis(g_buf, OS_SCAN_LINES, cols, PH, g_stride, 3, &lo, &hi, &why);
	sprintf(buf, "n=%d why=%hs", n, WhyName(why));
	Check("y refused, and says why", n == 0, buf);

	/* 5 - THE BROKEN CONTROL --------------------------------------- */
	wprintf(L"\n5. BROKEN CONTROL: a false rectangle on ONE line only\n");
	//	No comp at all, so the ONLY thing any line can find is the contamination.
	//	If the vote accepts a single line's answer, it will report a confident
	//	wrong rectangle - which is precisely the failure the cross-axis control
	//	used to catch and which the within-axis vote must now catch instead.
	Fill(0, 0, 0, 0);
	Contaminate(rows[2], 400, 900);
	n = DetectAxis(g_buf, OS_SCAN_LINES, rows, PW, 3, g_stride, &lo, &hi, &why);
	sprintf(buf, "n=%d (a lone line must not carry a fix)", n);
	Check("single contaminated line is OUTVOTED", n == 0, buf);

	//	And the other half of the control: when the contamination is REAL - two
	//	lines agreeing - it must be accepted, or the vote is simply refusing
	//	everything and would 'pass' this test by being useless.
	Contaminate(rows[3], 400, 900);
	n = DetectAxis(g_buf, OS_SCAN_LINES, rows, PW, 3, g_stride, &lo, &hi, &why);
	sprintf(buf, "n=%d lo=%.0f hi=%.0f", n, lo, hi);
	Check("two agreeing lines ARE accepted", n == 2 && (int)lo == 400, buf);

	//	Disagreeing lines must not be fused into an average of two wrong answers.
	Fill(0, 0, 0, 0);
	Contaminate(rows[0], 100, 300);
	Contaminate(rows[1], 700, 1100);
	n = DetectAxis(g_buf, OS_SCAN_LINES, rows, PW, 3, g_stride, &lo, &hi, &why);
	sprintf(buf, "n=%d (must not average two disagreeing lines)", n);
	Check("two DISAGREEING lines are refused", n == 0, buf);

	wprintf(L"\n%hs\n", g_fail ? "SELF-TEST FAILED" : "SELF-TEST PASSED");
	free(g_buf);
	return g_fail ? 1 : 0;
}
