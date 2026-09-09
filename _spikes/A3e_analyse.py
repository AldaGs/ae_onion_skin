"""
A3e_analyse.py (Onion Skin) - Phase 0, spike A3e.

Reads A3e_captures.txt (the ground-truth stream) and A3e_paints.txt (what was
actually on screen), and reports how far the overlay slipped, binned by how fast
the comp was moving at the time.

WHAT THE NUMBER IS, AND WHAT IT IS NOT

    slip(P) = | t_painted_at_P  -  t_true_interpolated_to_P |

The capture stream is the reference. But the capture stream is ITSELF one
composition sync behind the screen, and the paint drew from a member of that
same stream - so the constant part of the sync lag cancels and this measures the
PIPELINE ONLY: publish -> tick -> paint. It is therefore a LOWER BOUND on what
the user sees, and it is printed as one. The verdict comes from the video.

Reporting it as a lower bound rather than as "the slip" is the whole discipline
here: a measurement that quietly omits a term it cannot see is how 16.7ms gets
declared acceptable on the strength of a number that never contained it.

THE CONTROL. The magenta box was fed a deliberately 100ms-stale transform. If
the analysis cannot separate magenta from green in the MOVING bins, then it is
not resolving lag at all, and it could not have detected a real lag problem
either. That is reported as INVALID, not as a pass.

    py A3e_analyse.py [captures.txt] [paints.txt]
"""

import sys
import math

REST_MAX   = 20.0     # px/s - below this the comp is not moving
WHEEL_MAX  = 400.0    # px/s - wheel scroll; above is a hand drag
STALE_MS   = 100.0    # must match OS_STALE_MS in the probe

# The pre-committed reading, stated here so a run cannot renegotiate it.
BUDGET = {"at rest": 1.0, "wheel": 5.0, "drag": 20.0}


def read_table(path):
    rows, header = [], None
    with open(path, "r") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if header is None:
                header = parts
                continue
            if len(parts) != len(header):
                continue
            rows.append(dict(zip(header, parts)))
    return header, rows


def interp(stream, t):
    """True (tx, ty) at time t, linearly between the two bracketing captures.

    Returns None outside the stream or across a gap longer than 100ms - an
    interpolation over a gap that long is an invention, and inventing the
    reference is how a slip measurement flatters itself."""
    lo, hi = 0, len(stream) - 1
    if t < stream[0][0] or t > stream[-1][0]:
        return None
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if stream[mid][0] <= t:
            lo = mid
        else:
            hi = mid
    t0, x0, y0 = stream[lo]
    t1, x1, y1 = stream[hi]
    if t1 - t0 > 100.0 or t1 <= t0:
        return None
    f = (t - t0) / (t1 - t0)
    return (x0 + f * (x1 - x0), y0 + f * (y1 - y0))


def velocity(stream, t, window=50.0):
    """Comp speed in px/s around t, from the reference stream itself."""
    a = b = None
    for s in stream:
        if s[0] <= t - window / 2:
            a = s
        if b is None and s[0] >= t + window / 2:
            b = s
    if a is None or b is None or b[0] <= a[0]:
        return None
    dt = (b[0] - a[0]) / 1000.0
    return math.hypot(b[1] - a[1], b[2] - a[2]) / dt


def pct(vals, p):
    if not vals:
        return float("nan")
    v = sorted(vals)
    i = min(len(v) - 1, max(0, int(round(p / 100.0 * (len(v) - 1)))))
    return v[i]


def main():
    cap_path = sys.argv[1] if len(sys.argv) > 1 else "A3e_captures.txt"
    pnt_path = sys.argv[2] if len(sys.argv) > 2 else "A3e_paints.txt"

    _, caps = read_table(cap_path)
    _, pnts = read_table(pnt_path)

    stream = [(float(r["t_ms"]), float(r["tx"]), float(r["ty"]))
              for r in caps if r["status"] == "ok"]
    stream.sort()

    print("captures       %d total, %d accepted" % (len(caps), len(stream)))
    print("paints         %d" % len(pnts))
    if len(stream) < 50 or len(pnts) < 50:
        print("\nINVALID: not enough accepted samples to say anything.")
        return 2

    #	Gaps in the ACCEPTED stream, which is not the same thing as the capture
    #	thread's cadence: a gap here can mean the thread stalled OR that every
    #	capture in that window was rejected because a comp edge was off-panel.
    #	Reporting the maximum as "cadence" said the thread stalled for 2.7 s when
    #	in fact it never missed a beat and the DETECTOR was blind. Two different
    #	defects with two different fixes, and the label chose the wrong one.
    #	So the blind gaps are now split out and named.
    gaps = [stream[i + 1][0] - stream[i][0] for i in range(len(stream) - 1)]
    live = [g for g in gaps if g < 100.0]
    blind = [g for g in gaps if g >= 100.0]
    print("capture cadence  mean %.2f ms   p95 %.2f ms   worst %.2f ms"
          % (sum(live) / len(live), pct(live, 95), max(live))
          if live else "capture cadence  (no unbroken samples)")
    if blind:
        print("BLIND GAPS       %d, totalling %.2f s, longest %.2f s"
              % (len(blind), sum(blind) / 1000.0, max(blind) / 1000.0))
        print("                 (every capture rejected - a comp edge was off-panel.")
        print("                 NOT a stalled thread, and NOT latency.)")

    bins = {"at rest": [], "wheel": [], "drag": []}
    ctrl = {"at rest": [], "wheel": [], "drag": []}
    vels = {"at rest": [], "wheel": [], "drag": []}
    ages = []
    skipped = 0

    for r in pnts:
        t = float(r["t_ms"])
        truth = interp(stream, t)
        v = velocity(stream, t)
        if truth is None or v is None:
            skipped += 1
            continue

        name = ("at rest" if v < REST_MAX
                else "wheel" if v < WHEEL_MAX
                else "drag")

        slip = math.hypot(float(r["tx"]) - truth[0], float(r["ty"]) - truth[1])
        bins[name].append(slip)
        vels[name].append(v)
        ages.append(t - float(r["src_t_ms"]))

        if r["have_stale"] == "1":
            ctrl[name].append(math.hypot(float(r["stale_tx"]) - truth[0],
                                         float(r["stale_ty"]) - truth[1]))

    print("skipped        %d paints (outside the stream, or across a gap)" % skipped)
    print("on-screen age  mean %.1f ms   p95 %.1f ms   (capture -> paint)"
          % (sum(ages) / len(ages), pct(ages, 95)) if ages else "")

    print("\nPIPELINE SLIP - A LOWER BOUND, not the user-visible number")
    print("%-9s %7s %9s %9s %9s   %9s" %
          ("bin", "n", "median", "p95", "worst", "ctrl p95"))
    verdict_ok, invalid = True, []
    for name in ("at rest", "wheel", "drag"):
        v, c = bins[name], ctrl[name]
        if not v:
            print("%-9s %7d   (no samples in this bin)" % (name, 0))
            invalid.append("%s: never exercised" % name)
            continue
        print("%-9s %7d %9.2f %9.2f %9.2f   %9.2f" %
              (name, len(v), pct(v, 50), pct(v, 95), max(v),
               pct(c, 95) if c else float("nan")))
        if pct(v, 95) > BUDGET[name]:
            verdict_ok = False
        # The control must be clearly worse where there IS motion to lag behind.
        if name != "at rest" and c and pct(c, 95) < 3.0 * pct(v, 95):
            invalid.append(
                "%s: the %.0fms-stale control (p95 %.2f px) is not separable "
                "from the live path (p95 %.2f px)"
                % (name, STALE_MS, pct(c, 95), pct(v, 95)))

    print("\nbudget         at rest <= %.0f px, wheel <= %.0f px, drag <= %.0f px"
          % (BUDGET["at rest"], BUDGET["wheel"], BUDGET["drag"]))

    #	ATTRIBUTION. Half a capture interval of slip is unavoidable at ANY
    #	capture rate: the newest published sample is on average that old, and
    #	the picture kept moving. That floor is a property of sampling at the
    #	display rate, NOT of GDI - Windows.Graphics.Capture is paced by the same
    #	frames and would not reduce it. So a bin that busts its budget by less
    #	than this floor is telling us the budget is unreachable at this
    #	velocity, not that the capture method is the wrong one. Printing it
    #	stops a quantisation result from being read as an argument for WGC.
    half_interval = (sum(gaps) / len(gaps)) / 2000.0
    print("\nQUANTISATION FLOOR - unavoidable at any capture method, at %.1f Hz"
          % (1000.0 * len(gaps) / sum(gaps)))
    for name in ("at rest", "wheel", "drag"):
        if vels[name]:
            print("  %-9s median %7.0f px/s  ->  floor %6.2f px"
                  % (name, pct(vels[name], 50), pct(vels[name], 50) * half_interval))

    if invalid:
        print("\nINVALID - the instrument did not prove itself:")
        for m in invalid:
            print("  - " + m)
        print("\nFix the harness and re-run. Report no verdict from this data:")
        print("a run that could not have detected a failure cannot report a pass.")
        return 3

    print("\nlog verdict    %s (on the lower bound only)"
          % ("WITHIN BUDGET" if verdict_ok else "OVER BUDGET"))
    if verdict_ok:
        print("\nThis does NOT yet say GDI ships. The log cannot see the capture")
        print("sync - only the pipeline after it. Read the video for the verdict;")
        print("if the video agrees, GDI ships and WGC becomes a polish item.")
    else:
        print("\nThe pipeline alone is already over budget, and the user-visible")
        print("slip can only be larger. Before concluding WGC: check each busted")
        print("bin against its quantisation floor above. Slip ABOVE the floor is")
        print("pipeline lag and WGC can attack it; slip AT the floor is the 60Hz")
        print("sampling rate, which WGC is paced by too and would not fix.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
